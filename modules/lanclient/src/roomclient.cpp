#include "roomclient.h"
#include "oi_coro.h"
#include "oi_duration.h"
#include "oi_net_os.h"
#include "oi_rpcdata_heartbeatreq.h"
#include "oi_rpcdata_joinreq.h"
#include "packet.h"
#include "peer_channel.h"
#include "rpc_ids.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <deque>
#include <string>
#include <thread>
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

namespace
{
const int32_t kCodeNoUdp = 3;
::Lan::PeerChannel* g_pServer = NULL;
::Lan::PeerChannel* g_pDirect = NULL;
::OI::Thread::Mutex g_stStateLock;
std::string g_sRoom;
::Lan::MacAddress g_stMac;
uint32_t g_iServerIp = 0;
uint16_t g_iServerPort = 0;
uint16_t g_iNicPort = 0;
bool g_bJoined = false;
bool g_bSawPunch = false;
bool g_bDropDirect = false;
::Lan::Snapshot g_stSnapshot;
std::deque<std::vector<uint8_t>> g_vInbound;
std::deque<std::vector<uint8_t>> g_vOutbound;
std::atomic<bool> g_bRun(true);
std::thread g_stNic;

bool IsServer(const sockaddr_in& stFrom)
{
	return ntohl(stFrom.sin_addr.s_addr) == g_iServerIp && ntohs(stFrom.sin_port) == g_iServerPort;
}

const ::Lan::PeerView* OtherPeer()
{
	for (const ::Lan::PeerView& stPeer : g_stSnapshot.vPeers)
	{
		if (::Lan::MacEqual(stPeer.stMac, g_stMac) && stPeer.iVirtualIp == g_stSnapshot.iVirtualIp)
		{
			continue;
		}
		return &stPeer;
	}
	return NULL;
}

void TakeFrame(const uint8_t* pPayload, size_t iPayload)
{
	const uint8_t* pFrame = NULL;
	size_t iFrame = 0;
	if (::Lan::DecodeFrame(pPayload, iPayload, pFrame, iFrame))
	{
		g_vInbound.push_back(std::vector<uint8_t>(pFrame, pFrame + iFrame));
	}
}

void OnDirect(const sockaddr_in& stFrom, const uint8_t* pData, size_t iLength)
{
	(void)stFrom;
	uint8_t iType = 0;
	const uint8_t* pPayload = NULL;
	size_t iPayload = 0;
	if (!::Lan::DecodePacket(pData, iLength, iType, pPayload, iPayload))
	{
		return;
	}
	::OI::Thread::MutexGuard stLock(g_stStateLock);
	if (g_bDropDirect)
	{
		return;
	}
	if (iType == ::Lan::kPacketPunch)
	{
		g_bSawPunch = true;
		return;
	}
	if (iType == ::Lan::kPacketFrame)
	{
		TakeFrame(pPayload, iPayload);
	}
}

void OnServer(const sockaddr_in& stFrom, const uint8_t* pData, size_t iLength)
{
	if (!IsServer(stFrom))
	{
		OnDirect(stFrom, pData, iLength);
		return;
	}
	uint8_t iType = 0;
	const uint8_t* pPayload = NULL;
	size_t iPayload = 0;
	if (!::Lan::DecodePacket(pData, iLength, iType, pPayload, iPayload))
	{
		return;
	}
	if (iType != ::Lan::kPacketFrame)
	{
		return;
	}
	::OI::Thread::MutexGuard stLock(g_stStateLock);
	TakeFrame(pPayload, iPayload);
}

template <typename Res>
void KeepSnapshot(const Res& stResponse)
{
	::Lan::Snapshot stSnapshot;
	stSnapshot.iCode = stResponse.iCode;
	stSnapshot.iVirtualIp = stResponse.iVirtualIp;
	for (const ::OI::RpcData::RoomPeer& stInfo : stResponse.vPeers)
	{
		if (stInfo.sMac.size() != ::Lan::kMacLength)
		{
			continue;
		}
		::Lan::PeerView stPeer;
		memcpy(stPeer.stMac.vBytes, stInfo.sMac.data(), ::Lan::kMacLength);
		stPeer.iSourceIp = stInfo.iSourceIp;
		stPeer.iSourcePort = stInfo.iSourcePort;
		stPeer.iVirtualIp = stInfo.iVirtualIp;
		stPeer.bDirect = stInfo.iDirect != 0;
		stSnapshot.vPeers.push_back(stPeer);
	}
	::OI::Thread::MutexGuard stLock(g_stStateLock);
	g_stSnapshot = stSnapshot;
	g_bJoined = stResponse.iCode == ::Lan::kSnapshotOk;
}

void SendFrame(const std::vector<uint8_t>& vFrame)
{
	std::vector<uint8_t> vPacket;
	::OI::Thread::MutexGuard stLock(g_stStateLock);
	const ::Lan::PeerView* pPeer = OtherPeer();
	bool bDirect = pPeer != NULL && pPeer->bDirect && !g_bDropDirect && g_bSawPunch && g_pDirect != NULL;
	if (bDirect)
	{
		if (::Lan::EncodeFrame(vFrame.data(), vFrame.size(), vPacket))
		{
			g_pDirect->Send(vPacket);
		}
		return;
	}
	if (pPeer == NULL || g_pServer == NULL)
	{
		return;
	}
	if (::Lan::EncodeRelay(g_sRoom, pPeer->iVirtualIp, vFrame.data(), vFrame.size(), vPacket))
	{
		g_pServer->Send(vPacket);
	}
}

void Punch()
{
	std::vector<uint8_t> vPacket;
	::OI::Thread::MutexGuard stLock(g_stStateLock);
	const ::Lan::PeerView* pPeer = OtherPeer();
	if (pPeer == NULL || g_bDropDirect || g_pDirect == NULL || (g_bSawPunch && pPeer->bDirect))
	{
		return;
	}
	if (::Lan::EncodePunch(g_stMac, vPacket))
	{
		g_pDirect->Send(vPacket);
	}
}

void ArmDirect()
{
	const ::Lan::PeerView* pPeer = NULL;
	{
		::OI::Thread::MutexGuard stLock(g_stStateLock);
		pPeer = OtherPeer();
		if (pPeer == NULL || pPeer->iSourcePort == 0 || g_pServer == NULL)
		{
			return;
		}
		if (g_pDirect == NULL)
		{
			g_pDirect = g_pServer->Dup(pPeer->iSourceIp, pPeer->iSourcePort);
			if (g_pDirect != NULL)
			{
				g_pDirect->SetHandler(OnDirect);
			}
		}
		else
		{
			g_pDirect->SetDest(pPeer->iSourceIp, pPeer->iSourcePort);
		}
	}
}

::OI::Coro::Task Pump()
{
	std::string sMac((const char*)g_stMac.vBytes, ::Lan::kMacLength);
	while (g_bRun.load())
	{
		std::vector<uint8_t> vProbe;
		bool bSaw = false;
		{
			::OI::Thread::MutexGuard stLock(g_stStateLock);
			bSaw = g_bSawPunch;
		}
		if (::Lan::EncodeHeartbeat(g_sRoom, g_stMac, bSaw, vProbe) && g_pServer != NULL)
		{
			g_pServer->Send(vProbe);
		}
		co_await ::OI::Coro::yield(100);
		bool bJoined = false;
		{
			::OI::Thread::MutexGuard stLock(g_stStateLock);
			bJoined = g_bJoined;
			bSaw = g_bSawPunch;
		}
		if (!bJoined)
		{
			::OI::RpcData::JoinReq stRequest;
			stRequest.sRoom = g_sRoom;
			stRequest.sMac = sMac;
			::OI::RpcData::JoinRes stResponse;
			const int32_t iRet = co_await ::OI::Rpc::RpcCall(RoomClient::GetInstance(), stRequest, stResponse, ::OI::Timer::Seconds(3));
			if (iRet == OI_OK && stResponse.iCode != kCodeNoUdp)
			{
				KeepSnapshot(stResponse);
			}
		}
		else
		{
			::OI::RpcData::HeartbeatReq stRequest;
			stRequest.sRoom = g_sRoom;
			stRequest.sMac = sMac;
			stRequest.iSawPunch = bSaw ? 1 : 0;
			::OI::RpcData::HeartbeatRes stResponse;
			const int32_t iRet = co_await ::OI::Rpc::RpcCall(RoomClient::GetInstance(), stRequest, stResponse, ::OI::Timer::Seconds(3));
			if (iRet == OI_OK && stResponse.iCode != kCodeNoUdp)
			{
				KeepSnapshot(stResponse);
			}
		}
		ArmDirect();
		Punch();
		for (;;)
		{
			std::vector<uint8_t> vFrame;
			{
				::OI::Thread::MutexGuard stLock(g_stStateLock);
				if (g_vOutbound.empty())
				{
					break;
				}
				vFrame = std::move(g_vOutbound.front());
				g_vOutbound.pop_front();
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

void WriteAll(uintptr_t iSocket, const char* pData, size_t iLength)
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

void WriteText(uintptr_t iSocket, const std::string& sText)
{
	WriteAll(iSocket, sText.data(), sText.size());
}

bool ReadSome(uintptr_t iSocket, std::string& sBuffer)
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

void ServeNic(uint16_t iPort)
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
	stAddress.sin_port = htons(iPort);
#ifdef _WIN32
	if (::bind((SOCKET)iListen, (sockaddr*)&stAddress, sizeof(stAddress)) != 0 || ::listen((SOCKET)iListen, 1) != 0)
#else
	if (::bind((int)iListen, (sockaddr*)&stAddress, sizeof(stAddress)) != 0 || ::listen((int)iListen, 1) != 0)
#endif
	{
		fprintf(stderr, "fake nic listen failed\n");
		return;
	}
	printf("lanroom nic %u\n", iPort);
	fflush(stdout);
#ifdef _WIN32
	uintptr_t iClient = (uintptr_t)::accept((SOCKET)iListen, NULL, NULL);
#else
	uintptr_t iClient = (uintptr_t)::accept((int)iListen, NULL, NULL);
#endif
	std::string sBuffer;
	while (g_bRun.load())
	{
		size_t iNewline = sBuffer.find('\n');
		while (iNewline == std::string::npos)
		{
			if (!ReadSome(iClient, sBuffer))
			{
				g_bRun.store(false);
				return;
			}
			iNewline = sBuffer.find('\n');
		}
		std::string sLine = sBuffer.substr(0, iNewline);
		sBuffer.erase(0, iNewline + 1);
		if (sLine == "PORT")
		{
			WriteText(iClient, "port " + std::to_string(g_pServer->LocalPort()) + "\n");
		}
		else if (sLine == "DROP 1")
		{
			::OI::Thread::MutexGuard stLock(g_stStateLock);
			g_bDropDirect = true;
			g_bSawPunch = false;
			WriteText(iClient, "ok\n");
		}
		else if (sLine == "DROP 0")
		{
			::OI::Thread::MutexGuard stLock(g_stStateLock);
			g_bDropDirect = false;
			WriteText(iClient, "ok\n");
		}
		else if (sLine == "VIRT")
		{
			::OI::Thread::MutexGuard stLock(g_stStateLock);
			WriteText(iClient, "virt " + ::Lan::FormatIpv4(g_stSnapshot.iVirtualIp) + "\n");
		}
		else if (sLine == "PEERS")
		{
			::OI::Thread::MutexGuard stLock(g_stStateLock);
			WriteText(iClient, "peers " + std::to_string(g_stSnapshot.vPeers.size()) + "\n");
			for (const ::Lan::PeerView& stPeer : g_stSnapshot.vPeers)
			{
				char vMac[32];
				snprintf(vMac, sizeof(vMac), "%02x:%02x:%02x:%02x:%02x:%02x", stPeer.stMac.vBytes[0], stPeer.stMac.vBytes[1], stPeer.stMac.vBytes[2], stPeer.stMac.vBytes[3], stPeer.stMac.vBytes[4], stPeer.stMac.vBytes[5]);
				WriteText(iClient, std::string("peer ") + vMac + " " + ::Lan::FormatIpv4(stPeer.iSourceIp) + " " + std::to_string(stPeer.iSourcePort) + " " + ::Lan::FormatIpv4(stPeer.iVirtualIp) + " " + (stPeer.bDirect ? "1" : "0") + "\n");
			}
		}
		else if (sLine.compare(0, 6, "WRITE ") == 0)
		{
			size_t iNeed = (size_t)atoi(sLine.c_str() + 6);
			while (sBuffer.size() < iNeed)
			{
				if (!ReadSome(iClient, sBuffer))
				{
					g_bRun.store(false);
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
				::OI::Thread::MutexGuard stLock(g_stStateLock);
				g_vOutbound.push_back(std::move(vFrame));
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
					::OI::Thread::MutexGuard stLock(g_stStateLock);
					if (!g_vInbound.empty())
					{
						vFrame = std::move(g_vInbound.front());
						g_vInbound.pop_front();
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
}

RoomClient RoomClient::inst;

RoomClient::~RoomClient()
{
}

const std::string RoomClient::GetIDString()
{
	return "RoomClient";
}

bool RoomClient::Init()
{
	return ::OI::Rpc::ProtoClientManager::Init();
}

bool RoomClient::Prepare(const std::string& sRoom, const std::string& sMac, const std::string& sBind, const std::string& sUdpServer, uint16_t iUdpPort, uint16_t iNicPort, bool bDrop)
{
	if (sRoom.empty() || !::Lan::ParseMac(sMac, g_stMac) || iNicPort == 0)
	{
		return false;
	}
	g_sRoom = sRoom;
	g_iNicPort = iNicPort;
	g_bDropDirect = bDrop;
	g_iServerPort = iUdpPort;
	if (inet_pton(AF_INET, sUdpServer.c_str(), &g_iServerIp) != 1)
	{
		return false;
	}
	g_iServerIp = ntohl(g_iServerIp);
	static ::Lan::PeerChannel stChannel;
	g_pServer = &stChannel;
	stChannel.SetHandler(OnServer);
	stChannel.SetDest(g_iServerIp, g_iServerPort);
	if (!stChannel.Open(sBind, 0))
	{
		return false;
	}
	printf("lanroom client port %u\n", stChannel.LocalPort());
	fflush(stdout);
	return true;
}

void RoomClient::StartWork()
{
	g_stNic = std::thread(ServeNic, g_iNicPort);
	::OI::Coro::StartTask([](::OI::Coro::Token) -> ::OI::Coro::Task
	{
		co_await Pump();
	});
}

void RoomClient::JoinWork()
{
	g_bRun.store(false);
	if (g_stNic.joinable())
	{
		g_stNic.join();
	}
}

void RoomClient::OnClientConnect(OI::Net::Session* pSession)
{
	(void)pSession;
}

void RoomClient::OnClientDisconnect(const OI::Net::Session* pSession)
{
	(void)pSession;
}
