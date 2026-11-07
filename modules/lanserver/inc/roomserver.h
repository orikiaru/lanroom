#pragma once
#include "oi_errno.h"
#include "oi_net.h"
#include "oi_rpc_coro.h"
#include "oi_rpcdata_heartbeatreq.h"
#include "oi_rpcdata_heartbeatres.h"
#include "oi_rpcdata_joinreq.h"
#include "oi_rpcdata_carryreq.h"
#include "oi_rpcdata_joinres.h"
#include "oi_rpcdata_leavereq.h"
#include "oi_rpcdata_leaveres.h"
#include "oi_singleton.h"

class RoomServer : public OI::Rpc::ProtoSessionManager
{
	DECLARE_SINGLETON(RoomServer)
	RoomServer();

	::OI::Rpc::RpcTask HandleJoin(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::JoinReq& stRequest, ::OI::RpcData::JoinRes& stResponse);
	::OI::Rpc::RpcTask HandleHeartbeat(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::HeartbeatReq& stRequest, ::OI::RpcData::HeartbeatRes& stResponse);
	::OI::Rpc::RpcTask HandleLeave(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::LeaveReq& stRequest, ::OI::RpcData::LeaveRes& stResponse);
	::OI::Rpc::RpcTask HandleCarry(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::CarryReq& stRequest);

  public:
	virtual ~RoomServer();
	bool Init();
	template <typename Msg>
	int32_t Notify(sid_t iSessionId, const Msg& stMessage)
	{
		if (iSessionId == INVALID_SID)
		{
			return OI_ERR_CONNECT;
		}
		return ::OI::Rpc::RpcNotify(*this, iSessionId, stMessage);
	}
	virtual const std::string GetIDString();

  private:
	virtual void OnAddSession(OI::Net::Session* pSession);
	virtual void OnDelSession(const OI::Net::Session* pSession);
};
