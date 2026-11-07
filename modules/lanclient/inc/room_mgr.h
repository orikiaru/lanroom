#pragma once
#include "lan_addr.h"
#include "oi_coro.h"
#include "oi_lock.h"
#include "oi_rpc_coro.h"
#include "oi_rpcdata_carryreq.h"
#include "oi_rpcdata_roomattr.h"
#include "oi_rpcdata_roomcastreq.h"
#include "oi_rpcdata_framereq.h"
#include "oi_rpcdata_punchreq.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_singleton.h"
#include <atomic>
#include <deque>
#include <string>
#include <vector>

class RoomMgr
{
	DECLARE_SINGLETON(RoomMgr)
	RoomMgr();

	::OI::Thread::Mutex m_stStateLock;
	sid_t m_iReadSession = 0;
	std::string m_sRoom;
	::Lan::MacAddress m_stMac;
	uint32_t m_iServerIp = 0;
	uint16_t m_iServerPort = 0;
	bool m_bJoined = false;
	bool m_bSawPunch = false;
	bool m_bDropDirect = false;
	bool m_bTcpAsk = false;
	bool m_bTcpRelay = false;
	bool m_bArmed = false;
	bool m_bLeaving = false;
	std::string m_sLeaveRoom;
	uint32_t m_iRoomVersion = 0;
	std::atomic<int32_t> m_iJoinCode{0};
	uint32_t m_iAttempt = 0;
	uint32_t m_iVirtualIp = 0;
	std::vector<::OI::RpcData::RoomPeer> m_vPeers;
	std::deque<std::vector<uint8_t>> m_vInbound;
	std::deque<std::vector<uint8_t>> m_vOutbound;
	std::atomic<bool> m_bRun;

	const ::OI::RpcData::RoomPeer* OtherPeer() const;
	bool FromServer(uint32_t iSourceIp, uint16_t iSourcePort) const;
	void TakeFrame(const std::string& sFrame);
	void KeepSnapshot(uint32_t iVirtualIp, const std::vector<::OI::RpcData::RoomPeer>& vPeers, const ::OI::RpcData::RoomAttr& stAttr);
	void Probe();
	void SendFrame(const std::vector<uint8_t>& vFrame);
	void Punch();
	::OI::Coro::Task Pump();

  public:
	bool Prepare(const std::string& sRoom, const std::string& sMac, const std::string& sUdpServer, uint16_t iUdpPort, bool bDrop, bool bTcp);
	bool Retarget(const std::string& sRoom, const std::string& sHost, uint16_t iPort, bool bTcp);
	void RequestLeave();
	int32_t Punched(const ::OI::RpcData::PunchReq& stPunch, uint32_t iSourceIp, uint16_t iSourcePort);
	int32_t Received(const ::OI::RpcData::FrameReq& stFrame, uint32_t iSourceIp, uint16_t iSourcePort);
	void ApplyCast(const ::OI::RpcData::RoomCastReq& stCast);
	int32_t JoinCode() const;
	void SetDrop(bool bDrop);
	uint32_t VirtualIp();
	void CopyPeers(std::vector<::OI::RpcData::RoomPeer>& vPeers);
	bool PushOutbound(const std::vector<uint8_t>& vFrame);
	bool Read(sid_t iSessionId, std::vector<uint8_t>& vFrame);
	bool ExpireRead(sid_t iSessionId);
	void CancelRead(sid_t iSessionId);
	void Start();
	void Stop();
	struct View
	{
		bool bJoined = false;
		bool bArmed = false;
		bool bLeaving = false;
		bool bDirect = false;
		bool bTcp = false;
		uint32_t iVirtualIp = 0;
		std::string sPeer;
	};
	View Current();
};
