#include "packet.h"
#include <stdio.h>
#include <string.h>

namespace Lan
{

namespace
{
void WriteU16(std::vector<uint8_t>& vPacket, uint16_t iValue)
{
	vPacket.push_back((uint8_t)(iValue & 0xFF));
	vPacket.push_back((uint8_t)((iValue >> 8) & 0xFF));
}

void WriteU32(std::vector<uint8_t>& vPacket, uint32_t iValue)
{
	vPacket.push_back((uint8_t)(iValue & 0xFF));
	vPacket.push_back((uint8_t)((iValue >> 8) & 0xFF));
	vPacket.push_back((uint8_t)((iValue >> 16) & 0xFF));
	vPacket.push_back((uint8_t)((iValue >> 24) & 0xFF));
}

bool ReadU16(const uint8_t* pData, size_t iLength, size_t& iOffset, uint16_t& iValue)
{
	if (iOffset + 2 > iLength)
	{
		return false;
	}
	iValue = (uint16_t)pData[iOffset] | ((uint16_t)pData[iOffset + 1] << 8);
	iOffset += 2;
	return true;
}

bool ReadU32(const uint8_t* pData, size_t iLength, size_t& iOffset, uint32_t& iValue)
{
	if (iOffset + 4 > iLength)
	{
		return false;
	}
	iValue = (uint32_t)pData[iOffset] | ((uint32_t)pData[iOffset + 1] << 8) | ((uint32_t)pData[iOffset + 2] << 16) | ((uint32_t)pData[iOffset + 3] << 24);
	iOffset += 4;
	return true;
}

bool Begin(std::vector<uint8_t>& vPacket, uint8_t iType)
{
	vPacket.clear();
	WriteU32(vPacket, kPacketMagic);
	vPacket.push_back(kPacketVersion);
	vPacket.push_back(iType);
	WriteU16(vPacket, 0);
	return true;
}

void Finish(std::vector<uint8_t>& vPacket)
{
	uint16_t iPayload = (uint16_t)(vPacket.size() - 8);
	vPacket[6] = (uint8_t)(iPayload & 0xFF);
	vPacket[7] = (uint8_t)((iPayload >> 8) & 0xFF);
}
}

bool MacEqual(const MacAddress& stLeft, const MacAddress& stRight)
{
	return memcmp(stLeft.vBytes, stRight.vBytes, kMacLength) == 0;
}

bool ParseMac(const std::string& sText, MacAddress& stMac)
{
	unsigned int vParts[kMacLength];
	if (sscanf(sText.c_str(), "%x:%x:%x:%x:%x:%x", &vParts[0], &vParts[1], &vParts[2], &vParts[3], &vParts[4], &vParts[5]) != kMacLength)
	{
		return false;
	}
	for (int iIndex = 0; iIndex < kMacLength; ++iIndex)
	{
		if (vParts[iIndex] > 0xFF)
		{
			return false;
		}
		stMac.vBytes[iIndex] = (uint8_t)vParts[iIndex];
	}
	return true;
}

std::string FormatIpv4(uint32_t iAddress)
{
	char vText[32];
	snprintf(vText, sizeof(vText), "%u.%u.%u.%u", (iAddress >> 24) & 0xFF, (iAddress >> 16) & 0xFF, (iAddress >> 8) & 0xFF, iAddress & 0xFF);
	return vText;
}

bool EncodeJoin(const std::string& sRoom, const MacAddress& stMac, uint32_t iClaimedIp, std::vector<uint8_t>& vPacket)
{
	if (sRoom.size() > kRoomNameMax)
	{
		return false;
	}
	Begin(vPacket, kPacketJoin);
	vPacket.push_back((uint8_t)sRoom.size());
	vPacket.insert(vPacket.end(), sRoom.begin(), sRoom.end());
	vPacket.insert(vPacket.end(), stMac.vBytes, stMac.vBytes + kMacLength);
	WriteU32(vPacket, iClaimedIp);
	Finish(vPacket);
	return true;
}

bool EncodeHeartbeat(const std::string& sRoom, const MacAddress& stMac, bool bSawPunch, std::vector<uint8_t>& vPacket)
{
	if (sRoom.size() > kRoomNameMax)
	{
		return false;
	}
	Begin(vPacket, kPacketHeartbeat);
	vPacket.push_back((uint8_t)sRoom.size());
	vPacket.insert(vPacket.end(), sRoom.begin(), sRoom.end());
	vPacket.insert(vPacket.end(), stMac.vBytes, stMac.vBytes + kMacLength);
	vPacket.push_back(bSawPunch ? 1 : 0);
	Finish(vPacket);
	return true;
}

bool EncodePunch(const MacAddress& stMac, std::vector<uint8_t>& vPacket)
{
	Begin(vPacket, kPacketPunch);
	vPacket.insert(vPacket.end(), stMac.vBytes, stMac.vBytes + kMacLength);
	Finish(vPacket);
	return true;
}

bool EncodeFrame(const uint8_t* pFrame, size_t iFrameLength, std::vector<uint8_t>& vPacket)
{
	if (pFrame == NULL || iFrameLength == 0 || iFrameLength > kFrameMax)
	{
		return false;
	}
	Begin(vPacket, kPacketFrame);
	vPacket.insert(vPacket.end(), pFrame, pFrame + iFrameLength);
	Finish(vPacket);
	return true;
}

bool EncodeRelay(const std::string& sRoom, uint32_t iDestinationVirtual, const uint8_t* pFrame, size_t iFrameLength, std::vector<uint8_t>& vPacket)
{
	if (sRoom.empty() || sRoom.size() > kRoomNameMax || pFrame == NULL || iFrameLength == 0 || iFrameLength > kFrameMax)
	{
		return false;
	}
	Begin(vPacket, kPacketRelay);
	vPacket.push_back((uint8_t)sRoom.size());
	vPacket.insert(vPacket.end(), sRoom.begin(), sRoom.end());
	WriteU32(vPacket, iDestinationVirtual);
	vPacket.insert(vPacket.end(), pFrame, pFrame + iFrameLength);
	Finish(vPacket);
	return true;
}

bool EncodeSnapshot(const Snapshot& stSnapshot, std::vector<uint8_t>& vPacket)
{
	if (stSnapshot.vPeers.size() > 255)
	{
		return false;
	}
	Begin(vPacket, kPacketSnapshot);
	WriteU32(vPacket, (uint32_t)stSnapshot.iCode);
	WriteU32(vPacket, stSnapshot.iVirtualIp);
	vPacket.push_back((uint8_t)stSnapshot.vPeers.size());
	for (const PeerView& stPeer : stSnapshot.vPeers)
	{
		vPacket.insert(vPacket.end(), stPeer.stMac.vBytes, stPeer.stMac.vBytes + kMacLength);
		WriteU32(vPacket, stPeer.iSourceIp);
		WriteU16(vPacket, stPeer.iSourcePort);
		WriteU32(vPacket, stPeer.iVirtualIp);
		vPacket.push_back(stPeer.bDirect ? 1 : 0);
	}
	Finish(vPacket);
	return true;
}

bool DecodePacket(const uint8_t* pData, size_t iLength, uint8_t& iType, const uint8_t*& pPayload, size_t& iPayloadLength)
{
	if (pData == NULL || iLength < 8)
	{
		return false;
	}
	uint32_t iMagic = (uint32_t)pData[0] | ((uint32_t)pData[1] << 8) | ((uint32_t)pData[2] << 16) | ((uint32_t)pData[3] << 24);
	if (iMagic != kPacketMagic || pData[4] != kPacketVersion)
	{
		return false;
	}
	iType = pData[5];
	iPayloadLength = (size_t)pData[6] | ((size_t)pData[7] << 8);
	if (iLength != 8 + iPayloadLength)
	{
		return false;
	}
	pPayload = pData + 8;
	return true;
}

bool DecodeJoin(const uint8_t* pPayload, size_t iLength, std::string& sRoom, MacAddress& stMac, uint32_t& iClaimedIp)
{
	if (pPayload == NULL || iLength < 1)
	{
		return false;
	}
	size_t iName = pPayload[0];
	if (iName == 0 || iName > kRoomNameMax || iLength != 1 + iName + kMacLength + 4)
	{
		return false;
	}
	sRoom.assign((const char*)pPayload + 1, iName);
	memcpy(stMac.vBytes, pPayload + 1 + iName, kMacLength);
	size_t iOffset = 1 + iName + kMacLength;
	return ReadU32(pPayload, iLength, iOffset, iClaimedIp);
}

bool DecodeHeartbeat(const uint8_t* pPayload, size_t iLength, std::string& sRoom, MacAddress& stMac, bool& bSawPunch)
{
	if (pPayload == NULL || iLength < 1)
	{
		return false;
	}
	size_t iName = pPayload[0];
	if (iName == 0 || iName > kRoomNameMax || iLength != 1 + iName + kMacLength + 1)
	{
		return false;
	}
	sRoom.assign((const char*)pPayload + 1, iName);
	memcpy(stMac.vBytes, pPayload + 1 + iName, kMacLength);
	bSawPunch = pPayload[iLength - 1] != 0;
	return true;
}

bool DecodePunch(const uint8_t* pPayload, size_t iLength, MacAddress& stMac)
{
	if (pPayload == NULL || iLength != kMacLength)
	{
		return false;
	}
	memcpy(stMac.vBytes, pPayload, kMacLength);
	return true;
}

bool DecodeFrame(const uint8_t* pPayload, size_t iLength, const uint8_t*& pFrame, size_t& iFrameLength)
{
	if (pPayload == NULL || iLength == 0 || iLength > kFrameMax)
	{
		return false;
	}
	pFrame = pPayload;
	iFrameLength = iLength;
	return true;
}

bool DecodeRelay(const uint8_t* pPayload, size_t iLength, std::string& sRoom, uint32_t& iDestinationVirtual, const uint8_t*& pFrame, size_t& iFrameLength)
{
	if (pPayload == NULL || iLength < 1)
	{
		return false;
	}
	size_t iName = pPayload[0];
	if (iName == 0 || iName > kRoomNameMax || iLength < 1 + iName + 4 + 1)
	{
		return false;
	}
	sRoom.assign((const char*)pPayload + 1, iName);
	size_t iOffset = 1 + iName;
	if (!ReadU32(pPayload, iLength, iOffset, iDestinationVirtual))
	{
		return false;
	}
	iFrameLength = iLength - iOffset;
	if (iFrameLength == 0 || iFrameLength > kFrameMax)
	{
		return false;
	}
	pFrame = pPayload + iOffset;
	return true;
}

bool DecodeSnapshot(const uint8_t* pPayload, size_t iLength, Snapshot& stSnapshot)
{
	stSnapshot = Snapshot();
	if (pPayload == NULL || iLength < 9)
	{
		return false;
	}
	size_t iOffset = 0;
	uint32_t iCode = 0;
	if (!ReadU32(pPayload, iLength, iOffset, iCode) || !ReadU32(pPayload, iLength, iOffset, stSnapshot.iVirtualIp))
	{
		return false;
	}
	stSnapshot.iCode = (int32_t)iCode;
	uint8_t iCount = pPayload[iOffset];
	iOffset += 1;
	for (uint8_t iIndex = 0; iIndex < iCount; ++iIndex)
	{
		if (iOffset + kMacLength + 4 + 2 + 4 + 1 > iLength)
		{
			return false;
		}
		PeerView stPeer;
		memcpy(stPeer.stMac.vBytes, pPayload + iOffset, kMacLength);
		iOffset += kMacLength;
		if (!ReadU32(pPayload, iLength, iOffset, stPeer.iSourceIp) || !ReadU16(pPayload, iLength, iOffset, stPeer.iSourcePort) || !ReadU32(pPayload, iLength, iOffset, stPeer.iVirtualIp))
		{
			return false;
		}
		stPeer.bDirect = pPayload[iOffset] != 0;
		iOffset += 1;
		stSnapshot.vPeers.push_back(stPeer);
	}
	return iOffset == iLength;
}

}
