#pragma once
#include "oi_channel.h"
#include "oi_lock.h"
#include <stdint.h>
#include <functional>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

namespace Lan
{

// 一条不 connect 的 UDP。发送地址记在通道上。
// Dup 出第二条通道，套接字是 dup，目的地址换成对端。原通道继续收，按来源地址交给对应通道。
class PeerChannel : public ::OI::Net::Channel
{
  public:
	typedef std::function<void(const sockaddr_in& stFrom, const uint8_t* pData, size_t iLength)> FnPacket;

	PeerChannel();
	bool Open(const std::string& sBindIp, uint16_t iPort);
	uint16_t LocalPort() const;
	void SetDest(uint32_t iAddress, uint16_t iPort);
	bool Send(const std::vector<uint8_t>& vPacket);
	bool SendTo(uint32_t iAddress, uint16_t iPort, const std::vector<uint8_t>& vPacket);
	// 重写这条通道的发送地址。收包仍由原通道按来源分。
	PeerChannel* Dup(uint32_t iAddress, uint16_t iPort);
	void SetHandler(FnPacket fnPacket);

	void DataIn() override;
	void DataOut() override;
	bool TryClose() override;

  private:
	bool SameDest(const sockaddr_in& stFrom) const;
	bool SendOn(net_socket_t iDescriptor, uint32_t iAddress, uint16_t iPort, const std::vector<uint8_t>& vPacket);

	FnPacket m_fnPacket;
	::OI::Thread::Mutex m_stLock;
	std::vector<uint8_t> m_vBuffer;
	uint16_t m_iLocalPort;
	uint32_t m_iDestIp;
	uint16_t m_iDestPort;
	PeerChannel* m_pOwner;
	std::vector<PeerChannel*> m_vChild;
};

}
