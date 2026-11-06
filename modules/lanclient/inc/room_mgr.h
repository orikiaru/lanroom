#pragma once
#include "oi_coro.h"
#include "oi_lock.h"
#include "oi_rpc_coro.h"
#include "oi_rpcdata_framereq.h"
#include "oi_rpcdata_punchreq.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_singleton.h"
#include "packet.h"
#include <atomic>
#include <deque>
#include <string>
#include <thread>
#include <vector>

// 客户端的房间逻辑。加入和心跳走 RoomClient，探测、打洞、帧走 RoomUdp。
// 状态在 m_stStateLock 下读写，发包前先拷出目标再放锁。
class RoomMgr
{
	DECLARE_SINGLETON(RoomMgr)
	RoomMgr();

	::OI::Thread::Mutex m_stStateLock;
	std::string m_sRoom;
	::Lan::MacAddress m_stMac;
	uint32_t m_iServerIp = 0;
	uint16_t m_iServerPort = 0;
	uint16_t m_iNicPort = 0;
	bool m_bJoined = false;
	bool m_bSawPunch = false;
	bool m_bDropDirect = false;
	uint32_t m_iVirtualIp = 0;
	std::vector<::OI::RpcData::RoomPeer> m_vPeers;
	std::deque<std::vector<uint8_t>> m_vInbound;
	std::deque<std::vector<uint8_t>> m_vOutbound;
	std::atomic<bool> m_bRun;
	std::thread m_stNic;

	const ::OI::RpcData::RoomPeer* OtherPeer() const;
	bool FromServer(uint32_t iSourceIp, uint16_t iSourcePort) const;
	void TakeFrame(const std::string& sFrame);
	void KeepSnapshot(uint32_t iVirtualIp, const std::vector<::OI::RpcData::RoomPeer>& vPeers);
	void Probe();
	void SendFrame(const std::vector<uint8_t>& vFrame);
	void Punch();
	::OI::Coro::Task Pump();
	void ServeNic();
	static void WriteAll(uintptr_t iSocket, const char* pData, size_t iLength);
	static void WriteText(uintptr_t iSocket, const std::string& sText);
	static bool ReadSome(uintptr_t iSocket, std::string& sBuffer);

  public:
	bool Prepare(const std::string& sRoom, const std::string& sMac, const std::string& sUdpServer, uint16_t iUdpPort, uint16_t iNicPort, bool bDrop);
	int32_t Punched(const ::OI::RpcData::PunchReq& stPunch, uint32_t iSourceIp, uint16_t iSourcePort);
	int32_t Received(const ::OI::RpcData::FrameReq& stFrame, uint32_t iSourceIp, uint16_t iSourcePort);
	void Start();
	void Stop();
};
