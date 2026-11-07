#include "room_mgr.h"
#include "oi_duration.h"
#include "oi_log.h"
#include "oi_rpcdata_leavereq.h"
#include "oi_rpcdata_leaveres.h"
#include "oi_rpcdata_heartbeatreq.h"
#include "oi_rpcdata_heartbeatres.h"
#include "oi_rpcdata_joinreq.h"
#include "oi_rpcdata_joinres.h"
#include "oi_rpcdata_probereq.h"
#include "oi_rpcdata_relayreq.h"
#include "nic_mgr.h"
#include "tun_mgr.h"
#include "roomclient.h"
#include "rpc_enums.h"
#include "rpc_ids.h"
#include "roomudp.h"
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

RoomMgr::RoomMgr()
    : m_bRun(true)
{
}

const ::OI::RpcData::RoomPeer* RoomMgr::OtherPeer() const
{
	std::string sSelf = m_stMac.Bytes();
	for (const ::OI::RpcData::RoomPeer& stPeer : m_vPeers)
	{
		if (stPeer.sMac == sSelf && stPeer.iVirtualIp == m_iVirtualIp)
		{
			continue;
		}
		return &stPeer;
	}
	return NULL;
}

bool RoomMgr::FromServer(uint32_t iSourceIp, uint16_t iSourcePort) const
{
	return iSourceIp == m_iServerIp && iSourcePort == m_iServerPort;
}

void RoomMgr::TakeFrame(const std::string& sFrame)
{
	if (sFrame.empty() || sFrame.size() > ::Lan::kFrameMax)
	{
		return;
	}
	std::vector<uint8_t> vFrame(sFrame.begin(), sFrame.end());
	if (m_iReadSession != 0)
	{
		sid_t iSessionId = m_iReadSession;
		m_iReadSession = 0;
		NicMgr::GetInstance().SendFrame(iSessionId, vFrame);
		return;
	}
	if (TunMgr::GetInstance().Write(vFrame))
	{
		return;
	}
	m_vInbound.push_back(std::move(vFrame));
}

int32_t RoomMgr::Punched(const ::OI::RpcData::PunchReq& stPunch, uint32_t iSourceIp, uint16_t iSourcePort)
{
	(void)stPunch;
	if (FromServer(iSourceIp, iSourcePort))
	{
		return OI_ERR_INVALID_ARGUMENT;
	}
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (!m_bDropDirect)
	{
		m_bSawPunch = true;
	}
	return OI_OK;
}

int32_t RoomMgr::Received(const ::OI::RpcData::FrameReq& stFrame, uint32_t iSourceIp, uint16_t iSourcePort)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (!FromServer(iSourceIp, iSourcePort) && m_bDropDirect)
	{
		return OI_OK;
	}
	TakeFrame(stFrame.sFrame);
	return OI_OK;
}

void RoomMgr::KeepSnapshot(uint32_t iVirtualIp, const std::vector<::OI::RpcData::RoomPeer>& vPeers, const ::OI::RpcData::RoomAttr& stAttr)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (stAttr.iVersion < m_iRoomVersion)
	{
		return;
	}
	m_iRoomVersion = stAttr.iVersion;
	m_iVirtualIp = iVirtualIp;
	m_vPeers = vPeers;
	m_bJoined = true;
	m_bTcpRelay = stAttr.iTcpRelay != 0;
}

int32_t RoomMgr::JoinCode() const
{
	return m_iJoinCode.load();
}

void RoomMgr::ApplyCast(const ::OI::RpcData::RoomCastReq& stCast)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (!m_bJoined || stCast.sRoom != m_sRoom || stCast.stAttr.iVersion < m_iRoomVersion)
	{
		return;
	}
	m_iRoomVersion = stCast.stAttr.iVersion;
	m_vPeers = stCast.vPeers;
	m_bTcpRelay = stCast.stAttr.iTcpRelay != 0;
}

void RoomMgr::Probe()
{
	::OI::RpcData::ProbeReq stProbe;
	stProbe.sRoom = m_sRoom;
	stProbe.sMac = m_stMac.Bytes();
	{
		::OI::Thread::MutexGuard stLock(m_stStateLock);
		stProbe.iSawPunch = m_bSawPunch ? 1 : 0;
	}
	RoomUdp::GetInstance().Notify(m_iServerIp, m_iServerPort, stProbe);
}

void RoomMgr::SendFrame(const std::vector<uint8_t>& vFrame)
{
	struct Target
	{
		bool bDirect = false;
		uint32_t iPeerIp = 0;
		uint16_t iPeerPort = 0;
		uint32_t iVirtualIp = 0;
	};
	std::vector<Target> vTarget;
	bool bTcp = false;
	{
		::OI::Thread::MutexGuard stLock(m_stStateLock);
		bTcp = m_bTcpRelay;
		std::vector<const ::OI::RpcData::RoomPeer*> vOther;
		std::string sSelf = m_stMac.Bytes();
		for (const ::OI::RpcData::RoomPeer& stPeer : m_vPeers)
		{
			if (stPeer.sMac == sSelf && stPeer.iVirtualIp == m_iVirtualIp)
			{
				continue;
			}
			vOther.push_back(&stPeer);
		}
		if (vOther.empty())
		{
			return;
		}
		uint32_t iDest = 0;
		bool bIp = vFrame.size() >= 20 && (vFrame[0] >> 4) == 4;
		if (bIp)
		{
			iDest = ((uint32_t)vFrame[16] << 24) | ((uint32_t)vFrame[17] << 16) | ((uint32_t)vFrame[18] << 8) | (uint32_t)vFrame[19];
		}
		bool bAll = vOther.size() == 1 || (bIp && (iDest == 0xFFFFFFFFu || (iDest & 0xFFu) == 0xFFu || (iDest >> 24) >= 224));
		for (const ::OI::RpcData::RoomPeer* pPeer : vOther)
		{
			if (!bAll && pPeer->iVirtualIp != iDest)
			{
				continue;
			}
			Target stTarget;
			stTarget.bDirect = !bTcp && pPeer->iDirect != 0 && !m_bDropDirect && m_bSawPunch && pPeer->iSourcePort != 0;
			stTarget.iPeerIp = pPeer->iSourceIp;
			stTarget.iPeerPort = pPeer->iSourcePort;
			stTarget.iVirtualIp = pPeer->iVirtualIp;
			vTarget.push_back(stTarget);
		}
	}
	for (const Target& stTarget : vTarget)
	{
		if (bTcp)
		{
			::OI::RpcData::CarryReq stCarry;
			stCarry.sRoom = m_sRoom;
			stCarry.sMac = m_stMac.Bytes();
			stCarry.iVirtualIp = stTarget.iVirtualIp;
			stCarry.sFrame.assign((const char*)vFrame.data(), vFrame.size());
			RoomClient::GetInstance().Notify(stCarry);
			continue;
		}
		if (stTarget.bDirect)
		{
			::OI::RpcData::FrameReq stFrame;
			stFrame.sFrame.assign((const char*)vFrame.data(), vFrame.size());
			RoomUdp::GetInstance().Notify(stTarget.iPeerIp, stTarget.iPeerPort, stFrame);
			continue;
		}
		::OI::RpcData::RelayReq stRelay;
		stRelay.sRoom = m_sRoom;
		stRelay.iVirtualIp = stTarget.iVirtualIp;
		stRelay.sFrame.assign((const char*)vFrame.data(), vFrame.size());
		RoomUdp::GetInstance().Notify(m_iServerIp, m_iServerPort, stRelay);
	}
}

void RoomMgr::Punch()
{
	uint32_t iPeerIp = 0;
	uint16_t iPeerPort = 0;
	{
		::OI::Thread::MutexGuard stLock(m_stStateLock);
		const ::OI::RpcData::RoomPeer* pPeer = OtherPeer();
		if (m_bTcpRelay || pPeer == NULL || m_bDropDirect || pPeer->iSourcePort == 0 || (m_bSawPunch && pPeer->iDirect != 0))
		{
			return;
		}
		iPeerIp = pPeer->iSourceIp;
		iPeerPort = pPeer->iSourcePort;
	}
	::OI::RpcData::PunchReq stPunch;
	stPunch.sMac = m_stMac.Bytes();
	RoomUdp::GetInstance().Notify(iPeerIp, iPeerPort, stPunch);
}

::OI::Coro::Task RoomMgr::Pump()
{
	std::string sMac = m_stMac.Bytes();
	while (m_bRun.load())
	{
		Probe();
		co_await ::OI::Coro::yield(100);
		bool bJoined = false;
		bool bSaw = false;
		bool bTcpAsk = false;
		bool bArmed = false;
		bool bLeaving = false;
		uint32_t iAttempt = 0;
		std::string sRoom;
		std::string sLeaveRoom;
		{
			::OI::Thread::MutexGuard stLock(m_stStateLock);
			bJoined = m_bJoined;
			bSaw = m_bSawPunch;
			bTcpAsk = m_bTcpAsk;
			bArmed = m_bArmed;
			bLeaving = m_bLeaving;
			iAttempt = m_iAttempt;
			sRoom = m_sRoom;
			sLeaveRoom = m_sLeaveRoom;
		}
		if (bLeaving)
		{
			::OI::RpcData::LeaveReq stRequest;
			stRequest.sRoom = sLeaveRoom;
			stRequest.sMac = sMac;
			::OI::RpcData::LeaveRes stResponse;
			const int32_t iRet = co_await ::OI::Rpc::RpcCall(RoomClient::GetInstance(), stRequest, stResponse, ::OI::Timer::Seconds(3));
			::OI::Thread::MutexGuard stLock(m_stStateLock);
			if (m_iAttempt == iAttempt)
			{
				m_bLeaving = false;
				LOG_INFO("RoomMgr::Leave local room=%s code=%d", sLeaveRoom.c_str(), iRet);
			}
			continue;
		}
		if (!bArmed && !bJoined)
		{
			co_await ::OI::Coro::yield(100);
			continue;
		}
		if (!bJoined)
		{
			::OI::RpcData::JoinReq stRequest;
			stRequest.sRoom = sRoom;
			stRequest.sMac = sMac;
			stRequest.stAttr.iTcpRelay = bTcpAsk ? 1 : 0;
			::OI::RpcData::JoinRes stResponse;
			const int32_t iRet = co_await ::OI::Rpc::RpcCall(RoomClient::GetInstance(), stRequest, stResponse, ::OI::Timer::Seconds(3));
			{
				::OI::Thread::MutexGuard stLock(m_stStateLock);
				if (m_iAttempt != iAttempt)
				{
					continue;
				}
			}
			m_iJoinCode.store(iRet);
			if (iRet == OI_OK)
			{
				KeepSnapshot(stResponse.iVirtualIp, stResponse.vPeers, stResponse.stAttr);
			}
			else if (iRet == ::OI::RpcData::ROOM_ERR_FULL)
			{
				co_await ::OI::Coro::yield(5000);
			}
		}
		else
		{
			::OI::RpcData::HeartbeatReq stRequest;
			stRequest.sRoom = m_sRoom;
			stRequest.sMac = sMac;
			stRequest.iSawPunch = bSaw ? 1 : 0;
			::OI::RpcData::HeartbeatRes stResponse;
			const int32_t iRet = co_await ::OI::Rpc::RpcCall(RoomClient::GetInstance(), stRequest, stResponse, ::OI::Timer::Seconds(3));
			if (iRet == OI_OK)
			{
				KeepSnapshot(stResponse.iVirtualIp, stResponse.vPeers, stResponse.stAttr);
			}
		}
		Punch();
		for (;;)
		{
			std::vector<uint8_t> vFrame;
			{
				::OI::Thread::MutexGuard stLock(m_stStateLock);
				if (m_vOutbound.empty())
				{
					break;
				}
				vFrame = std::move(m_vOutbound.front());
				m_vOutbound.pop_front();
			}
			if (vFrame.size() > ::Lan::kFrameMax)
			{
				continue;
			}
			SendFrame(vFrame);
		}
		co_await ::OI::Coro::yield(100);
	}
	co_return;
}

void RoomMgr::SetDrop(bool bDrop)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	m_bDropDirect = bDrop;
	if (bDrop)
	{
		m_bSawPunch = false;
	}
}

uint32_t RoomMgr::VirtualIp()
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	return m_iVirtualIp;
}

void RoomMgr::CopyPeers(std::vector<::OI::RpcData::RoomPeer>& vPeers)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	vPeers = m_vPeers;
}

bool RoomMgr::PushOutbound(const std::vector<uint8_t>& vFrame)
{
	if (vFrame.empty() || vFrame.size() > ::Lan::kFrameMax)
	{
		return false;
	}
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	m_vOutbound.push_back(vFrame);
	return true;
}

bool RoomMgr::Read(sid_t iSessionId, std::vector<uint8_t>& vFrame)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (m_vInbound.empty())
	{
		m_iReadSession = iSessionId;
		return false;
	}
	vFrame = std::move(m_vInbound.front());
	m_vInbound.pop_front();
	return true;
}

bool RoomMgr::ExpireRead(sid_t iSessionId)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (m_iReadSession != iSessionId)
	{
		return false;
	}
	m_iReadSession = 0;
	return true;
}

void RoomMgr::CancelRead(sid_t iSessionId)
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (m_iReadSession == iSessionId)
	{
		m_iReadSession = 0;
	}
}

bool RoomMgr::Prepare(const std::string& sRoom, const std::string& sMac, const std::string& sUdpServer, uint16_t iUdpPort, bool bDrop, bool bTcp)
{
	if (sRoom.empty() || !m_stMac.Parse(sMac))
	{
		return false;
	}
	m_sRoom = sRoom;
	m_bDropDirect = bDrop;
	m_bTcpAsk = bTcp;
	m_bArmed = true;
	m_bLeaving = false;
	m_iServerPort = iUdpPort;
	if (inet_pton(AF_INET, sUdpServer.c_str(), &m_iServerIp) != 1)
	{
		return false;
	}
	m_iServerIp = ntohl(m_iServerIp);
	return true;
}

bool RoomMgr::Retarget(const std::string& sRoom, const std::string& sHost, uint16_t iPort, bool bTcp)
{
	if (sRoom.empty() || iPort == 0)
	{
		return false;
	}
	uint32_t iHost = 0;
	if (inet_pton(AF_INET, sHost.c_str(), &iHost) != 1)
	{
		return false;
	}
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	m_iAttempt += 1;
	m_sRoom = sRoom;
	m_iServerIp = ntohl(iHost);
	m_iServerPort = iPort;
	m_bTcpAsk = bTcp;
	m_bArmed = true;
	m_bLeaving = false;
	m_bJoined = false;
	m_bTcpRelay = false;
	m_bSawPunch = false;
	m_iRoomVersion = 0;
	m_iVirtualIp = 0;
	m_vPeers.clear();
	m_iJoinCode.store(0);
	return true;
}

void RoomMgr::RequestLeave()
{
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	if (m_sRoom.empty())
	{
		return;
	}
	m_iAttempt += 1;
	m_sLeaveRoom = m_sRoom;
	m_bLeaving = true;
	m_bArmed = false;
	m_bJoined = false;
	m_bTcpRelay = false;
	m_bSawPunch = false;
	m_iRoomVersion = 0;
	m_iVirtualIp = 0;
	m_vPeers.clear();
	m_iJoinCode.store(0);
}

void RoomMgr::Start()
{
	::OI::Coro::StartTask([](::OI::Coro::Token)
	{
		return RoomMgr::GetInstance().Pump();
	});
}

void RoomMgr::Stop()
{
	m_bRun.store(false);
}

RoomMgr::View RoomMgr::Current()
{
	View stView;
	::OI::Thread::MutexGuard stLock(m_stStateLock);
	stView.bJoined = m_bJoined;
	stView.bArmed = m_bArmed;
	stView.bLeaving = m_bLeaving;
	stView.bTcp = m_bTcpRelay;
	stView.iVirtualIp = m_iVirtualIp;
	bool bAllDirect = false;
	std::string sSelf = m_stMac.Bytes();
	for (const ::OI::RpcData::RoomPeer& stPeer : m_vPeers)
	{
		if (stPeer.sMac == sSelf && stPeer.iVirtualIp == m_iVirtualIp)
		{
			continue;
		}
		::Lan::MacAddress stMac;
		std::string sMac = stMac.From(stPeer.sMac) ? stMac.Colon() : "invalid";
		if (!stView.sPeer.empty())
		{
			stView.sPeer += "\n";
		}
		else
		{
			bAllDirect = true;
		}
		bAllDirect = bAllDirect && stPeer.iDirect != 0 && stPeer.iSourcePort != 0;
		stView.sPeer += sMac + "  " + ::Lan::Ipv4::Text(stPeer.iVirtualIp) + "  " + ::Lan::Ipv4::Text(stPeer.iSourceIp) + ":" + std::to_string(stPeer.iSourcePort);
	}
	stView.bDirect = bAllDirect && !m_bDropDirect && m_bSawPunch;
	return stView;
}
