#include "peer_channel.h"
#include "oi_net_os.h"
#include "oi_poll.h"
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Lan
{

namespace
{
bool IsWouldBlock()
{
#ifdef _WIN32
	int iError = WSAGetLastError();
	return iError == WSAEWOULDBLOCK || iError == WSAEINTR;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}
}

PeerChannel::PeerChannel()
    : ::OI::Net::Channel(::OI::Net::NetSocket(), NULL), m_iLocalPort(0), m_iDestIp(0), m_iDestPort(0), m_pOwner(NULL)
{
	m_vBuffer.resize(2048);
}

bool PeerChannel::Open(const std::string& sBindIp, uint16_t iPort)
{
	NetOs_Startup();
	net_socket_t iDescriptor = (net_socket_t)::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (iDescriptor == INVALID_SOCKET_ID)
	{
		return false;
	}
	int iReuse = 1;
#ifdef _WIN32
	setsockopt((SOCKET)iDescriptor, SOL_SOCKET, SO_REUSEADDR, (const char*)&iReuse, sizeof(iReuse));
#else
	setsockopt((int)iDescriptor, SOL_SOCKET, SO_REUSEADDR, (const char*)&iReuse, sizeof(iReuse));
#endif
	if (NetOs_SetNonblock(iDescriptor) != 0)
	{
		NetOs_Close(iDescriptor);
		return false;
	}
	sockaddr_in stAddress;
	memset(&stAddress, 0, sizeof(stAddress));
	stAddress.sin_family = AF_INET;
	stAddress.sin_port = htons(iPort);
	if (sBindIp.empty())
	{
		stAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else if (inet_pton(AF_INET, sBindIp.c_str(), &stAddress.sin_addr) != 1)
	{
		NetOs_Close(iDescriptor);
		return false;
	}
	if (NetOs_Bind(iDescriptor, (sockaddr*)&stAddress, sizeof(stAddress)) != 0)
	{
		NetOs_Close(iDescriptor);
		return false;
	}
	sockaddr_in stLocal;
	memset(&stLocal, 0, sizeof(stLocal));
	socklen_t iLength = sizeof(stLocal);
#ifdef _WIN32
	if (getsockname((SOCKET)iDescriptor, (sockaddr*)&stLocal, &iLength) != 0)
#else
	if (getsockname((int)iDescriptor, (sockaddr*)&stLocal, &iLength) != 0)
#endif
	{
		NetOs_Close(iDescriptor);
		return false;
	}
	m_iLocalPort = ntohs(stLocal.sin_port);
	stSocket.Attach(iDescriptor);
	::OI::Net::Poll::GetInstance().IOEventRegister(this, true, false);
	return true;
}

uint16_t PeerChannel::LocalPort() const
{
	return m_iLocalPort;
}

void PeerChannel::SetDest(uint32_t iAddress, uint16_t iPort)
{
	::OI::Thread::MutexGuard stLock(m_stLock);
	m_iDestIp = iAddress;
	m_iDestPort = iPort;
}

bool PeerChannel::SameDest(const sockaddr_in& stFrom) const
{
	return ntohl(stFrom.sin_addr.s_addr) == m_iDestIp && ntohs(stFrom.sin_port) == m_iDestPort;
}

bool PeerChannel::SendOn(net_socket_t iDescriptor, uint32_t iAddress, uint16_t iPort, const std::vector<uint8_t>& vPacket)
{
	if (vPacket.empty() || iDescriptor == INVALID_SOCKET_ID || iPort == 0)
	{
		return false;
	}
	sockaddr_in stAddress;
	memset(&stAddress, 0, sizeof(stAddress));
	stAddress.sin_family = AF_INET;
	stAddress.sin_port = htons(iPort);
	stAddress.sin_addr.s_addr = htonl(iAddress);
#ifdef _WIN32
	int iSent = (int)::sendto((SOCKET)iDescriptor, (const char*)vPacket.data(), (int)vPacket.size(), 0, (sockaddr*)&stAddress, sizeof(stAddress));
#else
	int iSent = (int)::sendto((int)iDescriptor, (const char*)vPacket.data(), (int)vPacket.size(), 0, (sockaddr*)&stAddress, sizeof(stAddress));
#endif
	return iSent == (int)vPacket.size();
}

bool PeerChannel::Send(const std::vector<uint8_t>& vPacket)
{
	uint32_t iAddress = 0;
	uint16_t iPort = 0;
	{
		::OI::Thread::MutexGuard stLock(m_stLock);
		iAddress = m_iDestIp;
		iPort = m_iDestPort;
	}
	return SendOn(stSocket.GetSocket(), iAddress, iPort, vPacket);
}

bool PeerChannel::SendTo(uint32_t iAddress, uint16_t iPort, const std::vector<uint8_t>& vPacket)
{
	return SendOn(stSocket.GetSocket(), iAddress, iPort, vPacket);
}

PeerChannel* PeerChannel::Dup(uint32_t iAddress, uint16_t iPort)
{
	if (m_pOwner != NULL || stSocket.GetSocket() == INVALID_SOCKET_ID || iPort == 0)
	{
		return NULL;
	}
	{
		::OI::Thread::MutexGuard stLock(m_stLock);
		for (PeerChannel* pChild : m_vChild)
		{
			if (pChild->m_iDestIp == iAddress)
			{
				pChild->SetDest(iAddress, iPort);
				return pChild;
			}
		}
	}
	net_socket_t iDup = NetOs_Dup(stSocket.GetSocket());
	if (iDup == INVALID_SOCKET_ID)
	{
		return NULL;
	}
	PeerChannel* pChild = new PeerChannel();
	pChild->m_pOwner = this;
	pChild->m_iLocalPort = m_iLocalPort;
	pChild->SetDest(iAddress, iPort);
	pChild->stSocket.Attach(iDup);
	::OI::Net::Poll::GetInstance().IOEventRegister(pChild, false, false);
	{
		::OI::Thread::MutexGuard stLock(m_stLock);
		m_vChild.push_back(pChild);
	}
	return pChild;
}

void PeerChannel::SetHandler(FnPacket fnPacket)
{
	m_fnPacket = std::move(fnPacket);
}

void PeerChannel::DataIn()
{
	if (m_pOwner != NULL)
	{
		return;
	}
	for (int iRound = 0; iRound < 32; ++iRound)
	{
		sockaddr_in stFrom;
		memset(&stFrom, 0, sizeof(stFrom));
		socklen_t iLength = sizeof(stFrom);
#ifdef _WIN32
		int iCount = (int)::recvfrom((SOCKET)stSocket.GetSocket(), (char*)m_vBuffer.data(), (int)m_vBuffer.size(), 0, (sockaddr*)&stFrom, &iLength);
#else
		int iCount = (int)::recvfrom((int)stSocket.GetSocket(), (char*)m_vBuffer.data(), (int)m_vBuffer.size(), 0, (sockaddr*)&stFrom, &iLength);
#endif
		if (iCount < 0)
		{
			if (IsWouldBlock())
			{
				return;
			}
			return;
		}
		if (iCount == 0)
		{
			continue;
		}
		PeerChannel* pTarget = this;
		{
			::OI::Thread::MutexGuard stLock(m_stLock);
			for (PeerChannel* pChild : m_vChild)
			{
				if (pChild->SameDest(stFrom))
				{
					pTarget = pChild;
					break;
				}
			}
		}
		if (pTarget->m_fnPacket)
		{
			pTarget->m_fnPacket(stFrom, m_vBuffer.data(), (size_t)iCount);
		}
	}
}

void PeerChannel::DataOut()
{
}

bool PeerChannel::TryClose()
{
	return false;
}

}
