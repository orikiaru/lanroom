#include "nic_mgr.h"
#include "nic_session.h"
#include "room_mgr.h"
#include <stdio.h>

NicMgr::NicMgr()
{
}

const std::string NicMgr::GetIDString()
{
	return "NicMgr";
}

bool NicMgr::Listen(uint16_t iPort)
{
	if (iPort == 0)
	{
		return false;
	}
	DefaultInit("127.0.0.1", std::to_string(iPort), ::OI::Net::SESSION_TYPE_TCP);
	if (!StartServer())
	{
		return false;
	}
	printf("lanroom nic %u\n", iPort);
	fflush(stdout);
	return true;
}

void NicMgr::SendText(sid_t iSessionId, const std::string& sText)
{
	::OI::Base::Bytes stBytes(sText.data(), sText.size());
	SendBytes(iSessionId, stBytes);
}

void NicMgr::SendFrame(sid_t iSessionId, const std::vector<uint8_t>& vFrame)
{
	std::string sHead = "frame " + std::to_string(vFrame.size()) + "\n";
	::OI::Base::Bytes stBytes(sHead.data(), sHead.size());
	if (!vFrame.empty())
	{
		stBytes.Append(vFrame.data(), vFrame.size());
	}
	SendBytes(iSessionId, stBytes);
}

::OI::Net::Session* NicMgr::CloneSession()
{
	return new NicSession(this);
}

void NicMgr::OnAddSession(::OI::Net::Session* pSession)
{
	(void)pSession;
}

void NicMgr::OnDelSession(const ::OI::Net::Session* pSession)
{
	RoomMgr::GetInstance().CancelRead(pSession->GetSessionID());
}
