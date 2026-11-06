#pragma once
#include "packet.h"
#include <stdint.h>
#include <string>
#include <vector>

namespace Lan
{

struct Member
{
	MacAddress stMac;
	uint32_t iSourceIp;
	uint16_t iSourcePort;
	uint32_t iVirtualIp;
	bool bSawPunch;
	bool bDirect;
	int64_t iLastSeenMs;
};

struct RoomRecord
{
	uint32_t iRelayCount;
	std::vector<Member> vMembers;
};

// 身份是来源 IP + MAC。端口变化只更新这一条。
bool SameUser(const Member& stMember, uint32_t iSourceIp, const MacAddress& stMac);
bool UpsertMember(RoomRecord& stRoom, uint32_t iSourceIp, uint16_t iSourcePort, const MacAddress& stMac, bool bSawPunch, int64_t iNowMs);
void ExpireMembers(RoomRecord& stRoom, int64_t iNowMs, int64_t iTtlMs);
void RefreshDirect(RoomRecord& stRoom);
Member* FindUser(RoomRecord& stRoom, uint32_t iSourceIp, const MacAddress& stMac);
const Member* FindVirtual(const RoomRecord& stRoom, uint32_t iVirtualIp);
Snapshot SnapshotFor(const RoomRecord& stRoom, uint32_t iSourceIp, const MacAddress& stMac);

std::string EncodeRoom(const RoomRecord& stRoom);
bool DecodeRoom(const std::string& sBlob, RoomRecord& stRoom);
bool SamePersisted(const RoomRecord& stLeft, const RoomRecord& stRight);

}
