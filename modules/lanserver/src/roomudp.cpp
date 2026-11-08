#include "roomudp.h"
#include "oi_socket.h"
#include "room_mgr.h"
#include "rpc_ids.h"

RoomUdp::RoomUdp()
{
}

const std::string RoomUdp::GetIDString()
{
	return "RoomUdp";
}

bool RoomUdp::Source(sid_t iSessionId, uint32_t& iIp, uint16_t& iPort)
{
	::OI::Net::NetAddr stAddr;
	if (!GetPeerAddr(iSessionId, stAddr) || stAddr.Family() != AF_INET)
	{
		return false;
	}
	const sockaddr_in* pAddr = (const sockaddr_in*)stAddr.NetAttrPtr();
	iIp = ntohl(pAddr->sin_addr.s_addr);
	iPort = stAddr.Port();
	return true;
}

::OI::Rpc::RpcTask RoomUdp::HandleProbe(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::ProbeReq& stRequest)
{
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!Source(stContext.iSessionId, iSourceIp, iSourcePort))
	{
		co_return OI_ERR_NOT_FOUND;
	}
	co_return RoomMgr::GetInstance().Probe(stRequest, iSourceIp, iSourcePort);
}

bool RoomUdp::Init()
{
	OI_NOTIFY_BIND_SELF(Probe, RoomUdp, HandleProbe);
	return ::OI::Rpc::ProtoSessionManager::Init();
}

void RoomUdp::OnAddSession(::OI::Net::Session* pSession)
{
	(void)pSession;
}

void RoomUdp::OnDelSession(const ::OI::Net::Session* pSession)
{
	(void)pSession;
}
