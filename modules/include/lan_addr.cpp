#include "lan_addr.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#elif defined(__APPLE__)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#else
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace Lan
{

MacAddress::MacAddress()
{
	memset(m_vBytes, 0, sizeof(m_vBytes));
}

bool MacAddress::Parse(const std::string& sText)
{
	unsigned int vParts[kLength];
	if (sscanf(sText.c_str(), "%x:%x:%x:%x:%x:%x", &vParts[0], &vParts[1], &vParts[2], &vParts[3], &vParts[4], &vParts[5]) != kLength)
	{
		return false;
	}
	for (int iIndex = 0; iIndex < kLength; ++iIndex)
	{
		if (vParts[iIndex] > 0xFF)
		{
			return false;
		}
		m_vBytes[iIndex] = (uint8_t)vParts[iIndex];
	}
	return true;
}

bool MacAddress::From(const std::string& sBytes)
{
	if (sBytes.size() != (size_t)kLength)
	{
		return false;
	}
	memcpy(m_vBytes, sBytes.data(), kLength);
	return true;
}

bool MacAddress::FromUplink()
{
	memset(m_vBytes, 0, sizeof(m_vBytes));
#ifdef _WIN32
	IN_ADDR stDest;
	DWORD iIndex = 0;
	if (inet_pton(AF_INET, "1.1.1.1", &stDest) != 1 || GetBestInterface(stDest.S_un.S_addr, &iIndex) != NO_ERROR)
	{
		return false;
	}
	ULONG iSize = 0;
	if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, NULL, &iSize) != ERROR_BUFFER_OVERFLOW)
	{
		return false;
	}
	std::vector<unsigned char> vBuffer(iSize);
	IP_ADAPTER_ADDRESSES* pList = (IP_ADAPTER_ADDRESSES*)vBuffer.data();
	if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, pList, &iSize) != NO_ERROR)
	{
		return false;
	}
	for (IP_ADAPTER_ADDRESSES* pAdapter = pList; pAdapter != NULL; pAdapter = pAdapter->Next)
	{
		if (pAdapter->IfIndex != iIndex || pAdapter->PhysicalAddressLength != (ULONG)kLength || pAdapter->OperStatus != IfOperStatusUp)
		{
			continue;
		}
		memcpy(m_vBytes, pAdapter->PhysicalAddress, kLength);
		return m_vBytes[0] != 0 || m_vBytes[1] != 0 || m_vBytes[2] != 0 || m_vBytes[3] != 0 || m_vBytes[4] != 0 || m_vBytes[5] != 0;
	}
	return false;
#elif defined(__APPLE__)
	int iRoute = socket(PF_ROUTE, SOCK_RAW, AF_INET);
	if (iRoute < 0)
	{
		return false;
	}
	struct
	{
		rt_msghdr stHeader;
		sockaddr_in stDest;
	} stMessage;
	memset(&stMessage, 0, sizeof(stMessage));
	stMessage.stHeader.rtm_msglen = sizeof(stMessage);
	stMessage.stHeader.rtm_version = RTM_VERSION;
	stMessage.stHeader.rtm_type = RTM_GET;
	stMessage.stHeader.rtm_addrs = RTA_DST;
	stMessage.stHeader.rtm_pid = getpid();
	stMessage.stHeader.rtm_seq = 1;
	stMessage.stDest.sin_len = sizeof(stMessage.stDest);
	stMessage.stDest.sin_family = AF_INET;
	if (inet_pton(AF_INET, "1.1.1.1", &stMessage.stDest.sin_addr) != 1 || write(iRoute, &stMessage, sizeof(stMessage)) != (ssize_t)sizeof(stMessage))
	{
		close(iRoute);
		return false;
	}
	timeval stWait;
	stWait.tv_sec = 1;
	stWait.tv_usec = 0;
	setsockopt(iRoute, SOL_SOCKET, SO_RCVTIMEO, &stWait, sizeof(stWait));
	char vReply[2048];
	unsigned int iIndex = 0;
	bool bRoute = false;
	for (int iTry = 0; iTry < 8; ++iTry)
	{
		ssize_t iCount = read(iRoute, vReply, sizeof(vReply));
		if (iCount < (ssize_t)sizeof(rt_msghdr))
		{
			continue;
		}
		rt_msghdr* pHeader = (rt_msghdr*)vReply;
		if (pHeader->rtm_seq != 1 || pHeader->rtm_pid != getpid())
		{
			continue;
		}
		if (pHeader->rtm_errno != 0 || pHeader->rtm_index == 0)
		{
			break;
		}
		iIndex = pHeader->rtm_index;
		bRoute = true;
		break;
	}
	close(iRoute);
	if (!bRoute)
	{
		return false;
	}
	ifaddrs* pList = NULL;
	if (getifaddrs(&pList) != 0)
	{
		return false;
	}
	bool bOk = false;
	for (ifaddrs* pItem = pList; pItem != NULL; pItem = pItem->ifa_next)
	{
		if (pItem->ifa_addr == NULL || pItem->ifa_addr->sa_family != AF_LINK || if_nametoindex(pItem->ifa_name) != iIndex)
		{
			continue;
		}
		sockaddr_dl* pLink = (sockaddr_dl*)pItem->ifa_addr;
		if (pLink->sdl_alen != (u_char)kLength)
		{
			continue;
		}
		memcpy(m_vBytes, LLADDR(pLink), kLength);
		bOk = m_vBytes[0] != 0 || m_vBytes[1] != 0 || m_vBytes[2] != 0 || m_vBytes[3] != 0 || m_vBytes[4] != 0 || m_vBytes[5] != 0;
		break;
	}
	freeifaddrs(pList);
	return bOk;
#else
	FILE* pRoute = fopen("/proc/net/route", "r");
	if (pRoute == NULL)
	{
		return false;
	}
	char vLine[256];
	char vName[IFNAMSIZ];
	unsigned int iDestination = 0;
	unsigned int iGateway = 0;
	unsigned int iFlags = 0;
	unsigned int iRef = 0;
	unsigned int iUse = 0;
	unsigned int iMetric = 0;
	unsigned int iMask = 0;
	unsigned int iBestMetric = 0xFFFFFFFFu;
	char vBest[IFNAMSIZ];
	memset(vBest, 0, sizeof(vBest));
	if (fgets(vLine, sizeof(vLine), pRoute) == NULL)
	{
		fclose(pRoute);
		return false;
	}
	while (fgets(vLine, sizeof(vLine), pRoute) != NULL)
	{
		if (sscanf(vLine, "%15s %x %x %x %u %u %u %x", vName, &iDestination, &iGateway, &iFlags, &iRef, &iUse, &iMetric, &iMask) < 8)
		{
			continue;
		}
		(void)iGateway;
		(void)iRef;
		(void)iUse;
		if (iDestination == 0 && iMask == 0 && (iFlags & 1u) != 0 && iMetric <= iBestMetric)
		{
			iBestMetric = iMetric;
			snprintf(vBest, sizeof(vBest), "%s", vName);
		}
	}
	fclose(pRoute);
	if (vBest[0] == 0)
	{
		return false;
	}
	int iSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (iSocket < 0)
	{
		return false;
	}
	ifreq stRequest;
	memset(&stRequest, 0, sizeof(stRequest));
	snprintf(stRequest.ifr_name, sizeof(stRequest.ifr_name), "%s", vBest);
	if (ioctl(iSocket, SIOCGIFHWADDR, &stRequest) != 0)
	{
		close(iSocket);
		return false;
	}
	close(iSocket);
	memcpy(m_vBytes, stRequest.ifr_hwaddr.sa_data, kLength);
	return m_vBytes[0] != 0 || m_vBytes[1] != 0 || m_vBytes[2] != 0 || m_vBytes[3] != 0 || m_vBytes[4] != 0 || m_vBytes[5] != 0;
#endif
}

std::string MacAddress::Bytes() const
{
	return std::string((const char*)m_vBytes, kLength);
}

std::string MacAddress::Colon() const
{
	char vText[32];
	snprintf(vText, sizeof(vText), "%02x:%02x:%02x:%02x:%02x:%02x", m_vBytes[0], m_vBytes[1], m_vBytes[2], m_vBytes[3], m_vBytes[4], m_vBytes[5]);
	return vText;
}

std::string Ipv4::Text(uint32_t iAddress)
{
	char vText[16];
	snprintf(vText, sizeof(vText), "%u.%u.%u.%u", (iAddress >> 24) & 0xFF, (iAddress >> 16) & 0xFF, (iAddress >> 8) & 0xFF, iAddress & 0xFF);
	return vText;
}

bool Ipv4::ToAll(uint32_t iAddress)
{
	return iAddress == 0xFFFFFFFFu || (iAddress & 0xFFu) == 0xFFu || (iAddress >> 24) >= 224;
}

}
