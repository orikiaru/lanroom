#include "roomclient.h"
#include "oi_socket.h"
#include "room_mgr.h"
#include "rpc_ids.h"

RoomClient::RoomClient()
{
}

RoomClient::~RoomClient()
{
}

const std::string RoomClient::GetIDString()
{
	return "RoomClient";
}

::OI::Rpc::RpcTask RoomClient::HandleRoomCast(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::RoomCastReq& stRequest)
{
	(void)stContext;
	RoomMgr::GetInstance().ApplyCast(stRequest);
	co_return OI_OK;
}

::OI::Rpc::RpcTask RoomClient::HandleCarry(const ::OI::Rpc::RpcContext& stContext, const ::OI::RpcData::CarryReq& stRequest)
{
	(void)stContext;
	co_return RoomMgr::GetInstance().Carried(stRequest);
}

bool RoomClient::Init()
{
	OI_NOTIFY_BIND_SELF(RoomCast, RoomClient, HandleRoomCast);
	OI_NOTIFY_BIND_SELF(Carry, RoomClient, HandleCarry);
	if (!::OI::Rpc::ProtoClientManager::Init())
	{
		return false;
	}
	RoomMgr::GetInstance().Start();
	return true;
}

void RoomClient::UseHost(const std::string& sHost)
{
	if (sHost.empty())
	{
		return;
	}
	DefaultInit(sHost, GetStrPort(), GetSessionType());
	sEndpoint = ::OI::Net::NetAddr::Endpoint(sHost, GetStrPort());
}

void RoomClient::OnClientConnect(OI::Net::Session* pSession)
{
	(void)pSession;
}

void RoomClient::OnClientDisconnect(const OI::Net::Session* pSession)
{
	(void)pSession;
}

void RoomClient::OnConnectFailed()
{
	if (!AutoReconnect() || bServiceDown || sEndpoint.empty())
	{
		FailHeldCalls();
		return;
	}
	Reconnect(1000);
}
