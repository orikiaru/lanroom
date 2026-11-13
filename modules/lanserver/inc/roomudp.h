#pragma once
#include "lan_addr.h"
#include "oi_net.h"
#include "oi_rpc_coro.h"
#include "oi_rpcdata_probereq.h"
#include "oi_singleton.h"
#include <string>

class RoomUdp : public ::OI::Rpc::ProtoSessionManager
{
	DECLARE_SINGLETON(RoomUdp)
	RoomUdp();

	::OI::Rpc::RpcTask HandleProbe(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::ProbeReq& stRequest);
  public:
	int32_t Init() override;
	bool Source(sid_t iSessionId, uint32_t& iIp, uint16_t& iPort);
	sid_t Peer(uint32_t iIp, uint16_t iPort)
	{
		sid_t iSessionId = INVALID_SID;
		if (OpenPeer(::Lan::Ipv4::Text(iIp), std::to_string(iPort), iSessionId) != OI_OK)
		{
			return INVALID_SID;
		}
		return iSessionId;
	}
	virtual const std::string GetIDString();

  private:
	virtual void OnAddSession(::OI::Net::Session* pSession);
	virtual void OnDelSession(const ::OI::Net::Session* pSession);
};
