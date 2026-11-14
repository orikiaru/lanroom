#pragma once
#include "oi_singleton.h"
#include <stdint.h>
#include <string>
#include <vector>

// 本机虚拟网卡。Windows 用 Wintun，macOS 用 utun，Linux 用 TUN，都是三层 IP 包。
// 从网卡读到的包交给 RoomMgr 发出，房间里收到的包写回网卡。
class TunMgr
{
	DECLARE_SINGLETON(TunMgr)
	TunMgr();

	std::string m_sStatus = "未创建";
	uint32_t m_iAddress = 0;

  public:
	bool Start();
	void Stop();
	bool Write(const std::vector<uint8_t>& vFrame);
	std::string Status() const;

  private:
	void ReadLoop();
	void ApplyAddress();
	bool Open();
	void Close();
};
