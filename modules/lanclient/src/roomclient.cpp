#include "roomclient.h"

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

bool RoomClient::Init()
{
	return ::OI::Rpc::ProtoClientManager::Init();
}

void RoomClient::OnClientConnect(OI::Net::Session* pSession)
{
	(void)pSession;
}

void RoomClient::OnClientDisconnect(const OI::Net::Session* pSession)
{
	(void)pSession;
}
