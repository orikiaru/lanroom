#pragma once
#include "oi_coro.h"
#include "oi_rpcdata_roommember.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_rpcdata_roomattr.h"
#include "oi_rpcdata_roomrecord.h"
#include "oi_header.h"
#include "lan_addr.h"
#include <map>
#include <stdint.h>
#include <time.h>
#include <string>
#include <vector>

class Room
{
	struct SeenEndpoint
	{
		uint32_t iSourceIp = 0;
		uint16_t iSourcePort = 0;
		sid_t iOwner = 0;
	};

	std::string m_sRoom;
	::OI::RpcData::RoomRecord m_stRecord;
	bool m_bBusy = false;
	bool m_bLoaded = false;
	// 转发不读写 Redis，计数先记在这里，下次 Saved 再并进记录。
	uint32_t m_iPendingRelay = 0;
	std::map<std::string, std::vector<SeenEndpoint>> m_mSeen;

	bool SameUser(const ::OI::RpcData::RoomMember& stMember, uint32_t iSourceIp, const ::Lan::MacAddress& stMac) const;
	uint32_t AllocateVirtual() const;
	void RefreshDirect();
	void AdoptTcp(bool bRequested);
	void Touch();
	void WritePeers(std::vector<::OI::RpcData::RoomPeer>& vPeers) const;
	void AddSessions(const ::OI::RpcData::RoomMember& stMember, sid_t iExcept, std::vector<sid_t>& vSessions) const;
	static const size_t kMemberMax = 10;
	bool HasUser(uint32_t iSourceIp, const ::Lan::MacAddress& stMac) const;
	bool Upsert(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, bool bSawPunch, time_t iNow);
	void Expire(time_t iNow, time_t iTtl);
	bool Fill(uint32_t iSourceIp, const ::Lan::MacAddress& stMac, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers) const;

  public:
	void Remember(const ::Lan::MacAddress& stMac, uint32_t iSourceIp, uint16_t iSourcePort);
	bool Claim(const ::Lan::MacAddress& stMac, sid_t iSessionId, uint32_t& iSourceIp, uint16_t& iSourcePort);
	void Release(sid_t iSessionId);
	::OI::Coro::TaskResult<int32_t> Occupy();
	void ReleaseBusy();
	::OI::Coro::TaskResult<int32_t> Loaded(const std::string& sRoom);
	bool Ready() const;
	::OI::Coro::TaskResult<int32_t> Saved();
	int32_t Join(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, bool bTcpRelay, time_t iNow, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers);
	int32_t Update(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, bool bSawPunch, time_t iNow, time_t iTtl, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers);
	bool Remove(uint32_t iSourceIp, const ::Lan::MacAddress& stMac);
	const ::OI::RpcData::RoomMember* FindVirtual(uint32_t iVirtualIp) const;
	void CountRelay();
	uint8_t TcpRelay() const;
	const ::OI::RpcData::RoomAttr& Attr() const;
	const std::string& Name() const;
	void CopyPeers(std::vector<::OI::RpcData::RoomPeer>& vPeers) const;
	void CollectSessions(sid_t iExcept, std::vector<sid_t>& vSessions) const;
	// 单播只取目标成员的 TCP 会话；广播和组播取除发送者外所有人的。
	void CollectTargets(sid_t iExcept, uint32_t iVirtualIp, std::vector<sid_t>& vSessions) const;
};
