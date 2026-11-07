#pragma once
#include "oi_session_manager.h"
#include "oi_singleton.h"
#include <stdint.h>
#include <string>
#include <vector>

class NicMgr : public ::OI::Net::SessionManager
{
	DECLARE_SINGLETON(NicMgr)
	NicMgr();

  public:
	bool Listen(uint16_t iPort);
	void SendText(sid_t iSessionId, const std::string& sText);
	void SendFrame(sid_t iSessionId, const std::vector<uint8_t>& vFrame);
	virtual const std::string GetIDString();

  protected:
	virtual ::OI::Net::Session* CloneSession();
	virtual void OnAddSession(::OI::Net::Session* pSession);
	virtual void OnDelSession(const ::OI::Net::Session* pSession);
};
