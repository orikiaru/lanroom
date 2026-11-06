#pragma once
#include "lan_udp.h"
#include "oi_rpcdata_framereq.h"
#include "oi_rpcdata_punchreq.h"
#include "oi_singleton.h"
#include <string>

// 客户端的 UDP 口。探测和转发发给服务器，打洞和直连帧发给对端，收到的交给 RoomMgr。
class RoomUdp : public ::Lan::LanUdp
{
	DECLARE_SINGLETON(RoomUdp)
	RoomUdp();

	::OI::Rpc::RpcTask HandlePunch(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::PunchReq& stRequest);
	::OI::Rpc::RpcTask HandleFrame(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::FrameReq& stRequest);

  public:
	// 绑定地址由命令行给，端口交给系统选。
	bool Init(const std::string& sBind);
	virtual const std::string GetIDString();

  private:
	virtual void OnAddSession(::OI::Net::Session* pSession);
	virtual void OnDelSession(const ::OI::Net::Session* pSession);
};
