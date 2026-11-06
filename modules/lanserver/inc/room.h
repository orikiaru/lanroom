#pragma once
#include "oi_coro.h"
#include "oi_rpcdata_roommember.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_rpcdata_roomrecord.h"
#include "oi_header.h"
#include "packet.h"
#include <map>
#include <stdint.h>
#include <time.h>
#include <string>
#include <vector>

namespace Lan
{

bool MacFrom(const std::string& sMac, MacAddress& stMac);
std::string MacText(const MacAddress& stMac);

}

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
	std::map<std::string, std::vector<SeenEndpoint>> m_mSeen;

	bool SameUser(const ::OI::RpcData::RoomMember& stMember, uint32_t iSourceIp, const ::Lan::MacAddress& stMac) const;
	uint32_t AllocateVirtual() const;
	void RefreshDirect();
	bool Upsert(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, bool bSawPunch, time_t iNow);
	void Expire(time_t iNow, time_t iTtl);
	bool Fill(uint32_t iSourceIp, const ::Lan::MacAddress& stMac, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers) const;

  public:
	void Remember(const ::Lan::MacAddress& stMac, uint32_t iSourceIp, uint16_t iSourcePort);
	bool Claim(const ::Lan::MacAddress& stMac, sid_t iSessionId, uint32_t& iSourceIp, uint16_t& iSourcePort);
	void Release(sid_t iSessionId);
	::OI::Coro::TaskResult<int32_t> Loaded(const std::string& sRoom);
	::OI::Coro::TaskResult<int32_t> Saved();
	int32_t Join(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, time_t iNow, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers);
	int32_t Update(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, bool bSawPunch, time_t iNow, time_t iTtl, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers);
	const ::OI::RpcData::RoomMember* FindVirtual(uint32_t iVirtualIp) const;
	void CountRelay();
};
