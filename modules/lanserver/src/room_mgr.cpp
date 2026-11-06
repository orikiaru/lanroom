#include "room_mgr.h"
#include "oi_config.h"
#include "oi_coro.h"
#include "oi_errno.h"
#include "oi_timer_helper.h"

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
	if (!::Lan::MacFrom(stProbe.sMac, stMac) || stProbe.sRoom.empty())
	{
		return OI_ERR_INVALID_ARGUMENT;
	}
	Take(stProbe.sRoom).Remember(stMac, iSourceIp, iSourcePort);
	return OI_OK;
}

::OI::Rpc::RpcTask RoomMgr::Relay(const ::OI::RpcData::RelayReq& stRelay, uint32_t iSourceIp, uint16_t iSourcePort, uint32_t& iTargetIp, uint16_t& iTargetPort)
{
	if (stRelay.sFrame.size() > ::Lan::kFrameMax || stRelay.sRoom.empty())
	{
		co_return OI_ERR_INVALID_ARGUMENT;
	}
	Room& stRoom = Take(stRelay.sRoom);
	const int32_t iLoaded = co_await stRoom.Loaded(stRelay.sRoom);
	if (iLoaded != OI_OK)
	{
		co_return iLoaded;
	}
	const ::OI::RpcData::RoomMember* pTarget = stRoom.FindVirtual(stRelay.iVirtualIp);
	if (pTarget == NULL || (pTarget->iSourceIp == iSourceIp && pTarget->iSourcePort == iSourcePort))
	{
		co_return OI_ERR_NOT_FOUND;
	}
	iTargetIp = pTarget->iSourceIp;
	iTargetPort = pTarget->iSourcePort;
	stRoom.CountRelay();
	co_return co_await stRoom.Saved();
}

::OI::Rpc::RpcTask RoomMgr::Join(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::JoinReq& stRequest, ::OI::RpcData::JoinRes& stResponse)
{
	::Lan::MacAddress stMac;
	if (!::Lan::MacFrom(stRequest.sMac, stMac) || stRequest.sRoom.empty())
	{
		co_return OI_ERR_INVALID_ARGUMENT;
	}
	Room& stRoom = Take(stRequest.sRoom);
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Claim(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
	{
		co_return OI_ERR_NOT_FOUND;
	}
	const int32_t iLoaded = co_await stRoom.Loaded(stRequest.sRoom);
	if (iLoaded != OI_OK)
	{
		co_return iLoaded;
	}
	const int32_t iJoined = stRoom.Join(iSourceIp, iSourcePort, stMac, ::OI::Timer::GetTime(), stResponse.iVirtualIp, stResponse.vPeers);
	if (iJoined != OI_OK)
	{
		co_return iJoined;
	}
	co_return co_await stRoom.Saved();
}

::OI::Rpc::RpcTask RoomMgr::Heartbeat(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::HeartbeatReq& stRequest, ::OI::RpcData::HeartbeatRes& stResponse)
{
	::Lan::MacAddress stMac;
	if (!::Lan::MacFrom(stRequest.sMac, stMac) || stRequest.sRoom.empty())
	{
		co_return OI_ERR_INVALID_ARGUMENT;
	}
	Room& stRoom = Take(stRequest.sRoom);
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Claim(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
	{
		co_return OI_ERR_NOT_FOUND;
	}
	const int32_t iLoaded = co_await stRoom.Loaded(stRequest.sRoom);
	if (iLoaded != OI_OK)
	{
		co_return iLoaded;
	}
	const int32_t iUpdated = stRoom.Update(iSourceIp, iSourcePort, stMac, stRequest.iSawPunch != 0, ::OI::Timer::GetTime(), m_iMemberTtl, stResponse.iVirtualIp, stResponse.vPeers);
	if (iUpdated != OI_OK)
	{
		co_return iUpdated;
	}
	co_return co_await stRoom.Saved();
}
