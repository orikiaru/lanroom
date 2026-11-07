#include "nic_session.h"
#include "lan_addr.h"
#include "nic_mgr.h"
#include "oi_timer_manager.h"
#include "room_mgr.h"
#include "roomudp.h"
#include <stdlib.h>

NicSession::NicSession(::OI::Net::SessionManager* pManager)
    : ::OI::Net::NetSession(pManager)
{
}

void NicSession::OnDataIn()
{
	Consume();
}

void NicSession::OnDataOut()
{
}

void NicSession::Consume()
{
	for (;;)
	{
		if (m_iNeed > 0)
		{
			if (stInputBuffer.Size() < m_iNeed)
			{
				return;
			}
			std::vector<uint8_t> vFrame(stInputBuffer.Begin(), stInputBuffer.Begin() + m_iNeed);
			stInputBuffer.Erase(0, m_iNeed);
			m_iNeed = 0;
			NicMgr::GetInstance().SendText(GetSessionID(), RoomMgr::GetInstance().PushOutbound(vFrame) ? "ok\n" : "fail\n");
			continue;
		}
		const char* pBegin = stInputBuffer.Begin();
		const char* pEnd = pBegin + stInputBuffer.Size();
		const char* pNewline = pBegin;
		while (pNewline < pEnd && *pNewline != '\n')
		{
			++pNewline;
		}
		if (pNewline == pEnd)
		{
			return;
		}
		std::string sLine(pBegin, pNewline);
		stInputBuffer.Erase(0, (size_t)(pNewline - pBegin) + 1);
		if (sLine.compare(0, 6, "WRITE ") == 0)
		{
			m_iNeed = (size_t)atoi(sLine.c_str() + 6);
			continue;
		}
		Command(sLine);
	}
}

void NicSession::Command(const std::string& sLine)
{
	sid_t iSessionId = GetSessionID();
	if (sLine == "PORT")
	{
		NicMgr::GetInstance().SendText(iSessionId, "port " + std::to_string(RoomUdp::GetInstance().LocalPort()) + "\n");
	}
	else if (sLine == "DROP 1")
	{
		RoomMgr::GetInstance().SetDrop(true);
		NicMgr::GetInstance().SendText(iSessionId, "ok\n");
	}
	else if (sLine == "DROP 0")
	{
		RoomMgr::GetInstance().SetDrop(false);
		NicMgr::GetInstance().SendText(iSessionId, "ok\n");
	}
	else if (sLine == "VIRT")
	{
		NicMgr::GetInstance().SendText(iSessionId, "virt " + ::Lan::Ipv4::Text(RoomMgr::GetInstance().VirtualIp()) + "\n");
	}
	else if (sLine == "PEERS")
	{
		std::vector<::OI::RpcData::RoomPeer> vPeers;
		RoomMgr::GetInstance().CopyPeers(vPeers);
		NicMgr::GetInstance().SendText(iSessionId, "peers " + std::to_string(vPeers.size()) + "\n");
		for (const ::OI::RpcData::RoomPeer& stPeer : vPeers)
		{
			::Lan::MacAddress stMac;
			std::string sMac = stMac.From(stPeer.sMac) ? stMac.Colon() : "invalid";
			NicMgr::GetInstance().SendText(iSessionId, std::string("peer ") + sMac + " " + ::Lan::Ipv4::Text(stPeer.iSourceIp) + " " + std::to_string(stPeer.iSourcePort) + " " + ::Lan::Ipv4::Text(stPeer.iVirtualIp) + " " + (stPeer.iDirect != 0 ? "1" : "0") + "\n");
		}
	}
		else if (sLine == "LEAVE")
		{
			RoomMgr::GetInstance().RequestLeave();
			NicMgr::GetInstance().SendText(iSessionId, "ok\n");
		}
		else if (sLine == "READ")
	{
		std::vector<uint8_t> vFrame;
		if (RoomMgr::GetInstance().Read(iSessionId, vFrame))
		{
			NicMgr::GetInstance().SendFrame(iSessionId, vFrame);
			return;
		}
		::OI::Timer::BaseTimerManager::GetInstance().AddTimer([iSessionId]()
		{
			if (RoomMgr::GetInstance().ExpireRead(iSessionId))
			{
				NicMgr::GetInstance().SendText(iSessionId, "empty\n");
			}
		}, 2000);
	}
	else
	{
		NicMgr::GetInstance().SendText(iSessionId, "fail\n");
	}
}
