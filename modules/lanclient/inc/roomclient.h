#pragma once
#include "oi_errno.h"
#include "oi_net.h"
#include "oi_rpc_coro.h"
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
	template <typename Msg>
	int32_t Notify(const Msg& stMessage)
	{
		sid_t iSessionId = ConnectedSid();
		if (iSessionId == INVALID_SID)
		{
			return OI_ERR_CONNECT;
		}
		return ::OI::Rpc::RpcNotify(*this, iSessionId, stMessage);
	}
	virtual const std::string GetIDString();

  private:
	::OI::Rpc::RpcTask HandleRoomCast(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::RoomCastReq& stRequest);
	virtual void OnClientConnect(OI::Net::Session* pSession);
	virtual void OnClientDisconnect(const OI::Net::Session* pSession);
	virtual void OnConnectFailed();
};
