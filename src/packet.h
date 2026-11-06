#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace Lan
{

enum
{
	kPacketMagic = 0x4D524E4Cu,
	kPacketVersion = 1,
	kRoomNameMax = 64,
	kFrameMax = 1200,
	kMacLength = 6
};

enum PacketType
{
	kPacketJoin = 1,
	kPacketSnapshot = 2,
	kPacketHeartbeat = 3,
	kPacketPunch = 4,
	kPacketFrame = 5,
	kPacketRelay = 6
};

enum SnapshotCode
{
	kSnapshotOk = 0,
	kSnapshotBadRoom = 1,
	kSnapshotBadMac = 2
};

struct MacAddress
{
	uint8_t vBytes[kMacLength];
};

struct PeerView
{
	MacAddress stMac;
	uint32_t iSourceIp;
	uint16_t iSourcePort;
	uint32_t iVirtualIp;
	bool bDirect;
};

struct Snapshot
{
	int32_t iCode;
	uint32_t iVirtualIp;
	std::vector<PeerView> vPeers;
};

bool MacEqual(const MacAddress& stLeft, const MacAddress& stRight);
bool ParseMac(const std::string& sText, MacAddress& stMac);
std::string FormatIpv4(uint32_t iAddress);

bool EncodeJoin(const std::string& sRoom, const MacAddress& stMac, uint32_t iClaimedIp, std::vector<uint8_t>& vPacket);
bool EncodeHeartbeat(const std::string& sRoom, const MacAddress& stMac, bool bSawPunch, std::vector<uint8_t>& vPacket);
bool EncodePunch(const MacAddress& stMac, std::vector<uint8_t>& vPacket);
bool EncodeFrame(const uint8_t* pFrame, size_t iFrameLength, std::vector<uint8_t>& vPacket);
bool EncodeRelay(const std::string& sRoom, uint32_t iDestinationVirtual, const uint8_t* pFrame, size_t iFrameLength, std::vector<uint8_t>& vPacket);
bool EncodeSnapshot(const Snapshot& stSnapshot, std::vector<uint8_t>& vPacket);

bool DecodePacket(const uint8_t* pData, size_t iLength, uint8_t& iType, const uint8_t*& pPayload, size_t& iPayloadLength);
bool DecodeJoin(const uint8_t* pPayload, size_t iLength, std::string& sRoom, MacAddress& stMac, uint32_t& iClaimedIp);
bool DecodeHeartbeat(const uint8_t* pPayload, size_t iLength, std::string& sRoom, MacAddress& stMac, bool& bSawPunch);
bool DecodePunch(const uint8_t* pPayload, size_t iLength, MacAddress& stMac);
bool DecodeFrame(const uint8_t* pPayload, size_t iLength, const uint8_t*& pFrame, size_t& iFrameLength);
bool DecodeRelay(const uint8_t* pPayload, size_t iLength, std::string& sRoom, uint32_t& iDestinationVirtual, const uint8_t*& pFrame, size_t& iFrameLength);
bool DecodeSnapshot(const uint8_t* pPayload, size_t iLength, Snapshot& stSnapshot);

}
