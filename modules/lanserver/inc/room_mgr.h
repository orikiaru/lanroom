#pragma once
#include "oi_coro.h"
#include "oi_rpc_coro.h"
#include "oi_rpcdata_heartbeatreq.h"
#include "oi_rpcdata_heartbeatres.h"
#include "oi_rpcdata_joinreq.h"
#include "oi_rpcdata_joinres.h"
#include "oi_rpcdata_leavereq.h"
#include "oi_rpcdata_leaveres.h"
#include "oi_rpcdata_probereq.h"
#include "oi_rpcdata_carryreq.h"
#include "oi_singleton.h"
#include "lan_addr.h"
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
	void Broadcast(const std::string& sRoom, const std::vector<sid_t>& vSessions, const std::vector<::OI::RpcData::RoomPeer>& vPeers, const ::OI::RpcData::RoomAttr& stAttr);

  public:
	void Init();
	int32_t Probe(const ::OI::RpcData::ProbeReq& stProbe, uint32_t iSourceIp, uint16_t iSourcePort);
	::OI::Rpc::RpcTask Carry(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::CarryReq& stCarry);
	void ReleaseSession(sid_t iSessionId);
	::OI::Rpc::RpcTask Join(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::JoinReq& stRequest, ::OI::RpcData::JoinRes& stResponse);
	::OI::Rpc::RpcTask Heartbeat(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::HeartbeatReq& stRequest, ::OI::RpcData::HeartbeatRes& stResponse);
	::OI::Rpc::RpcTask Leave(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::LeaveReq& stRequest, ::OI::RpcData::LeaveRes& stResponse);
};
