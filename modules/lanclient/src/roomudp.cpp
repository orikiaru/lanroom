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

::OI::Rpc::RpcTask RoomUdp::HandlePunch(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::PunchReq& stRequest)
{
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!Source(stContext.iSessionId, iSourceIp, iSourcePort))
	{
		co_return OI_ERR_NOT_FOUND;
	}
	co_return RoomMgr::GetInstance().Punched(stRequest, iSourceIp, iSourcePort);
}

::OI::Rpc::RpcTask RoomUdp::HandleFrame(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::FrameReq& stRequest)
{
	uint32_t iSourceIp = 0;
	uint16_t iSourcePort = 0;
	if (!Source(stContext.iSessionId, iSourceIp, iSourcePort))
	{
		co_return OI_ERR_NOT_FOUND;
	}
	co_return RoomMgr::GetInstance().Received(stRequest, iSourceIp, iSourcePort);
}

bool RoomUdp::Init(const std::string& sBind)
{
	OI_NOTIFY_BIND_SELF(Punch, RoomUdp, HandlePunch);
	OI_NOTIFY_BIND_SELF(Frame, RoomUdp, HandleFrame);
	if (!::OI::Rpc::ProtoSessionManager::Init())
	{
		return false;
	}
	DefaultInit(sBind, "0", ::OI::Net::SESSION_TYPE_UDP);
	return true;
}

void RoomUdp::OnAddSession(::OI::Net::Session* pSession)
{
	(void)pSession;
}

void RoomUdp::OnDelSession(const ::OI::Net::Session* pSession)
{
	(void)pSession;
}
