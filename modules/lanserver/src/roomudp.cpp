#include "roomudp.h"
#include "oi_rpcdata_framereq.h"
#include "room_mgr.h"
#include "rpc_ids.h"

RoomUdp::RoomUdp()
{
}

const std::string RoomUdp::GetIDString()
{
	return "RoomUdp";
}

::OI::Rpc::RpcTask RoomUdp::HandleProbe(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::ProbeReq& stRequest)
{
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!SourceOf(stContext.iSessionId, iSourceIp, iSourcePort))
	{
		co_return OI_ERR_NOT_FOUND;
	}
	co_return RoomMgr::GetInstance().Probe(stRequest, iSourceIp, iSourcePort);
}

::OI::Rpc::RpcTask RoomUdp::HandleRelay(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::RelayReq& stRequest)
{
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!SourceOf(stContext.iSessionId, iSourceIp, iSourcePort))
	{
		co_return OI_ERR_NOT_FOUND;
	}
	uint32_t iTargetIp = 0;
	uint16_t iTargetPort = 0;
	const int32_t iRelayed = co_await RoomMgr::GetInstance().Relay(stRequest, iSourceIp, iSourcePort, iTargetIp, iTargetPort);
	if (iRelayed != OI_OK)
	{
		co_return iRelayed;
	}
	::OI::RpcData::FrameReq stFrame;
	stFrame.sFrame = stRequest.sFrame;
	co_return NotifyTo(iTargetIp, iTargetPort, stFrame);
}

bool RoomUdp::Init()
{
	OI_NOTIFY_BIND_SELF(Probe, RoomUdp, HandleProbe);
	OI_NOTIFY_BIND_SELF(Relay, RoomUdp, HandleRelay);
	return ::Lan::LanUdp::Init();
}

void RoomUdp::OnAddSession(::OI::Net::Session* pSession)
{
	(void)pSession;
}

void RoomUdp::OnDelSession(const ::OI::Net::Session* pSession)
{
	(void)pSession;
}
