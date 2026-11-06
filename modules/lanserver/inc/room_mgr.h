#pragma once
#include "oi_coro.h"
#include "oi_rpc_coro.h"
#include "oi_rpcdata_heartbeatreq.h"
#include "oi_rpcdata_heartbeatres.h"
#include "oi_rpcdata_joinreq.h"
#include "oi_rpcdata_joinres.h"
#include "oi_rpcdata_probereq.h"
#include "oi_rpcdata_relayreq.h"
#include "oi_singleton.h"
#include "packet.h"
#include "room.h"
#include <map>
#include <time.h>
#include <string>
#include <vector>

class RoomMgr
{
	DECLARE_SINGLETON(RoomMgr)
	RoomMgr();

	std::map<std::string, Room> m_mRoom;
	time_t m_iMemberTtl = 1;

	Room& Take(const std::string& sRoom);

  public:
	void Init();
	int32_t Probe(const ::OI::RpcData::ProbeReq& stProbe, uint32_t iSourceIp, uint16_t iSourcePort);
	// 找到转发目标并记一次转发。成功时填目标地址，帧由调用方发。
	::OI::Rpc::RpcTask Relay(const ::OI::RpcData::RelayReq& stRelay, uint32_t iSourceIp, uint16_t iSourcePort, uint32_t& iTargetIp, uint16_t& iTargetPort);
	void ReleaseSession(sid_t iSessionId);
	::OI::Rpc::RpcTask Join(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::JoinReq& stRequest, ::OI::RpcData::JoinRes& stResponse);
	::OI::Rpc::RpcTask Heartbeat(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::HeartbeatReq& stRequest, ::OI::RpcData::HeartbeatRes& stResponse);
};
