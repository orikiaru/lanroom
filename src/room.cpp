#include "room.h"
#include <string.h>

namespace Lan
{

namespace
{
const uint32_t kRoomBlobMagic = 0x4D4F4F52u;
const uint32_t kVirtualBase = 0x0A420000u;

void AppendU16(std::string& sBlob, uint16_t iValue)
{
	sBlob.push_back((char)(iValue & 0xFF));
	sBlob.push_back((char)((iValue >> 8) & 0xFF));
}

void AppendU32(std::string& sBlob, uint32_t iValue)
{
	sBlob.push_back((char)(iValue & 0xFF));
	sBlob.push_back((char)((iValue >> 8) & 0xFF));
	sBlob.push_back((char)((iValue >> 16) & 0xFF));
	sBlob.push_back((char)((iValue >> 24) & 0xFF));
}

void AppendU64(std::string& sBlob, uint64_t iValue)
{
	for (int iShift = 0; iShift < 64; iShift += 8)
	{
		sBlob.push_back((char)((iValue >> iShift) & 0xFF));
	}
}

bool TakeU16(const std::string& sBlob, size_t& iOffset, uint16_t& iValue)
{
	if (iOffset + 2 > sBlob.size())
	{
		return false;
	}
	iValue = (uint16_t)(uint8_t)sBlob[iOffset] | ((uint16_t)(uint8_t)sBlob[iOffset + 1] << 8);
	iOffset += 2;
	return true;
}

bool TakeU32(const std::string& sBlob, size_t& iOffset, uint32_t& iValue)
{
	if (iOffset + 4 > sBlob.size())
	{
		return false;
	}
	iValue = (uint32_t)(uint8_t)sBlob[iOffset] | ((uint32_t)(uint8_t)sBlob[iOffset + 1] << 8) | ((uint32_t)(uint8_t)sBlob[iOffset + 2] << 16) | ((uint32_t)(uint8_t)sBlob[iOffset + 3] << 24);
	iOffset += 4;
	return true;
}

bool TakeU64(const std::string& sBlob, size_t& iOffset, uint64_t& iValue)
{
	if (iOffset + 8 > sBlob.size())
	{
		return false;
	}
	iValue = 0;
	for (int iShift = 0; iShift < 64; iShift += 8)
	{
		iValue |= (uint64_t)(uint8_t)sBlob[iOffset] << iShift;
		iOffset += 1;
	}
	return true;
}

uint32_t AllocateVirtual(const RoomRecord& stRoom)
{
	for (uint32_t iHost = 1; iHost < 255; ++iHost)
	{
		uint32_t iAddress = kVirtualBase | iHost;
		bool bUsed = false;
		for (const Member& stMember : stRoom.vMembers)
		{
			if (stMember.iVirtualIp == iAddress)
			{
				bUsed = true;
				break;
			}
		}
		if (!bUsed)
		{
			return iAddress;
		}
	}
	return 0;
}
}

bool SameUser(const Member& stMember, uint32_t iSourceIp, const MacAddress& stMac)
{
	return stMember.iSourceIp == iSourceIp && MacEqual(stMember.stMac, stMac);
}

void RefreshDirect(RoomRecord& stRoom)
{
	bool bAllSaw = stRoom.vMembers.size() >= 2;
	for (const Member& stMember : stRoom.vMembers)
	{
		if (!stMember.bSawPunch)
		{
			bAllSaw = false;
			break;
		}
	}
	for (Member& stMember : stRoom.vMembers)
	{
		stMember.bDirect = bAllSaw;
	}
}

bool UpsertMember(RoomRecord& stRoom, uint32_t iSourceIp, uint16_t iSourcePort, const MacAddress& stMac, bool bSawPunch, int64_t iNowMs)
{
	for (Member& stMember : stRoom.vMembers)
	{
		if (!SameUser(stMember, iSourceIp, stMac))
		{
			continue;
		}
		if (stMember.iSourcePort != iSourcePort)
		{
			stMember.bSawPunch = false;
		}
		stMember.iSourcePort = iSourcePort;
		stMember.iLastSeenMs = iNowMs;
		if (bSawPunch)
		{
			stMember.bSawPunch = true;
		}
		RefreshDirect(stRoom);
		return true;
	}
	Member stMember;
	memset(&stMember, 0, sizeof(stMember));
	stMember.stMac = stMac;
	stMember.iSourceIp = iSourceIp;
	stMember.iSourcePort = iSourcePort;
	stMember.iVirtualIp = AllocateVirtual(stRoom);
	stMember.bSawPunch = bSawPunch;
	stMember.iLastSeenMs = iNowMs;
	if (stMember.iVirtualIp == 0)
	{
		return false;
	}
	stRoom.vMembers.push_back(stMember);
	RefreshDirect(stRoom);
	return true;
}

void ExpireMembers(RoomRecord& stRoom, int64_t iNowMs, int64_t iTtlMs)
{
	std::vector<Member> vAlive;
	for (const Member& stMember : stRoom.vMembers)
	{
		if (iNowMs - stMember.iLastSeenMs <= iTtlMs)
		{
			vAlive.push_back(stMember);
		}
	}
	stRoom.vMembers.swap(vAlive);
	RefreshDirect(stRoom);
}

Member* FindUser(RoomRecord& stRoom, uint32_t iSourceIp, const MacAddress& stMac)
{
	for (Member& stMember : stRoom.vMembers)
	{
		if (SameUser(stMember, iSourceIp, stMac))
		{
			return &stMember;
		}
	}
	return NULL;
}

const Member* FindVirtual(const RoomRecord& stRoom, uint32_t iVirtualIp)
{
	for (const Member& stMember : stRoom.vMembers)
	{
		if (stMember.iVirtualIp == iVirtualIp)
		{
			return &stMember;
		}
	}
	return NULL;
}

Snapshot SnapshotFor(const RoomRecord& stRoom, uint32_t iSourceIp, const MacAddress& stMac)
{
	Snapshot stSnapshot;
	stSnapshot.iCode = kSnapshotOk;
	stSnapshot.iVirtualIp = 0;
	const Member* pSelf = NULL;
	for (const Member& stMember : stRoom.vMembers)
	{
		if (SameUser(stMember, iSourceIp, stMac))
		{
			pSelf = &stMember;
			stSnapshot.iVirtualIp = stMember.iVirtualIp;
		}
	}
	if (pSelf == NULL)
	{
		stSnapshot.iCode = kSnapshotBadMac;
		return stSnapshot;
	}
	for (const Member& stMember : stRoom.vMembers)
	{
		PeerView stPeer;
		stPeer.stMac = stMember.stMac;
		stPeer.iSourceIp = stMember.iSourceIp;
		stPeer.iSourcePort = stMember.iSourcePort;
		stPeer.iVirtualIp = stMember.iVirtualIp;
		stPeer.bDirect = stMember.bDirect;
		stSnapshot.vPeers.push_back(stPeer);
	}
	return stSnapshot;
}

std::string EncodeRoom(const RoomRecord& stRoom)
{
	std::string sBlob;
	AppendU32(sBlob, kRoomBlobMagic);
	sBlob.push_back(1);
	AppendU32(sBlob, stRoom.iRelayCount);
	sBlob.push_back((char)stRoom.vMembers.size());
	for (const Member& stMember : stRoom.vMembers)
	{
		sBlob.append((const char*)stMember.stMac.vBytes, kMacLength);
		AppendU32(sBlob, stMember.iSourceIp);
		AppendU16(sBlob, stMember.iSourcePort);
		AppendU32(sBlob, stMember.iVirtualIp);
		sBlob.push_back(stMember.bSawPunch ? 1 : 0);
		sBlob.push_back(stMember.bDirect ? 1 : 0);
		AppendU64(sBlob, (uint64_t)stMember.iLastSeenMs);
	}
	return sBlob;
}

bool SamePersisted(const RoomRecord& stLeft, const RoomRecord& stRight)
{
	if (stLeft.iRelayCount != stRight.iRelayCount || stLeft.vMembers.size() != stRight.vMembers.size())
	{
		return false;
	}
	for (size_t iIndex = 0; iIndex < stLeft.vMembers.size(); ++iIndex)
	{
		const Member& stOne = stLeft.vMembers[iIndex];
		const Member& stTwo = stRight.vMembers[iIndex];
		if (!MacEqual(stOne.stMac, stTwo.stMac) || stOne.iSourceIp != stTwo.iSourceIp || stOne.iSourcePort != stTwo.iSourcePort || stOne.iVirtualIp != stTwo.iVirtualIp || stOne.bSawPunch != stTwo.bSawPunch || stOne.bDirect != stTwo.bDirect)
		{
			return false;
		}
	}
	return true;
}

bool DecodeRoom(const std::string& sBlob, RoomRecord& stRoom)
{
	stRoom = RoomRecord();
	size_t iOffset = 0;
	uint32_t iMagic = 0;
	if (!TakeU32(sBlob, iOffset, iMagic) || iMagic != kRoomBlobMagic || iOffset >= sBlob.size() || sBlob[iOffset] != 1)
	{
		return false;
	}
	iOffset += 1;
	if (!TakeU32(sBlob, iOffset, stRoom.iRelayCount) || iOffset >= sBlob.size())
	{
		return false;
	}
	uint8_t iCount = (uint8_t)sBlob[iOffset];
	iOffset += 1;
	for (uint8_t iIndex = 0; iIndex < iCount; ++iIndex)
	{
		if (iOffset + kMacLength > sBlob.size())
		{
			return false;
		}
		Member stMember;
		memset(&stMember, 0, sizeof(stMember));
		memcpy(stMember.stMac.vBytes, sBlob.data() + iOffset, kMacLength);
		iOffset += kMacLength;
		uint64_t iSeen = 0;
		uint8_t iSaw = 0;
		uint8_t iDirect = 0;
		if (!TakeU32(sBlob, iOffset, stMember.iSourceIp) || !TakeU16(sBlob, iOffset, stMember.iSourcePort) || !TakeU32(sBlob, iOffset, stMember.iVirtualIp) || iOffset + 2 > sBlob.size())
		{
			return false;
		}
		iSaw = (uint8_t)sBlob[iOffset];
		iDirect = (uint8_t)sBlob[iOffset + 1];
		iOffset += 2;
		if (!TakeU64(sBlob, iOffset, iSeen))
		{
			return false;
		}
		stMember.bSawPunch = iSaw != 0;
		stMember.bDirect = iDirect != 0;
		stMember.iLastSeenMs = (int64_t)iSeen;
		stRoom.vMembers.push_back(stMember);
	}
	return iOffset == sBlob.size();
}

}
