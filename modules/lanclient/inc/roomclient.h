#pragma once
#include "oi_net.h"
#include "oi_rpc_coro.h"
#include "oi_rpcdata_carryreq.h"
#include "oi_rpcdata_roomcastreq.h"
#include "oi_singleton.h"

// 连服务器的 TCP 口。加入和心跳由 RoomMgr 从这里发。
class RoomClient : public OI::Rpc::ProtoClientManager
{
	DECLARE_SINGLETON(RoomClient)
	RoomClient();

  public:
	virtual ~RoomClient();
	bool Init();
	void UseHost(const std::string& sHost);
	virtual const std::string GetIDString();

  private:
	::OI::Rpc::RpcTask HandleRoomCast(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::RoomCastReq& stRequest);
	::OI::Rpc::RpcTask HandleCarry(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::CarryReq& stRequest);
	virtual void OnClientConnect(OI::Net::Session* pSession);
	virtual void OnClientDisconnect(const OI::Net::Session* pSession);
	virtual void OnConnectFailed();
};
