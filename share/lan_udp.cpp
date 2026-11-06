#include "lan_udp.h"
#include "oi_socket.h"
#include "packet.h"

namespace Lan
{

bool LanUdp::SourceOf(sid_t iSessionId, uint32_t& iIp, uint16_t& iPort)
{
	::OI::Net::NetAddr stAddr;
	if (!GetPeerAddr(iSessionId, stAddr) || stAddr.Family() != AF_INET)
	{
		return false;
	}
	const sockaddr_in* pAddr = (const sockaddr_in*)stAddr.NetAttrPtr();
	iIp = ntohl(pAddr->sin_addr.s_addr);
	iPort = stAddr.Port();
	return true;
}

sid_t LanUdp::OpenTo(uint32_t iIp, uint16_t iPort)
{
	return OpenPeer(FormatIpv4(iIp), std::to_string(iPort));
}

}
