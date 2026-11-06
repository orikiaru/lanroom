#pragma once
#include "oi_rpc_coro.h"
#include <stdint.h>
#include <string>

namespace Lan
{

// 房间用的 UDP 管理器。一个对端地址一条会话，地址按主机序的 IPv4 进出，和 RoomMember 里存的一样。
class LanUdp : public ::OI::Rpc::ProtoSessionManager
{
  public:
	// 会话对端的 IPv4。会话没了或不是 IPv4 返回 false。
	bool SourceOf(sid_t iSessionId, uint32_t& iIp, uint16_t& iPort);
	// 往 ip:port 发一条单向消息。会话按地址复用，空闲关掉后再发会重开。
	template <typename Msg>
	int32_t NotifyTo(uint32_t iIp, uint16_t iPort, const Msg& stMessage)
	{
		sid_t iSessionId = OpenTo(iIp, iPort);
		if (iSessionId == INVALID_SID)
		{
			return OI_ERR_CONNECT;
		}
		return ::OI::Rpc::RpcNotify(*this, iSessionId, stMessage);
	}

  private:
	sid_t OpenTo(uint32_t iIp, uint16_t iPort);
};

}
