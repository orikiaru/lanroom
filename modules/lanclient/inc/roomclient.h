#pragma once
#include "oi_net.h"
#include "oi_rpc_coro.h"
#include "oi_singleton.h"

// 连服务器的 TCP 口。加入和心跳由 RoomMgr 从这里发。
class RoomClient : public OI::Rpc::ProtoClientManager
{
	DECLARE_SINGLETON(RoomClient)
	RoomClient();

  public:
	virtual ~RoomClient();
	bool Init();
	virtual const std::string GetIDString();

  private:
	virtual void OnClientConnect(OI::Net::Session* pSession);
	virtual void OnClientDisconnect(const OI::Net::Session* pSession);
};
