#pragma once
#include "oi_net.h"
#include "oi_rpc_coro.h"
#include <string>

class RoomServer : public OI::Rpc::ProtoSessionManager
{
	static RoomServer inst;

  public:
	RoomServer()
	{
	}
	virtual ~RoomServer();
	bool Init();
	bool OpenUdp(const std::string& sIp, uint16_t iPort);
	uint16_t UdpPort() const;
	static RoomServer& GetInstance()
	{
		return inst;
	}
	virtual const std::string GetIDString();

  private:
	virtual void OnAddSession(OI::Net::Session* pSession);
	virtual void OnDelSession(const OI::Net::Session* pSession);
};
