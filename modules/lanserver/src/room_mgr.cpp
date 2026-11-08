#include "room_mgr.h"
#include "oi_config.h"
#include "oi_coro.h"
#include "oi_duration.h"
#include "oi_errno.h"
#include "oi_log.h"
#include "oi_rpcdata_roomcastreq.h"
#include "oi_timer_helper.h"
#include "roomserver.h"
#include "rpc_ids.h"

struct RoomBusy
{
	Room& stRoom;
	explicit RoomBusy(Room& stRoomIn) : stRoom(stRoomIn) {}
	~RoomBusy() { stRoom.ReleaseBusy(); }
};

RoomMgr::RoomMgr()
{
}

void RoomMgr::Init()
{
	int64_t iTtl = 1;
	::OI::Base::Config::GlobalConfig().GetAs("LanServer.member_ttl", iTtl);
	m_iMemberTtl = (time_t)iTtl;
}

Room& RoomMgr::Take(const std::string& sRoom)
{
	return m_mRoom[sRoom];
}

void RoomMgr::ReleaseSession(sid_t iSessionId)
{
	for (auto& stEntry : m_mRoom)
	{
		stEntry.second.Release(iSessionId);
	}
}

int32_t RoomMgr::Probe(const ::OI::RpcData::ProbeReq& stProbe, uint32_t iSourceIp, uint16_t iSourcePort)
{
	::Lan::MacAddress stMac;
	if (!stMac.From(stProbe.sMac) || stProbe.sRoom.empty())
	{
		LOG_WARNING("RoomMgr::Probe reject src=%s:%u", ::Lan::Ipv4::Text(iSourceIp).c_str(), iSourcePort);
		return OI_ERR_INVALID_ARGUMENT;
	}
	Take(stProbe.sRoom).Remember(stMac, iSourceIp, iSourcePort);
	return OI_OK;
}

::OI::Rpc::RpcTask RoomMgr::Join(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::JoinReq& stRequest, ::OI::RpcData::JoinRes& stResponse)
{
	::Lan::MacAddress stMac;
	if (!stMac.From(stRequest.sMac) || stRequest.sRoom.empty())
	{
		LOG_WARNING("RoomMgr::Join reject room=%s", stRequest.sRoom.c_str());
		co_return OI_ERR_INVALID_ARGUMENT;
	}
	Room& stRoom = Take(stRequest.sRoom);
	co_await stRoom.Occupy();
	RoomBusy stBusy(stRoom);
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Claim(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
	{
		LOG_WARNING("RoomMgr::Join reject room=%s mac=%s sid=%d reason=no-probe", stRequest.sRoom.c_str(), stMac.Colon().c_str(), stContext.iSessionId);
		co_return OI_ERR_NOT_FOUND;
	}
	const int32_t iLoaded = co_await stRoom.Loaded(stRequest.sRoom);
	if (iLoaded != OI_OK)
	{
		LOG_WARNING("RoomMgr::Join load room=%s code=%d", stRequest.sRoom.c_str(), iLoaded);
		co_return iLoaded;
	}
	const int32_t iJoined = stRoom.Join(iSourceIp, iSourcePort, stMac, stRequest.stAttr.iTcpRelay != 0, ::OI::Timer::GetTime(), stResponse.iVirtualIp, stResponse.vPeers);
	if (iJoined != OI_OK)
	{
		LOG_WARNING("RoomMgr::Join fail room=%s mac=%s src=%s:%u code=%d", stRequest.sRoom.c_str(), stMac.Colon().c_str(), ::Lan::Ipv4::Text(iSourceIp).c_str(), iSourcePort, iJoined);
		co_return iJoined;
	}
	LOG_INFO("RoomMgr::Join room=%s mac=%s src=%s:%u virt=%s tcp=%u members=%zu version=%u", stRequest.sRoom.c_str(), stMac.Colon().c_str(), ::Lan::Ipv4::Text(iSourceIp).c_str(), iSourcePort, ::Lan::Ipv4::Text(stResponse.iVirtualIp).c_str(), stRoom.TcpRelay(), stResponse.vPeers.size(), stRoom.Attr().iVersion);
	stResponse.stAttr = stRoom.Attr();
	std::vector<sid_t> vSessions;
	std::vector<::OI::RpcData::RoomPeer> vPeers;
	::OI::RpcData::RoomAttr stAttr = stRoom.Attr();
	stRoom.CollectSessions(stContext.iSessionId, vSessions);
	stRoom.CopyPeers(vPeers);
	const int32_t iSaved = co_await stRoom.Saved();
	if (iSaved != OI_OK)
	{
		co_return iSaved;
	}
	Broadcast(stRequest.sRoom, vSessions, vPeers, stAttr);
	co_return OI_OK;
}

::OI::Rpc::RpcTask RoomMgr::Heartbeat(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::HeartbeatReq& stRequest, ::OI::RpcData::HeartbeatRes& stResponse)
{
	::Lan::MacAddress stMac;
	if (!stMac.From(stRequest.sMac) || stRequest.sRoom.empty())
	{
		LOG_WARNING("RoomMgr::Heartbeat reject room=%s", stRequest.sRoom.c_str());
		co_return OI_ERR_INVALID_ARGUMENT;
	}
	Room& stRoom = Take(stRequest.sRoom);
	co_await stRoom.Occupy();
	RoomBusy stBusy(stRoom);
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Claim(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
	{
		LOG_WARNING("RoomMgr::Heartbeat reject room=%s mac=%s sid=%d reason=no-probe", stRequest.sRoom.c_str(), stMac.Colon().c_str(), stContext.iSessionId);
		co_return OI_ERR_NOT_FOUND;
	}
	const int32_t iLoaded = co_await stRoom.Loaded(stRequest.sRoom);
	if (iLoaded != OI_OK)
	{
		LOG_WARNING("RoomMgr::Heartbeat load room=%s code=%d", stRequest.sRoom.c_str(), iLoaded);
		co_return iLoaded;
	}
	const int32_t iUpdated = stRoom.Update(iSourceIp, iSourcePort, stMac, stRequest.iSawPunch != 0, ::OI::Timer::GetTime(), m_iMemberTtl, stResponse.iVirtualIp, stResponse.vPeers);
	if (iUpdated != OI_OK)
	{
		LOG_WARNING("RoomMgr::Heartbeat fail room=%s mac=%s src=%s:%u code=%d", stRequest.sRoom.c_str(), stMac.Colon().c_str(), ::Lan::Ipv4::Text(iSourceIp).c_str(), iSourcePort, iUpdated);
		co_return iUpdated;
	}
	stResponse.stAttr = stRoom.Attr();
	co_return co_await stRoom.Saved();
}

::OI::Rpc::RpcTask RoomMgr::Leave(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::LeaveReq& stRequest, ::OI::RpcData::LeaveRes& stResponse)
{
	::Lan::MacAddress stMac;
	if (!stMac.From(stRequest.sMac) || stRequest.sRoom.empty())
	{
		LOG_WARNING("RoomMgr::Leave reject room=%s", stRequest.sRoom.c_str());
		co_return OI_ERR_INVALID_ARGUMENT;
	}
	Room& stRoom = Take(stRequest.sRoom);
	co_await stRoom.Occupy();
	RoomBusy stBusy(stRoom);
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Claim(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
	{
		LOG_WARNING("RoomMgr::Leave reject room=%s mac=%s sid=%d reason=no-probe", stRequest.sRoom.c_str(), stMac.Colon().c_str(), stContext.iSessionId);
		co_return OI_ERR_NOT_FOUND;
	}
	const int32_t iLoaded = co_await stRoom.Loaded(stRequest.sRoom);
	if (iLoaded != OI_OK)
	{
		LOG_WARNING("RoomMgr::Leave load room=%s code=%d", stRequest.sRoom.c_str(), iLoaded);
		co_return iLoaded;
	}
	if (!stRoom.Remove(iSourceIp, stMac))
	{
		stResponse.iLeft = 0;
		LOG_INFO("RoomMgr::Leave absent room=%s mac=%s", stRequest.sRoom.c_str(), stMac.Colon().c_str());
		co_return OI_OK;
	}
	stResponse.iLeft = 1;
	std::vector<sid_t> vSessions;
	std::vector<::OI::RpcData::RoomPeer> vPeers;
	::OI::RpcData::RoomAttr stAttr = stRoom.Attr();
	stRoom.CollectSessions(stContext.iSessionId, vSessions);
	stRoom.CopyPeers(vPeers);
	const int32_t iSaved = co_await stRoom.Saved();
	if (iSaved != OI_OK)
	{
		co_return iSaved;
	}
	LOG_INFO("RoomMgr::Leave room=%s mac=%s src=%s:%u members=%zu version=%u", stRequest.sRoom.c_str(), stMac.Colon().c_str(), ::Lan::Ipv4::Text(iSourceIp).c_str(), iSourcePort, vPeers.size(), stAttr.iVersion);
	Broadcast(stRequest.sRoom, vSessions, vPeers, stAttr);
	co_return OI_OK;
}

void RoomMgr::Broadcast(const std::string& sRoom, const std::vector<sid_t>& vSessions, const std::vector<::OI::RpcData::RoomPeer>& vPeers, const ::OI::RpcData::RoomAttr& stAttr)
{
	LOG_INFO("RoomMgr::Broadcast room=%s sessions=%zu members=%zu tcp=%u version=%u", sRoom.c_str(), vSessions.size(), vPeers.size(), stAttr.iTcpRelay, stAttr.iVersion);
	if (vSessions.empty())
	{
		return;
	}
	::OI::RpcData::RoomCastReq stCast;
	stCast.sRoom = sRoom;
	stCast.vPeers = vPeers;
	stCast.stAttr = stAttr;
	for (sid_t iSessionId : vSessions)
	{
		RoomServer::GetInstance().Notify(iSessionId, stCast);
	}
}

::OI::Rpc::RpcTask RoomMgr::Carry(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::CarryReq& stCarry)
{
	const int64_t iBegin = ::OI::Timer::SteadyMilliseconds();
	::Lan::MacAddress stMac;
	if (!stMac.From(stCarry.sMac) || stCarry.sRoom.empty() || stCarry.sFrame.size() > ::Lan::kFrameMax)
	{
		LOG_WARNING("RoomMgr::Carry reject room=%s bytes=%zu", stCarry.sRoom.c_str(), stCarry.sFrame.size());
		co_return OI_ERR_INVALID_ARGUMENT;
	}
	Room& stRoom = Take(stCarry.sRoom);
	if (!stRoom.Ready())
	{
		co_await stRoom.Occupy();
		RoomBusy stBusy(stRoom);
		if (!stRoom.Ready())
		{
			const int32_t iLoaded = co_await stRoom.Loaded(stCarry.sRoom);
			if (iLoaded != OI_OK)
			{
				LOG_WARNING("RoomMgr::Carry load room=%s code=%d", stCarry.sRoom.c_str(), iLoaded);
				co_return iLoaded;
			}
		}
	}
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Claim(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
	{
		LOG_WARNING("RoomMgr::Carry reject room=%s mac=%s sid=%d reason=no-probe", stCarry.sRoom.c_str(), stMac.Colon().c_str(), stContext.iSessionId);
		co_return OI_ERR_NOT_FOUND;
	}
	std::vector<sid_t> vSessions;
	stRoom.CollectTargets(stContext.iSessionId, stCarry.iVirtualIp, vSessions);
	if (vSessions.empty())
	{
		LOG_WARNING("RoomMgr::Carry miss room=%s virt=%s src=%s:%u", stCarry.sRoom.c_str(), ::Lan::Ipv4::Text(stCarry.iVirtualIp).c_str(), ::Lan::Ipv4::Text(iSourceIp).c_str(), iSourcePort);
		co_return OI_ERR_NOT_FOUND;
	}
	stRoom.CountRelay();
	int32_t iRet = OI_OK;
	for (sid_t iSessionId : vSessions)
	{
		const int32_t iSent = RoomServer::GetInstance().Notify(iSessionId, stCarry);
		if (iSent != OI_OK)
		{
			iRet = iSent;
		}
	}
	const int64_t iEnd = ::OI::Timer::SteadyMilliseconds();
	LOG_INFO("RoomMgr::Carry room=%s mac=%s dst=%s bytes=%zu targets=%zu code=%d begin=%lld end=%lld cost=%lldms", stCarry.sRoom.c_str(), stMac.Colon().c_str(), ::Lan::Ipv4::Text(stCarry.iVirtualIp).c_str(), stCarry.sFrame.size(), vSessions.size(), iRet, (long long)iBegin, (long long)iEnd, (long long)(iEnd - iBegin));
	co_return iRet;
}
