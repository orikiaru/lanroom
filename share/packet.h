#pragma once
#include <stdint.h>
#include <string>

namespace Lan
{

const int kMacLength = 6;
const size_t kFrameMax = 1200;

struct MacAddress
{
	uint8_t vBytes[kMacLength];
};

bool MacEqual(const MacAddress& stLeft, const MacAddress& stRight);
bool ParseMac(const std::string& sText, MacAddress& stMac);
std::string FormatIpv4(uint32_t iAddress);

}
