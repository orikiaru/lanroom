#include "room.h"
#include "oi_errno.h"
#include "oi_log.h"
#include "oi_redis_cmd.h"
#include "oi_timer_helper.h"
#include "rpc_enums.h"

void Room::Remember(const ::Lan::MacAddress& stMac, uint32_t iSourceIp, uint16_t iSourcePort)
{
	if (iSourcePort == 0)
	{
		return;
	}
	std::vector<SeenEndpoint>& vSeen = m_mSeen[stMac.Bytes()];
	for (SeenEndpoint& stSlot : vSeen)
	{
		if (stSlot.iSourceIp == iSourceIp)
		{
			stSlot.iSourcePort = iSourcePort;
			stSlot.iSeen = ++m_iSeenSeq;
			TouchAccess();
			return;
		}
	}
	SeenEndpoint stSlot;
	stSlot.iSourceIp = iSourceIp;
	stSlot.iSourcePort = iSourcePort;
	stSlot.iSeen = ++m_iSeenSeq;
	vSeen.push_back(stSlot);
	TouchAccess();
}

bool Room::Claim(const ::Lan::MacAddress& stMac, sid_t iSessionId, uint32_t& iSourceIp, uint16_t& iSourcePort)
{
	auto itSeen = m_mSeen.find(stMac.Bytes());
	if (itSeen == m_mSeen.end())
	{
		return false;
	}
	SeenEndpoint* pFree = NULL;
	for (SeenEndpoint& stSlot : itSeen->second)
	{
		if (stSlot.iSourcePort == 0)
		{
			continue;
		}
		if (stSlot.iOwner == iSessionId)
		{
			iSourceIp = stSlot.iSourceIp;
			iSourcePort = stSlot.iSourcePort;
			return true;
		}
		if (stSlot.iOwner == 0 && (pFree == NULL || stSlot.iSeen >= pFree->iSeen))
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

bool Room::Replace(const ::Lan::MacAddress& stMac, sid_t iSessionId, uint32_t& iSourceIp, uint16_t& iSourcePort)
{
	if (Claim(stMac, iSessionId, iSourceIp, iSourcePort))
	{
		return true;
	}
	auto itSeen = m_mSeen.find(stMac.Bytes());
	if (itSeen == m_mSeen.end())
	{
		return false;
	}
	SeenEndpoint* pLatest = NULL;
	for (SeenEndpoint& stSlot : itSeen->second)
	{
		if (stSlot.iSourcePort == 0)
		{
			continue;
		}
		if (pLatest == NULL || stSlot.iSeen >= pLatest->iSeen)
		{
			pLatest = &stSlot;
		}
	}
	if (pLatest == NULL)
	{
		return false;
	}
	pLatest->iOwner = iSessionId;
	iSourceIp = pLatest->iSourceIp;
	iSourcePort = pLatest->iSourcePort;
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
	return stMember.iSourceIp == iSourceIp && stMember.sMac == stMac.Bytes();
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

void Room::AdoptTcp(bool bRequested)
{
	if (m_stRecord.vMembers.empty())
	{
		m_stRecord.stAttr.iTcpRelay = bRequested ? 1 : 0;
	}
}

void Room::RefreshDirect()
{
	if (m_stRecord.stAttr.iTcpRelay != 0)
	{
		for (::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
		{
			stMember.iDirect = 0;
		}
		return;
	}
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

bool Room::HasUser(uint32_t iSourceIp, const ::Lan::MacAddress& stMac) const
{
	for (const ::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		if (SameUser(stMember, iSourceIp, stMac))
		{
			return true;
		}
	}
	return false;
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
			LOG_INFO("Room::Upsert port room=%s mac=%s old=%u new=%u", m_sRoom.c_str(), stMac.Colon().c_str(), stMember.iSourcePort, iSourcePort);
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
	stMember.sMac = stMac.Bytes();
	stMember.iSourceIp = iSourceIp;
	stMember.iSourcePort = iSourcePort;
	stMember.iVirtualIp = AllocateVirtual();
	stMember.iSawPunch = bSawPunch ? 1 : 0;
	stMember.iLastSeen = iNow;
	if (stMember.iVirtualIp == 0)
	{
		LOG_WARNING("Room::Upsert full room=%s mac=%s", m_sRoom.c_str(), stMac.Colon().c_str());
		return false;
	}
	LOG_INFO("Room::Upsert new room=%s mac=%s src=%s:%u virt=%s", m_sRoom.c_str(), stMac.Colon().c_str(), ::Lan::Ipv4::Text(iSourceIp).c_str(), iSourcePort, ::Lan::Ipv4::Text(stMember.iVirtualIp).c_str());
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
		else
		{
			::Lan::MacAddress stMac;
			LOG_INFO("Room::Expire room=%s mac=%s virt=%s", m_sRoom.c_str(), (stMac.From(stMember.sMac) ? stMac.Colon() : std::string("invalid")).c_str(), ::Lan::Ipv4::Text(stMember.iVirtualIp).c_str());
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
	WritePeers(vPeers);
	return true;
}

void Room::WritePeers(std::vector<::OI::RpcData::RoomPeer>& vPeers) const
{
	vPeers.clear();
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
}

void Room::Touch()
{
	m_stRecord.stAttr.iVersion += 1;
	TouchAccess();
}

void Room::TouchAccess()
{
	m_iLastAccess = ::OI::Timer::GetTime();
}

void Room::AddSessions(const ::OI::RpcData::RoomMember& stMember, sid_t iExcept, std::vector<sid_t>& vSessions) const
{
	auto itSeen = m_mSeen.find(stMember.sMac);
	if (itSeen == m_mSeen.end())
	{
		return;
	}
	for (const SeenEndpoint& stSlot : itSeen->second)
	{
		if (stSlot.iOwner == 0 || stSlot.iOwner == iExcept || stSlot.iSourceIp != stMember.iSourceIp)
		{
			continue;
		}
		bool bHave = false;
		for (sid_t iSessionId : vSessions)
		{
			if (iSessionId == stSlot.iOwner)
			{
				bHave = true;
				break;
			}
		}
		if (!bHave)
		{
			vSessions.push_back(stSlot.iOwner);
		}
	}
}

void Room::CollectSessions(sid_t iExcept, std::vector<sid_t>& vSessions) const
{
	vSessions.clear();
	for (const ::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		AddSessions(stMember, iExcept, vSessions);
	}
}

void Room::CollectTargets(sid_t iExcept, uint32_t iVirtualIp, std::vector<sid_t>& vSessions) const
{
	if (::Lan::Ipv4::ToAll(iVirtualIp))
	{
		CollectSessions(iExcept, vSessions);
		return;
	}
	vSessions.clear();
	const ::OI::RpcData::RoomMember* pTarget = FindVirtual(iVirtualIp);
	if (pTarget != NULL)
	{
		AddSessions(*pTarget, iExcept, vSessions);
	}
}

::OI::Coro::TaskResult<int32_t> Room::Occupy()
{
	while (m_bBusy)
	{
		co_await ::OI::Coro::yield(5);
	}
	m_bBusy = true;
	co_return OI_OK;
}

void Room::ReleaseBusy()
{
	m_bBusy = false;
}

::OI::Coro::TaskResult<int32_t> Room::Loaded(const std::string& sRoom)
{
	m_sRoom = sRoom;
	TouchAccess();
	if (m_bLoaded)
	{
		co_return OI_OK;
	}
	::OI::RpcData::RoomRecord stLoaded;
	const int32_t iRet = co_await ::OI::Redis::RedisGet(m_sRoom, stLoaded);
	if (iRet == OI_ERR_NOT_FOUND)
	{
		m_stRecord = ::OI::RpcData::RoomRecord();
		m_bLoaded = true;
		m_iLastSave = m_iLastAccess;
		co_return OI_OK;
	}
	if (iRet != OI_OK)
	{
		co_return iRet;
	}
	m_stRecord = stLoaded;
	m_bLoaded = true;
	m_iLastSave = m_iLastAccess;
	co_return OI_OK;
}

bool Room::Ready() const
{
	return m_bLoaded;
}

bool Room::Changed() const
{
	return m_iLastAccess > m_iLastSave;
}

bool Room::Idle(time_t iNow, time_t iTtl) const
{
	return m_iLastAccess == 0 || iNow - m_iLastAccess >= iTtl;
}

::OI::Coro::TaskResult<int32_t> Room::Saved(::OI::Timer::MS stTimeToLive)
{
	if (m_stRecord.vMembers.empty())
	{
		const int32_t iRet = co_await ::OI::Redis::RedisDel(m_sRoom);
		if (iRet == OI_OK || iRet == OI_ERR_NOT_FOUND)
		{
			m_iLastSave = ::OI::Timer::GetTime();
			co_return OI_OK;
		}
		co_return iRet;
	}
	m_stRecord.iRelayCount += m_iPendingRelay;
	m_iPendingRelay = 0;
	const int32_t iRet = co_await ::OI::Redis::RedisSet(m_sRoom, m_stRecord, stTimeToLive);
	if (iRet == OI_OK)
	{
		m_iLastSave = ::OI::Timer::GetTime();
	}
	co_return iRet;
}

int32_t Room::Join(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, bool bTcpRelay, time_t iNow, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers)
{
	AdoptTcp(bTcpRelay);
	if (!HasUser(iSourceIp, stMac) && m_stRecord.vMembers.size() >= kMemberMax)
	{
		LOG_WARNING("Room::Join full room=%s mac=%s members=%zu", m_sRoom.c_str(), stMac.Colon().c_str(), m_stRecord.vMembers.size());
		return ::OI::RpcData::ROOM_ERR_FULL;
	}
	if (!Upsert(iSourceIp, iSourcePort, stMac, false, iNow) || !Fill(iSourceIp, stMac, iVirtualIp, vPeers))
	{
		return OI_ERR_FAIL;
	}
	Touch();
	return OI_OK;
}

int32_t Room::Update(uint32_t iSourceIp, uint16_t iSourcePort, const ::Lan::MacAddress& stMac, bool bSawPunch, time_t iNow, time_t iTtl, uint32_t& iVirtualIp, std::vector<::OI::RpcData::RoomPeer>& vPeers)
{
	Expire(iNow, iTtl);
	if (!HasUser(iSourceIp, stMac) && m_stRecord.vMembers.size() >= kMemberMax)
	{
		LOG_WARNING("Room::Update full room=%s mac=%s members=%zu", m_sRoom.c_str(), stMac.Colon().c_str(), m_stRecord.vMembers.size());
		return ::OI::RpcData::ROOM_ERR_FULL;
	}
	if (!Upsert(iSourceIp, iSourcePort, stMac, bSawPunch, iNow) || !Fill(iSourceIp, stMac, iVirtualIp, vPeers))
	{
		return OI_ERR_FAIL;
	}
	Touch();
	return OI_OK;
}

bool Room::Remove(uint32_t iSourceIp, const ::Lan::MacAddress& stMac)
{
	std::vector<::OI::RpcData::RoomMember> vLeft;
	bool bRemoved = false;
	for (const ::OI::RpcData::RoomMember& stMember : m_stRecord.vMembers)
	{
		if (!bRemoved && SameUser(stMember, iSourceIp, stMac))
		{
			bRemoved = true;
			continue;
		}
		vLeft.push_back(stMember);
	}
	if (!bRemoved)
	{
		return false;
	}
	m_stRecord.vMembers.swap(vLeft);
	RefreshDirect();
	Touch();
	return true;
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
	m_iPendingRelay += 1;
	TouchAccess();
}

uint8_t Room::TcpRelay() const
{
	return m_stRecord.stAttr.iTcpRelay;
}

const ::OI::RpcData::RoomAttr& Room::Attr() const
{
	return m_stRecord.stAttr;
}

const std::string& Room::Name() const
{
	return m_sRoom;
}

void Room::CopyPeers(std::vector<::OI::RpcData::RoomPeer>& vPeers) const
{
	WritePeers(vPeers);
}
