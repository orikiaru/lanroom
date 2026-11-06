#include "packet.h"
#include <stdio.h>
#include <string.h>

namespace Lan
{

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
	char vText[16];
	snprintf(vText, sizeof(vText), "%u.%u.%u.%u", (iAddress >> 24) & 0xFF, (iAddress >> 16) & 0xFF, (iAddress >> 8) & 0xFF, iAddress & 0xFF);
	return vText;
}

}
