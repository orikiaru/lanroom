#include "roomserver.h"
#include "oi_config.h"
#include "oi_coro.h"
#include "oi_redis_cmd.h"
#include "oi_rpcdata_heartbeatreq.h"
#include "oi_rpcdata_joinreq.h"
#include "packet.h"
#include "peer_channel.h"
#include "room.h"
#include "rpc_ids.h"
#include <stdio.h>
#include <string.h>
#include <chrono>
#include <map>
#include <string>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace
{
const int32_t kCodeNoUdp = 3;
::Lan::PeerChannel* g_pChannel = NULL;
int64_t g_iMemberTtlMs = 1000;
::OI::Thread::Mutex g_stSeenLock;

struct SeenEndpoint
{
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	sid_t iOwner = 0;
};

std::map<std::string, std::vector<SeenEndpoint>> g_mSeen;
std::map<std::string, ::Lan::RoomRecord> g_mLive;
::OI::Thread::Mutex g_stLiveLock;

bool CopyLive(const std::string& sRoom, ::Lan::RoomRecord& stRoom)
{
	::OI::Thread::MutexGuard stLock(g_stLiveLock);
	auto itLive = g_mLive.find(sRoom);
	if (itLive == g_mLive.end())
	{
		return false;
	}
	stRoom = itLive->second;
	return true;
}

void StoreLive(const std::string& sRoom, const ::Lan::RoomRecord& stRoom)
{
	::OI::Thread::MutexGuard stLock(g_stLiveLock);
	if (stRoom.vMembers.empty())
	{
		g_mLive.erase(sRoom);
		return;
	}
	g_mLive[sRoom] = stRoom;
}

int64_t NowMs()
{
	return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool MacFromString(const std::string& sMac, ::Lan::MacAddress& stMac)
{
	if (sMac.size() != ::Lan::kMacLength)
	{
		return false;
	}
	memcpy(stMac.vBytes, sMac.data(), ::Lan::kMacLength);
	return true;
}

std::string MacToString(const ::Lan::MacAddress& stMac)
{
	return std::string((const char*)stMac.vBytes, ::Lan::kMacLength);
}

std::string SeenKey(const std::string& sRoom, const ::Lan::MacAddress& stMac)
{
	return sRoom + std::string(1, '\n') + MacToString(stMac);
}

void Remember(const std::string& sRoom, const ::Lan::MacAddress& stMac, uint32_t iSourceIp, uint16_t iSourcePort)
{
	if (sRoom.empty() || iSourcePort == 0)
	{
		return;
	}
	::OI::Thread::MutexGuard stLock(g_stSeenLock);
	std::vector<SeenEndpoint>& vSeen = g_mSeen[SeenKey(sRoom, stMac)];
	for (SeenEndpoint& stSlot : vSeen)
	{
		if (stSlot.iSourceIp == iSourceIp)
		{
			stSlot.iSourcePort = iSourcePort;
			return;
		}
	}
	SeenEndpoint stSlot;
	stSlot.iSourceIp = iSourceIp;
	stSlot.iSourcePort = iSourcePort;
	vSeen.push_back(stSlot);
}

bool ClaimSeen(const std::string& sRoom, const ::Lan::MacAddress& stMac, sid_t iSessionId, SeenEndpoint& stSeen)
{
	::OI::Thread::MutexGuard stLock(g_stSeenLock);
	auto itSeen = g_mSeen.find(SeenKey(sRoom, stMac));
	if (itSeen == g_mSeen.end())
	{
		return false;
	}
	SeenEndpoint* pFree = NULL;
	for (SeenEndpoint& stSlot : itSeen->second)
	{
		if (stSlot.iOwner == iSessionId && stSlot.iSourcePort != 0)
		{
			stSeen = stSlot;
			return true;
		}
		if (stSlot.iOwner == 0 && stSlot.iSourcePort != 0 && pFree == NULL)
		{
			pFree = &stSlot;
		}
	}
	if (pFree == NULL)
	{
		return false;
	}
	pFree->iOwner = iSessionId;
	stSeen = *pFree;
	return true;
}

void ReleaseSeen(sid_t iSessionId)
{
	::OI::Thread::MutexGuard stLock(g_stSeenLock);
	for (auto& stEntry : g_mSeen)
	{
		for (SeenEndpoint& stSlot : stEntry.second)
		{
			if (stSlot.iOwner == iSessionId)
			{
				stSlot.iOwner = 0;
			}
		}
	}
}

::OI::Coro::Task LoadRoom(const std::string& sRoom, ::Lan::RoomRecord& stRoom, bool& bFound)
{
	bFound = false;
	stRoom = ::Lan::RoomRecord();
	if (CopyLive(sRoom, stRoom))
	{
		bFound = true;
		co_return;
	}
	std::string sBlob;
	const int32_t iRet = co_await ::OI::Redis::RedisGet(sRoom, sBlob);
	if (iRet == OI_ERR_NOT_FOUND)
	{
		bFound = true;
		co_return;
	}
	if (iRet != OI_OK)
	{
		co_return;
	}
	bFound = ::Lan::DecodeRoom(sBlob, stRoom);
	if (bFound)
	{
		StoreLive(sRoom, stRoom);
	}
	co_return;
}

::OI::Coro::Task SaveRoom(const std::string& sRoom, const ::Lan::RoomRecord& stRoom, bool& bSaved)
{
	bSaved = false;
	::Lan::RoomRecord stPrevious;
	if (CopyLive(sRoom, stPrevious) && ::Lan::SamePersisted(stPrevious, stRoom))
	{
		StoreLive(sRoom, stRoom);
		bSaved = true;
		co_return;
	}
	if (stRoom.vMembers.empty())
	{
		const int32_t iRet = co_await ::OI::Redis::RedisDel(sRoom);
		bSaved = iRet == OI_OK || iRet == OI_ERR_NOT_FOUND;
		if (bSaved)
		{
			StoreLive(sRoom, stRoom);
		}
		co_return;
	}
	const int32_t iRet = co_await ::OI::Redis::RedisSet(sRoom, ::Lan::EncodeRoom(stRoom));
	bSaved = iRet == OI_OK;
	if (bSaved)
	{
		StoreLive(sRoom, stRoom);
	}
	co_return;
}

template <typename Res>
void FillResponse(const ::Lan::Snapshot& stSnapshot, Res& stResponse)
{
	stResponse.iCode = stSnapshot.iCode;
	stResponse.iVirtualIp = stSnapshot.iVirtualIp;
	stResponse.vPeers.clear();
	for (const ::Lan::PeerView& stPeer : stSnapshot.vPeers)
	{
		::OI::RpcData::RoomPeer stInfo;
		stInfo.sMac = MacToString(stPeer.stMac);
		stInfo.iSourceIp = stPeer.iSourceIp;
		stInfo.iSourcePort = stPeer.iSourcePort;
		stInfo.iVirtualIp = stPeer.iVirtualIp;
		stInfo.iDirect = stPeer.bDirect ? 1 : 0;
		stResponse.vPeers.push_back(stInfo);
	}
}

template <typename Res>
::OI::Coro::Task ApplyMember(const std::string& sRoom, const ::Lan::MacAddress& stMac, bool bSaw, sid_t iSessionId, Res& stResponse)
{
	stResponse.iCode = ::Lan::kSnapshotBadRoom;
	if (sRoom.empty())
	{
		co_return;
	}
	SeenEndpoint stSeen;
	if (!ClaimSeen(sRoom, stMac, iSessionId, stSeen))
	{
		stResponse.iCode = kCodeNoUdp;
		co_return;
	}
	::Lan::RoomRecord stRoom;
	bool bLoaded = false;
	co_await LoadRoom(sRoom, stRoom, bLoaded);
	if (!bLoaded)
	{
		stResponse.iCode = ::Lan::kSnapshotBadRoom;
		co_return;
	}
	::Lan::ExpireMembers(stRoom, NowMs(), g_iMemberTtlMs);
	if (!::Lan::UpsertMember(stRoom, stSeen.iSourceIp, stSeen.iSourcePort, stMac, bSaw, NowMs()))
	{
		stResponse.iCode = ::Lan::kSnapshotBadMac;
		co_return;
	}
	bool bSaved = false;
	co_await SaveRoom(sRoom, stRoom, bSaved);
	if (!bSaved)
	{
		stResponse.iCode = ::Lan::kSnapshotBadRoom;
		co_return;
	}
	FillResponse(::Lan::SnapshotFor(stRoom, stSeen.iSourceIp, stMac), stResponse);
	co_return;
}

::OI::Rpc::RpcTask HandleJoin(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::JoinReq& stRequest, ::OI::RpcData::JoinRes& stResponse)
{
	::Lan::MacAddress stMac;
	if (!MacFromString(stRequest.sMac, stMac))
	{
		stResponse.iCode = ::Lan::kSnapshotBadMac;
		co_return 0;
	}
	co_await ApplyMember(stRequest.sRoom, stMac, false, stContext.iSessionId, stResponse);
	co_return 0;
}

::OI::Rpc::RpcTask HandleHeartbeat(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::HeartbeatReq& stRequest, ::OI::RpcData::HeartbeatRes& stResponse)
{
	::Lan::MacAddress stMac;
	if (!MacFromString(stRequest.sMac, stMac))
	{
		stResponse.iCode = ::Lan::kSnapshotBadMac;
		co_return 0;
	}
	co_await ApplyMember(stRequest.sRoom, stMac, stRequest.iSawPunch != 0, stContext.iSessionId, stResponse);
	co_return 0;
}

void OnUdp(const sockaddr_in& stFrom, const uint8_t* pData, size_t iLength)
{
	uint8_t iType = 0;
	const uint8_t* pPayload = NULL;
	size_t iPayload = 0;
	if (!::Lan::DecodePacket(pData, iLength, iType, pPayload, iPayload))
	{
		return;
	}
	uint32_t iIp = ntohl(stFrom.sin_addr.s_addr);
	uint16_t iPort = ntohs(stFrom.sin_port);
	if (iType == ::Lan::kPacketHeartbeat)
	{
		std::string sRoom;
		::Lan::MacAddress stMac;
		bool bSaw = false;
		if (::Lan::DecodeHeartbeat(pPayload, iPayload, sRoom, stMac, bSaw))
		{
			Remember(sRoom, stMac, iIp, iPort);
		}
		return;
	}
	if (iType != ::Lan::kPacketRelay || g_pChannel == NULL)
	{
		return;
	}
	std::string sRoom;
	uint32_t iDestination = 0;
	const uint8_t* pFrame = NULL;
	size_t iFrame = 0;
	if (!::Lan::DecodeRelay(pPayload, iPayload, sRoom, iDestination, pFrame, iFrame))
	{
		return;
	}
	std::vector<uint8_t> vFrame(pFrame, pFrame + iFrame);
	::OI::Coro::StartTask([sRoom, iDestination, iIp, iPort, vFrame](::OI::Coro::Token) -> ::OI::Coro::Task
	{
		::Lan::RoomRecord stRoom;
		bool bLoaded = false;
		co_await LoadRoom(sRoom, stRoom, bLoaded);
		if (!bLoaded)
		{
			co_return;
		}
		::Lan::ExpireMembers(stRoom, NowMs(), g_iMemberTtlMs);
		const ::Lan::Member* pTarget = ::Lan::FindVirtual(stRoom, iDestination);
		if (pTarget == NULL || (pTarget->iSourceIp == iIp && pTarget->iSourcePort == iPort))
		{
			co_return;
		}
		std::vector<uint8_t> vPacket;
		if (!::Lan::EncodeFrame(vFrame.data(), vFrame.size(), vPacket))
		{
			co_return;
		}
		g_pChannel->SendTo(pTarget->iSourceIp, pTarget->iSourcePort, vPacket);
		stRoom.iRelayCount += 1;
		bool bSaved = false;
		co_await SaveRoom(sRoom, stRoom, bSaved);
		(void)bSaved;
		co_return;
	});
}
}

RoomServer RoomServer::inst;

RoomServer::~RoomServer()
{
}

const std::string RoomServer::GetIDString()
{
	return "RoomServer";
}

bool RoomServer::Init()
{
	int64_t iTtl = 1000;
	::OI::Base::Config::GlobalConfig().GetAs("LanServer.member_ttl_ms", iTtl);
	g_iMemberTtlMs = iTtl;
	::OI::Rpc::HandleRpc<::OI::RpcData::JoinReq>(HandleJoin);
	::OI::Rpc::HandleRpc<::OI::RpcData::HeartbeatReq>(HandleHeartbeat);
	return ::OI::Rpc::ProtoSessionManager::Init();
}

bool RoomServer::OpenUdp(const std::string& sIp, uint16_t iPort)
{
	static ::Lan::PeerChannel stChannel;
	g_pChannel = &stChannel;
	stChannel.SetHandler(OnUdp);
	return stChannel.Open(sIp, iPort);
}

uint16_t RoomServer::UdpPort() const
{
	return g_pChannel == NULL ? 0 : g_pChannel->LocalPort();
}

void RoomServer::OnAddSession(OI::Net::Session* pSession)
{
	(void)pSession;
}

void RoomServer::OnDelSession(const OI::Net::Session* pSession)
{
	if (pSession != NULL)
	{
		ReleaseSeen(pSession->GetSessionID());
	}
}
