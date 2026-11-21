#include "room_mgr.h"
#include "oi_config.h"
#include "oi_coro.h"
#include "oi_duration.h"
#include "oi_errno.h"
#include "oi_log.h"
#include "oi_redis_cmd.h"
#include "oi_rpcdata_roomcastreq.h"
#include "oi_timer_helper.h"
#include "room_auth.h"
#include "roomserver.h"
#include "rpc_ids.h"

RoomMgr::RoomMgr()
{
}

void RoomMgr::Init()
{
	int64_t iTtl = 1;
	::OI::Base::Config::GlobalConfig().GetAs("LanServer.member_ttl", iTtl);
	m_iMemberTtl = (time_t)iTtl;
	::OI::Base::Config::GlobalConfig().GetAs("LanServer.auth_secret", m_sAuthSecret);
}

std::shared_ptr<Room> RoomMgr::Take(const std::string& sRoom)
{
	std::shared_ptr<Room>& pRoom = m_mRoom[sRoom];
	if (!pRoom)
	{
		pRoom = std::make_shared<Room>();
	}
	return pRoom;
}

::OI::Coro::TaskResult<std::shared_ptr<Room>> RoomMgr::Load(const std::string& sRoom)
{
	std::shared_ptr<Room> pRoom = Take(sRoom);
	if (!pRoom->Ready())
	{
		const int32_t iLoaded = co_await pRoom->Loaded(sRoom);
		if (iLoaded != OI_OK)
		{
			LOG_WARNING("RoomMgr::Load room=%s code=%d", sRoom.c_str(), iLoaded);
			co_return std::shared_ptr<Room>();
		}
	}
	co_return pRoom;
}

::OI::Coro::TaskResult<int32_t> RoomMgr::Authorize(const std::string& sRoom, const std::string& sToken)
{
	if (m_sAuthSecret.empty())
	{
		co_return OI_OK;
	}
	std::string sOwner;
	if (!TicketOwner(m_sAuthSecret, sRoom, sToken, sOwner))
	{
		LOG_WARNING("RoomMgr::Authorize reject room=%s reason=ticket", sRoom.c_str());
		co_return OI_ERR_NOT_FOUND;
	}
	std::string sStored;
	const int32_t iRead = co_await ::OI::Redis::RedisGet(std::string("lanroom:room:") + sRoom, sStored);
	if (iRead != OI_OK || sStored != sOwner)
	{
		LOG_WARNING("RoomMgr::Authorize reject room=%s reason=room", sRoom.c_str());
		co_return OI_ERR_NOT_FOUND;
	}
	co_return OI_OK;
}

::OI::Coro::TaskResult<bool> RoomMgr::Kicked(const std::string& sRoom, const std::string& sMac)
{
	if (m_sAuthSecret.empty())
	{
		co_return false;
	}
	std::string sFlag;
	const int32_t iRead = co_await ::OI::Redis::RedisGet(std::string("lanroom:kick:") + sRoom + ":" + sMac, sFlag);
	co_return iRead == OI_OK;
}

::OI::Coro::Task RoomMgr::PublishLive(const Room& stRoom)
{
	const std::string sKey = std::string("lanroom:live:") + stRoom.Name();
	const std::string sText = stRoom.LiveText();
	if (sText.empty())
	{
		co_await ::OI::Redis::RedisDel(sKey);
		co_return;
	}
	const int32_t iSaved = co_await ::OI::Redis::RedisSet(sKey, sText, ::OI::Timer::Seconds(m_iMemberTtl));
	if (iSaved != OI_OK)
	{
		LOG_WARNING("RoomMgr::PublishLive room=%s code=%d", stRoom.Name().c_str(), iSaved);
	}
	co_return;
}

void RoomMgr::ReleaseSession(sid_t iSessionId)
{
	for (auto& stEntry : m_mRoom)
	{
		stEntry.second->Release(iSessionId);
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
	Take(stProbe.sRoom)->Remember(stMac, iSourceIp, iSourcePort);
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
	const int32_t iAllowed = co_await Authorize(stRequest.sRoom, stRequest.sToken);
	if (iAllowed != OI_OK)
	{
		co_return iAllowed;
	}
	if (co_await Kicked(stRequest.sRoom, stMac.Colon()))
	{
		LOG_WARNING("RoomMgr::Join kick room=%s mac=%s", stRequest.sRoom.c_str(), stMac.Colon().c_str());
		co_return OI_ERR_NOT_FOUND;
	}
	std::shared_ptr<Room> pRoom = co_await Load(stRequest.sRoom);
	if (!pRoom)
	{
		co_return OI_ERR_FAIL;
	}
	Room& stRoom = *pRoom;
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Replace(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
	{
		LOG_WARNING("RoomMgr::Join reject room=%s mac=%s sid=%d reason=no-probe", stRequest.sRoom.c_str(), stMac.Colon().c_str(), stContext.iSessionId);
		co_return OI_ERR_NOT_FOUND;
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
	const int32_t iSaved = co_await stRoom.Saved(::OI::Timer::Seconds(m_iMemberTtl));
	if (iSaved != OI_OK)
	{
		LOG_WARNING("RoomMgr::Join save room=%s code=%d", stRequest.sRoom.c_str(), iSaved);
		co_return iSaved;
	}
	Broadcast(stRequest.sRoom, vSessions, vPeers, stAttr);
	co_await PublishLive(stRoom);
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
	const int32_t iAllowed = co_await Authorize(stRequest.sRoom, stRequest.sToken);
	if (iAllowed != OI_OK)
	{
		co_return iAllowed;
	}
	if (co_await Kicked(stRequest.sRoom, stMac.Colon()))
	{
		std::shared_ptr<Room> pKicked = co_await Load(stRequest.sRoom);
		if (pKicked)
		{
			uint32_t iSourceIp = 0;
			uint16_t iSourcePort = 0;
			if (pKicked->Claim(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
			{
				const bool bRemoved = pKicked->Remove(iSourceIp, stMac);
				pKicked->Release(stContext.iSessionId);
				if (bRemoved)
				{
					std::vector<sid_t> vSessions;
					std::vector<::OI::RpcData::RoomPeer> vPeers;
					::OI::RpcData::RoomAttr stAttr = pKicked->Attr();
					pKicked->CollectSessions(stContext.iSessionId, vSessions);
					pKicked->CopyPeers(vPeers);
					co_await pKicked->Saved(::OI::Timer::Seconds(m_iMemberTtl));
					Broadcast(stRequest.sRoom, vSessions, vPeers, stAttr);
					co_await PublishLive(*pKicked);
				}
			}
		}
		LOG_WARNING("RoomMgr::Heartbeat kick room=%s mac=%s", stRequest.sRoom.c_str(), stMac.Colon().c_str());
		co_return OI_ERR_NOT_FOUND;
	}
	std::shared_ptr<Room> pRoom = co_await Load(stRequest.sRoom);
	if (!pRoom)
	{
		co_return OI_ERR_FAIL;
	}
	Room& stRoom = *pRoom;
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Claim(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
	{
		LOG_WARNING("RoomMgr::Heartbeat reject room=%s mac=%s sid=%d reason=no-probe", stRequest.sRoom.c_str(), stMac.Colon().c_str(), stContext.iSessionId);
		co_return OI_ERR_NOT_FOUND;
	}
	const int32_t iUpdated = stRoom.Update(iSourceIp, iSourcePort, stMac, stRequest.iSawPunch != 0, ::OI::Timer::GetTime(), m_iMemberTtl, stResponse.iVirtualIp, stResponse.vPeers);
	if (iUpdated != OI_OK)
	{
		LOG_WARNING("RoomMgr::Heartbeat fail room=%s mac=%s src=%s:%u code=%d", stRequest.sRoom.c_str(), stMac.Colon().c_str(), ::Lan::Ipv4::Text(iSourceIp).c_str(), iSourcePort, iUpdated);
		co_return iUpdated;
	}
	stResponse.stAttr = stRoom.Attr();
	const int32_t iSaved = co_await stRoom.Saved(::OI::Timer::Seconds(m_iMemberTtl));
	if (iSaved != OI_OK)
	{
		LOG_WARNING("RoomMgr::Heartbeat save room=%s code=%d", stRequest.sRoom.c_str(), iSaved);
		co_return iSaved;
	}
	co_await PublishLive(stRoom);
	co_return OI_OK;
}

::OI::Rpc::RpcTask RoomMgr::Leave(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::LeaveReq& stRequest, ::OI::RpcData::LeaveRes& stResponse)
{
	::Lan::MacAddress stMac;
	if (!stMac.From(stRequest.sMac) || stRequest.sRoom.empty())
	{
		LOG_WARNING("RoomMgr::Leave reject room=%s", stRequest.sRoom.c_str());
		co_return OI_ERR_INVALID_ARGUMENT;
	}
	std::shared_ptr<Room> pRoom = co_await Load(stRequest.sRoom);
	if (!pRoom)
	{
		co_return OI_ERR_FAIL;
	}
	Room& stRoom = *pRoom;
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Owned(stMac, stContext.iSessionId, iSourceIp, iSourcePort))
	{
		LOG_WARNING("RoomMgr::Leave reject room=%s mac=%s sid=%d reason=unowned", stRequest.sRoom.c_str(), stMac.Colon().c_str(), stContext.iSessionId);
		co_return OI_ERR_NOT_FOUND;
	}
	if (!stRoom.Remove(iSourceIp, stMac))
	{
		stRoom.Release(stContext.iSessionId);
		stResponse.iLeft = 0;
		LOG_INFO("RoomMgr::Leave absent room=%s mac=%s", stRequest.sRoom.c_str(), stMac.Colon().c_str());
		co_return OI_OK;
	}
	stRoom.Release(stContext.iSessionId);
	stResponse.iLeft = 1;
	std::vector<sid_t> vSessions;
	std::vector<::OI::RpcData::RoomPeer> vPeers;
	::OI::RpcData::RoomAttr stAttr = stRoom.Attr();
	stRoom.CollectSessions(stContext.iSessionId, vSessions);
	stRoom.CopyPeers(vPeers);
	const int32_t iSaved = co_await stRoom.Saved(::OI::Timer::Seconds(m_iMemberTtl));
	if (iSaved != OI_OK)
	{
		LOG_WARNING("RoomMgr::Leave save room=%s code=%d", stRequest.sRoom.c_str(), iSaved);
		co_return iSaved;
	}
	LOG_INFO("RoomMgr::Leave room=%s mac=%s src=%s:%u members=%zu version=%u", stRequest.sRoom.c_str(), stMac.Colon().c_str(), ::Lan::Ipv4::Text(iSourceIp).c_str(), iSourcePort, vPeers.size(), stAttr.iVersion);
	Broadcast(stRequest.sRoom, vSessions, vPeers, stAttr);
	co_await PublishLive(stRoom);
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
	std::shared_ptr<Room> pRoom = co_await Load(stCarry.sRoom);
	if (!pRoom)
	{
		co_return OI_ERR_FAIL;
	}
	Room& stRoom = *pRoom;
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!stRoom.Owned(stMac, stContext.iSessionId, iSourceIp, iSourcePort) || !stRoom.HasUser(iSourceIp, stMac))
	{
		LOG_WARNING("RoomMgr::Carry reject room=%s mac=%s sid=%d reason=not-member", stCarry.sRoom.c_str(), stMac.Colon().c_str(), stContext.iSessionId);
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

::OI::Coro::Task RoomMgr::OnSecondUpdate(time_t iNow)
{
	const time_t iCacheTtl = 30;
	std::vector<std::shared_ptr<Room>> vFlush;
	for (auto itRoom = m_mRoom.begin(); itRoom != m_mRoom.end(); )
	{
		if (!itRoom->second->Idle(iNow, iCacheTtl))
		{
			++itRoom;
			continue;
		}
		if (itRoom->second->Changed())
		{
			vFlush.push_back(itRoom->second);
			++itRoom;
		}
		else
		{
			itRoom = m_mRoom.erase(itRoom);
		}
	}
	for (const std::shared_ptr<Room>& pRoom : vFlush)
	{
		if (!pRoom->Idle(::OI::Timer::GetTime(), iCacheTtl))
		{
			continue;
		}
		if (pRoom->Changed())
		{
			const int32_t iSaved = co_await pRoom->Saved(::OI::Timer::Seconds(m_iMemberTtl), true);
			if (iSaved != OI_OK)
			{
				LOG_WARNING("RoomMgr::OnSecondUpdate save room=%s code=%d", pRoom->Name().c_str(), iSaved);
				continue;
			}
		}
		if (!pRoom->Changed() && pRoom->Idle(::OI::Timer::GetTime(), iCacheTtl))
		{
			m_mRoom.erase(pRoom->Name());
		}
	}
	co_return;
}
