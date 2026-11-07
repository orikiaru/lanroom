#pragma once
#include "lan_addr.h"
#include "oi_net.h"
#include "oi_rpc_coro.h"
#include "oi_rpcdata_framereq.h"
#include "oi_rpcdata_punchreq.h"
#include "oi_singleton.h"
#include <string>

class RoomUdp : public ::OI::Rpc::ProtoSessionManager
{
	DECLARE_SINGLETON(RoomUdp)
	RoomUdp();

	::OI::Rpc::RpcTask HandlePunch(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::PunchReq& stRequest);
	::OI::Rpc::RpcTask HandleFrame(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::FrameReq& stRequest);

  public:
	bool Init(const std::string& sBind);
	bool Source(sid_t iSessionId, uint32_t& iIp, uint16_t& iPort);
	template <typename Msg>
	int32_t Notify(uint32_t iIp, uint16_t iPort, const Msg& stMessage)
	{
		sid_t iSessionId = OpenPeer(::Lan::Ipv4::Text(iIp), std::to_string(iPort));
		if (iSessionId == INVALID_SID)
		{
			return OI_ERR_CONNECT;
		}
		return ::OI::Rpc::RpcNotify(*this, iSessionId, stMessage);
	}
	virtual const std::string GetIDString();

  private:
	virtual void OnAddSession(::OI::Net::Session* pSession);
	virtual void OnDelSession(const ::OI::Net::Session* pSession);
};
