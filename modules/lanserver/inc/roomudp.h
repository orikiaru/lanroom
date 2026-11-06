#pragma once
#include "lan_udp.h"
#include "oi_rpcdata_probereq.h"
#include "oi_rpcdata_relayreq.h"
#include "oi_singleton.h"

// 房间的 UDP 口。探测和转发都是单向消息，收到交给 RoomMgr，要转的帧从这里发出去。
class RoomUdp : public ::Lan::LanUdp
{
	DECLARE_SINGLETON(RoomUdp)
	RoomUdp();

	::OI::Rpc::RpcTask HandleProbe(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::ProbeReq& stRequest);
	::OI::Rpc::RpcTask HandleRelay(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::RelayReq& stRequest);

  public:
	bool Init();
	virtual const std::string GetIDString();

  private:
	virtual void OnAddSession(::OI::Net::Session* pSession);
	virtual void OnDelSession(const ::OI::Net::Session* pSession);
};
