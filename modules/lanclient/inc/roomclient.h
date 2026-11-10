#pragma once
#include "oi_net.h"
#include "oi_rpc_coro.h"
#include "oi_rpcdata_carryreq.h"
#include "oi_rpcdata_roomcastreq.h"
#include "oi_singleton.h"

// 连服务器的 TCP 口，写死地址，实例号 0。加入和心跳由 RoomMgr 从这里发。
class RoomClient : public OI::Rpc::ProtoMultiClientManager
{
	DECLARE_SINGLETON(RoomClient)
	RoomClient();

  public:
	virtual ~RoomClient();
	bool Init() override;
	void UseHost(const std::string& sHost);
	const std::string GetIDString() override;

  protected:
	int RetryMilliseconds() const override
	{
		return 1000;
	}

  private:
	::OI::Rpc::RpcTask HandleRoomCast(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::RoomCastReq& stRequest);
	::OI::Rpc::RpcTask HandleCarry(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::CarryReq& stRequest);
};
