#pragma once
#include <stdint.h>
#include <string>

namespace Lan
{

const size_t kFrameMax = 1200;

class MacAddress
{
	uint8_t m_vBytes[6];

  public:
	static const int kLength = 6;

	MacAddress();
	bool Parse(const std::string& sText);
	bool From(const std::string& sBytes);
	// 当前把流量送出外网的那块网卡。房间用它标识这台电脑。
	bool FromUplink();
	std::string Bytes() const;
	std::string Colon() const;
};

class Ipv4
{
  public:
	static std::string Text(uint32_t iAddress);
	// 255.255.255.255、/24 子网广播和组播，要发给房间里的其他所有人。
	static bool ToAll(uint32_t iAddress);
};

}
