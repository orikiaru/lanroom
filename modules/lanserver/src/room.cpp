#include "room.h"
#include "oi_errno.h"
#include "oi_redis_cmd.h"
#include <string.h>

namespace Lan
{

bool MacFrom(const std::string& sMac, MacAddress& stMac)
{
	if (sMac.size() != kMacLength)
	{
		return false;
	}
	memcpy(stMac.vBytes, sMac.data(), kMacLength);
	return true;
}

std::string MacText(const MacAddress& stMac)
{
	return std::string((const char*)stMac.vBytes, kMacLength);
}

}

void Room::Remember(const ::Lan::MacAddress& stMac, uint32_t iSourceIp, uint16_t iSourcePort)
{
	if (iSourcePort == 0)
	{
		return;
	}
	std::vector<SeenEndpoint>& vSeen = m_mSeen[::Lan::MacText(stMac)];
	for (SeenEndpoint& stSlot : vSeen)
	{
		if (stSlot.iSourceIp == iSourceIp)
		{
			stSlot.iSourcePort = iSourcePort;
			return;
		}
	}
	SeenEndpoint stSlot;
	stSlot.iSourceIp = iSourceIp;
	stSlot.iSourcePort = iSourcePort;
	vSeen.push_back(stSlot);
}

bool Room::Claim(const ::Lan::MacAddress& stMac, sid_t iSessionId, uint32_t& iSourceIp, uint16_t& iSourcePort)
{
	auto itSeen = m_mSeen.find(::Lan::MacText(stMac));
	if (itSeen == m_mSeen.end())
	{
		return false;
	}
	SeenEndpoint* pFree = NULL;
	for (SeenEndpoint& stSlot : itSeen->second)
	{
		if (stSlot.iOwner == iSessionId && stSlot.iSourcePort != 0)
		{
			iSourceIp = stSlot.iSourceIp;
			iSourcePort = stSlot.iSourcePort;
			return true;
		}
		if (stSlot.iOwner == 0 && stSlot.iSourcePort != 0 && pFree == NULL)
		{
			pFree = &stSlot;
		}
	}
	if (pFree == NULL)
	{
		return false;
	}
	pFree->iOwner = iSessionId;
	iSourceIp = pFree->iSourceIp;
	iSourcePort = pFree->iSourcePort;
	return true;
}

void Room::Release(sid_t iSessionId)
{
	for (auto& stEntry : m_mSeen)
	{
		for (SeenEndpoint& stSlot : stEntry.second)
		{
			if (stSlot.iOwner == iSessionId)
			{
				stSlot.iOwner = 0;
			}
		}
	}
}

bool Room::SameUser(const ::OI::RpcData::RoomMember& stMember, uint32_t iSourceIp, const ::Lan::MacAddress& stMac) const
{
	return stMember.iSourceIp == iSourceIp && stMember.sMac == ::Lan::MacText(stMac);
}

uint32_t Room::AllocateVirtual() const
{
	const uint32_t iBase = 0x0A420000u;
	for (uint32_t iHost = 1; iHost < 255; ++iHost)
	{
		uint32_t iAddress = iBase | iHost;
		bool bUsed = false;
		for (const ::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
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

void Room::RefreshDirect()
{
	bool bAllSaw = m_stRecord.vMembers.size() >= 2;
	for (const ::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		if (stMember.iSawPunch == 0)
		{
			bAllSaw = false;
			break;
		}
	}
	for (::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		stMember.iDirect = bAllSaw ? 1 : 0;
	}
}

bool Room::Upsert(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, bool bSawPunch, time_t iNow)
{
	for (::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		if (!SameUser(stMember, iSourceIp, stMac))
		{
			continue;
		}
		if (stMember.iSourcePort != iSourcePort)
		{
			stMember.iSawPunch = 0;
		}
		stMember.iSourcePort = iSourcePort;
		stMember.iLastSeen = iNow;
		if (bSawPunch)
		{
			stMember.iSawPunch = 1;
		}
		RefreshDirect();
		return true;
	}
	::OI::RpcData::RoomMember stMember;
	stMember.sMac = ::Lan::MacText(stMac);
	stMember.iSourceIp = iSourceIp;
	stMember.iSourcePort = iSourcePort;
	stMember.iVirtualIp = AllocateVirtual();
	stMember.iSawPunch = bSawPunch ? 1 : 0;
	stMember.iLastSeen = iNow;
	if (stMember.iVirtualIp == 0)
	{
		return false;
	}
	m_stRecord.vMembers.push_back(stMember);
	RefreshDirect();
	return true;
}

void Room::Expire(time_t iNow, time_t iTtl)
{
	std::vector<::OI::RpcData::RoomMember> vAlive;
	for (const ::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		if (iNow - (time_t)stMember.iLastSeen <= iTtl)
		{
			vAlive.push_back(stMember);
		}
	}
	m_stRecord.vMembers.swap(vAlive);
	RefreshDirect();
}

bool Room::Fill(uint32_t iSourceIp, const ::Lan::MacAddress& stMac, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers) const
{
	iVirtualIp = 0;
	vPeers.clear();
	bool bFound = false;
	for (const ::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		if (SameUser(stMember, iSourceIp, stMac))
		{
			bFound = true;
			iVirtualIp = stMember.iVirtualIp;
		}
	}
	if (!bFound)
	{
		return false;
	}
	for (const ::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		::OI::RpcData::RoomPeer stPeer;
		stPeer.sMac = stMember.sMac;
		stPeer.iSourceIp = stMember.iSourceIp;
		stPeer.iSourcePort = stMember.iSourcePort;
		stPeer.iVirtualIp = stMember.iVirtualIp;
		stPeer.iDirect = stMember.iDirect;
		vPeers.push_back(stPeer);
	}
	return true;
}

::OI::Coro::TaskResult<int32_t> Room::Loaded(const std::string& sRoom)
{
	m_sRoom = sRoom;
	m_stRecord = ::OI::RpcData::RoomRecord();
	const int32_t iRet = co_await ::OI::Redis::RedisGet(m_sRoom, m_stRecord);
	if (iRet == OI_ERR_NOT_FOUND)
	{
		co_return OI_OK;
	}
	co_return iRet;
}

::OI::Coro::TaskResult<int32_t> Room::Saved()
{
	if (m_stRecord.vMembers.empty())
	{
		const int32_t iRet = co_await ::OI::Redis::RedisDel(m_sRoom);
		if (iRet == OI_ERR_NOT_FOUND)
		{
			co_return OI_OK;
		}
		co_return iRet;
	}
	co_return co_await ::OI::Redis::RedisSet(m_sRoom, m_stRecord);
}

int32_t Room::Join(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, time_t iNow, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers)
{
	if (!Upsert(iSourceIp, iSourcePort, stMac, false, iNow) || !Fill(iSourceIp, stMac, iVirtualIp, vPeers))
	{
		return OI_ERR_FAIL;
	}
	return OI_OK;
}

int32_t Room::Update(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, bool bSawPunch, time_t iNow, time_t iTtl, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers)
{
	Expire(iNow, iTtl);
	if (!Upsert(iSourceIp, iSourcePort, stMac, bSawPunch, iNow) || !Fill(iSourceIp, stMac, iVirtualIp, vPeers))
	{
		return OI_ERR_FAIL;
	}
	return OI_OK;
}

const ::OI::RpcData::RoomMember* Room::FindVirtual(uint32_t iVirtualIp) const
{
	for (const ::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		if (stMember.iVirtualIp == iVirtualIp)
		{
			return &stMember;
		}
	}
	return NULL;
}

void Room::CountRelay()
{
	m_stRecord.iRelayCount += 1;
}
