#pragma once
#include "oi_net.h"
#include "oi_rpc_coro.h"
#include <string>

class RoomClient : public OI::Rpc::ProtoClientManager
{
	static RoomClient inst;

  public:
	RoomClient()
	{
	}
	virtual ~RoomClient();
	bool Init();
	bool Prepare(const std::string& sRoom, const std::string& sMac, const std::string& sBind, const std::string& sUdpServer, uint16_t iUdpPort, uint16_t iNicPort, bool bDrop);
	void StartWork();
	void JoinWork();
	static RoomClient& GetInstance()
	{
		return inst;
	}
	virtual const std::string GetIDString();

  private:
	virtual void OnClientConnect(OI::Net::Session* pSession);
	virtual void OnClientDisconnect(const OI::Net::Session* pSession);
};
