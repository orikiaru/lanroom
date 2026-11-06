#include "room_mgr.h"
#include "oi_duration.h"
#include "oi_net_os.h"
#include "oi_rpcdata_heartbeatreq.h"
#include "oi_rpcdata_heartbeatres.h"
#include "oi_rpcdata_joinreq.h"
#include "oi_rpcdata_joinres.h"
#include "oi_rpcdata_probereq.h"
#include "oi_rpcdata_relayreq.h"
#include "roomclient.h"
#include "roomudp.h"
#include "rpc_ids.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

RoomMgr::RoomMgr()
    : m_bRun(true)
{
	memset(&m_stMac, 0, sizeof(m_stMac));
}

const ::OI::RpcData::RoomPeer* RoomMgr::OtherPeer() const
{
	std::string sSelf((const char*)m_stMac.vBytes, ::Lan::kMacLength);
	for (const ::OI::RpcData::RoomPeer& stPeer : m_vPeers)
	{
		if (stPeer.sMac == sSelf && stPeer.iVirtualIp == m_iVirtualIp)
		{
			continue;
		}
		return &stPeer;
	}
	return NULL;
}

bool RoomMgr::FromServer(uint32_t iSourceIp, uint16_t iSourcePort) const
{
	return iSourceIp == m_iServerIp && iSourcePort == m_iServerPort;
}

void RoomMgr::TakeFrame(const std::string& sFrame)
{
	if (sFrame.empty() || sFrame.size() > ::Lan::kFrameMax)
	{
		return;
	}
	m_vInbound.push_back(std::vector<uint8_t>(sFrame.begin(), sFrame.end()));
}

int32_t RoomMgr::Punched(const ::OI::RpcData::PunchReq& stPunch, uint32_t iSourceIp, uint16_t iSourcePort)
{
	(void)stPunch;
	if (FromServer(iSourceIp, iSourcePort))
	{
		return OI_ERR_INVALID_ARGUMENT;
	}
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (!m_bDropDirect)
	{
		m_bSawPunch = true;
	}
	return OI_OK;
}

int32_t RoomMgr::Received(const ::OI::RpcData::FrameReq& stFrame, uint32_t iSourceIp, uint16_t iSourcePort)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (!FromServer(iSourceIp, iSourcePort) && m_bDropDirect)
	{
		return OI_OK;
	}
	TakeFrame(stFrame.sFrame);
	return OI_OK;
}

void RoomMgr::KeepSnapshot(uint32_t iVirtualIp, const std::vector<::OI::RpcData::RoomPeer>& vPeers)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	m_iVirtualIp = iVirtualIp;
	m_vPeers = vPeers;
	m_bJoined = true;
}

void RoomMgr::Probe()
{
	::OI::RpcData::ProbeReq stProbe;
	stProbe.sRoom = m_sRoom;
	stProbe.sMac.assign((const char*)m_stMac.vBytes, ::Lan::kMacLength);
	{
		::OI::Thread::MutexGuard stLock(m_stStateLock);
		stProbe.iSawPunch = m_bSawPunch ? 1 : 0;
	}
	RoomUdp::GetInstance().NotifyTo(m_iServerIp, m_iServerPort, stProbe);
}

void RoomMgr::SendFrame(const std::vector<uint8_t>& vFrame)
{
	bool bDirect = false;
	uint32_t iPeerIp = 0;
	uint16_t iPeerPort = 0;
	uint32_t iVirtualIp = 0;
	{
		::OI::Thread::MutexGuard stLock(m_stStateLock);
		const ::OI::RpcData::RoomPeer* pPeer = OtherPeer();
		if (pPeer == NULL)
		{
			return;
		}
		bDirect = pPeer->iDirect != 0 && !m_bDropDirect && m_bSawPunch && pPeer->iSourcePort != 0;
		iPeerIp = pPeer->iSourceIp;
		iPeerPort = pPeer->iSourcePort;
		iVirtualIp = pPeer->iVirtualIp;
	}
	if (bDirect)
	{
		::OI::RpcData::FrameReq stFrame;
		stFrame.sFrame.assign((const char*)vFrame.data(), vFrame.size());
		RoomUdp::GetInstance().NotifyTo(iPeerIp, iPeerPort, stFrame);
		return;
	}
	::OI::RpcData::RelayReq stRelay;
	stRelay.sRoom = m_sRoom;
	stRelay.iVirtualIp = iVirtualIp;
	stRelay.sFrame.assign((const char*)vFrame.data(), vFrame.size());
	RoomUdp::GetInstance().NotifyTo(m_iServerIp, m_iServerPort, stRelay);
}

void RoomMgr::Punch()
{
	uint32_t iPeerIp = 0;
	uint16_t iPeerPort = 0;
	{
		::OI::Thread::MutexGuard stLock(m_stStateLock);
		const ::OI::RpcData::RoomPeer* pPeer = OtherPeer();
		if (pPeer == NULL || m_bDropDirect || pPeer->iSourcePort == 0 || (m_bSawPunch && pPeer->iDirect != 0))
		{
			return;
		}
		iPeerIp = pPeer->iSourceIp;
		iPeerPort = pPeer->iSourcePort;
	}
	::OI::RpcData::PunchReq stPunch;
	stPunch.sMac.assign((const char*)m_stMac.vBytes, ::Lan::kMacLength);
	RoomUdp::GetInstance().NotifyTo(iPeerIp, iPeerPort, stPunch);
}

::OI::Coro::Task RoomMgr::Pump()
{
	std::string sMac((const char*)m_stMac.vBytes, ::Lan::kMacLength);
	while (m_bRun.load())
	{
		Probe();
		co_await ::OI::Coro::yield(100);
		bool bJoined = false;
		bool bSaw = false;
		{
			::OI::Thread::MutexGuard stLock(m_stStateLock);
			bJoined = m_bJoined;
			bSaw = m_bSawPunch;
		}
		if (!bJoined)
		{
			::OI::RpcData::JoinReq stRequest;
			stRequest.sRoom = m_sRoom;
			stRequest.sMac = sMac;
			::OI::RpcData::JoinRes stResponse;
			const int32_t iRet = co_await ::OI::Rpc::RpcCall(RoomClient::GetInstance(), stRequest, stResponse, ::OI::Timer::Seconds(3));
			if (iRet == OI_OK)
			{
				KeepSnapshot(stResponse.iVirtualIp, stResponse.vPeers);
			}
		}
		else
		{
			::OI::RpcData::HeartbeatReq stRequest;
			stRequest.sRoom = m_sRoom;
			stRequest.sMac = sMac;
			stRequest.iSawPunch = bSaw ? 1 : 0;
			::OI::RpcData::HeartbeatRes stResponse;
			const int32_t iRet = co_await ::OI::Rpc::RpcCall(RoomClient::GetInstance(), stRequest, stResponse, ::OI::Timer::Seconds(3));
			if (iRet == OI_OK)
			{
				KeepSnapshot(stResponse.iVirtualIp, stResponse.vPeers);
			}
		}
		Punch();
		for (;;)
		{
			std::vector<uint8_t> vFrame;
			{
				::OI::Thread::MutexGuard stLock(m_stStateLock);
				if (m_vOutbound.empty())
				{
					break;
				}
				vFrame = std::move(m_vOutbound.front());
				m_vOutbound.pop_front();
			}
			if (vFrame.size() > ::Lan::kFrameMax)
			{
				continue;
			}
			SendFrame(vFrame);
		}
		co_await ::OI::Coro::yield(100);
	}
	co_return;
}

void RoomMgr::WriteAll(uintptr_t iSocket, const char* pData, size_t iLength)
{
	size_t iOffset = 0;
	while (iOffset < iLength)
	{
#ifdef _WIN32
		int iSent = ::send((SOCKET)iSocket, pData + iOffset, (int)(iLength - iOffset), 0);
#else
		int iSent = (int)::send((int)iSocket, pData + iOffset, iLength - iOffset, 0);
#endif
		if (iSent <= 0)
		{
			return;
		}
		iOffset += (size_t)iSent;
	}
}

void RoomMgr::WriteText(uintptr_t iSocket, const std::string& sText)
{
	WriteAll(iSocket, sText.data(), sText.size());
}

bool RoomMgr::ReadSome(uintptr_t iSocket, std::string& sBuffer)
{
	char vTemp[512];
#ifdef _WIN32
	int iCount = ::recv((SOCKET)iSocket, vTemp, sizeof(vTemp), 0);
#else
	int iCount = (int)::recv((int)iSocket, vTemp, sizeof(vTemp), 0);
#endif
	if (iCount <= 0)
	{
		return false;
	}
	sBuffer.append(vTemp, vTemp + iCount);
	return true;
}

void RoomMgr::ServeNic()
{
	NetOs_Startup();
	uintptr_t iListen = (uintptr_t)::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	int iReuse = 1;
#ifdef _WIN32
	setsockopt((SOCKET)iListen, SOL_SOCKET, SO_REUSEADDR, (const char*)&iReuse, sizeof(iReuse));
#else
	setsockopt((int)iListen, SOL_SOCKET, SO_REUSEADDR, (const char*)&iReuse, sizeof(iReuse));
#endif
	sockaddr_in stAddress;
	memset(&stAddress, 0, sizeof(stAddress));
	stAddress.sin_family = AF_INET;
	stAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	stAddress.sin_port = htons(m_iNicPort);
#ifdef _WIN32
	if (::bind((SOCKET)iListen, (sockaddr*)&stAddress, sizeof(stAddress)) != 0 || ::listen((SOCKET)iListen, 1) != 0)
#else
	if (::bind((int)iListen, (sockaddr*)&stAddress, sizeof(stAddress)) != 0 || ::listen((int)iListen, 1) != 0)
#endif
	{
		fprintf(stderr, "fake nic listen failed\n");
		return;
	}
	printf("lanroom nic %u\n", m_iNicPort);
	fflush(stdout);
#ifdef _WIN32
	uintptr_t iClient = (uintptr_t)::accept((SOCKET)iListen, NULL, NULL);
#else
	uintptr_t iClient = (uintptr_t)::accept((int)iListen, NULL, NULL);
#endif
	std::string sBuffer;
	while (m_bRun.load())
	{
		size_t iNewline = sBuffer.find('\n');
		while (iNewline == std::string::npos)
		{
			if (!ReadSome(iClient, sBuffer))
			{
				m_bRun.store(false);
				return;
			}
			iNewline = sBuffer.find('\n');
		}
		std::string sLine = sBuffer.substr(0, iNewline);
		sBuffer.erase(0, iNewline + 1);
		if (sLine == "PORT")
		{
			WriteText(iClient, "port " + std::to_string(RoomUdp::GetInstance().LocalPort()) + "\n");
		}
		else if (sLine == "DROP 1")
		{
			::OI::Thread::MutexGuard stLock(m_stStateLock);
			m_bDropDirect = true;
			m_bSawPunch = false;
			WriteText(iClient, "ok\n");
		}
		else if (sLine == "DROP 0")
		{
			::OI::Thread::MutexGuard stLock(m_stStateLock);
			m_bDropDirect = false;
			WriteText(iClient, "ok\n");
		}
		else if (sLine == "VIRT")
		{
			::OI::Thread::MutexGuard stLock(m_stStateLock);
			WriteText(iClient, "virt " + ::Lan::FormatIpv4(m_iVirtualIp) + "\n");
		}
		else if (sLine == "PEERS")
		{
			::OI::Thread::MutexGuard stLock(m_stStateLock);
			WriteText(iClient, "peers " + std::to_string(m_vPeers.size()) + "\n");
			for (const ::OI::RpcData::RoomPeer& stPeer : m_vPeers)
			{
				char vMac[32] = "invalid";
				if (stPeer.sMac.size() == ::Lan::kMacLength)
				{
					const uint8_t* pMac = (const uint8_t*)stPeer.sMac.data();
					snprintf(vMac, sizeof(vMac), "%02x:%02x:%02x:%02x:%02x:%02x", pMac[0], pMac[1], pMac[2], pMac[3], pMac[4], pMac[5]);
				}
				WriteText(iClient, std::string("peer ") + vMac + " " + ::Lan::FormatIpv4(stPeer.iSourceIp) + " " + std::to_string(stPeer.iSourcePort) + " " + ::Lan::FormatIpv4(stPeer.iVirtualIp) + " " + (stPeer.iDirect != 0 ? "1" : "0") + "\n");
			}
		}
		else if (sLine.compare(0, 6, "WRITE ") == 0)
		{
			size_t iNeed = (size_t)atoi(sLine.c_str() + 6);
			while (sBuffer.size() < iNeed)
			{
				if (!ReadSome(iClient, sBuffer))
				{
					m_bRun.store(false);
					return;
				}
			}
			std::vector<uint8_t> vFrame(sBuffer.begin(), sBuffer.begin() + (std::ptrdiff_t)iNeed);
			sBuffer.erase(0, iNeed);
			if (iNeed == 0 || iNeed > ::Lan::kFrameMax)
			{
				WriteText(iClient, "fail\n");
				continue;
			}
			{
				::OI::Thread::MutexGuard stLock(m_stStateLock);
				m_vOutbound.push_back(std::move(vFrame));
			}
			WriteText(iClient, "ok\n");
		}
		else if (sLine == "READ")
		{
			std::vector<uint8_t> vFrame;
			bool bHave = false;
			for (int iWait = 0; iWait < 40 && !bHave; ++iWait)
			{
				{
					::OI::Thread::MutexGuard stLock(m_stStateLock);
					if (!m_vInbound.empty())
					{
						vFrame = std::move(m_vInbound.front());
						m_vInbound.pop_front();
						bHave = true;
					}
				}
				if (!bHave)
				{
#ifdef _WIN32
					Sleep(50);
#else
					usleep(50 * 1000);
#endif
				}
			}
			if (!bHave)
			{
				WriteText(iClient, "empty\n");
				continue;
			}
			WriteText(iClient, "frame " + std::to_string(vFrame.size()) + "\n");
			WriteAll(iClient, (const char*)vFrame.data(), vFrame.size());
		}
		else
		{
			WriteText(iClient, "fail\n");
		}
	}
}

bool RoomMgr::Prepare(const std::string& sRoom, const std::string& sMac, const std::string& sUdpServer, uint16_t iUdpPort, uint16_t iNicPort, bool bDrop)
{
	if (sRoom.empty() || !::Lan::ParseMac(sMac, m_stMac) || iNicPort == 0)
	{
		return false;
	}
	m_sRoom = sRoom;
	m_iNicPort = iNicPort;
	m_bDropDirect = bDrop;
	m_iServerPort = iUdpPort;
	if (inet_pton(AF_INET, sUdpServer.c_str(), &m_iServerIp) != 1)
	{
		return false;
	}
	m_iServerIp = ntohl(m_iServerIp);
	return true;
}

void RoomMgr::Start()
{
	m_stNic = std::thread(&RoomMgr::ServeNic, this);
	::OI::Coro::StartTask([this](::OI::Coro::Token) -> ::OI::Coro::Task
	{
		co_await Pump();
	});
}

void RoomMgr::Stop()
{
	m_bRun.store(false);
	if (m_stNic.joinable())
	{
		m_stNic.join();
	}
}
