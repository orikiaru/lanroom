#include "roomserver.h"
#include "roomudp.h"
#include "oi_app.h"
#include "oi_base.h"
#include "oi_config.h"
#include "oi_log.h"
#include "oi_redis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>

int main(int iArgumentCount, char* vArguments[])
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	const char* pConfig = iArgumentCount > 1 ? vArguments[1] : "config/server.toml";
	int iCode = ::OI::Base::StartFramework(pConfig);
	if (iCode != 0)
	{
		return iCode;
	}
	if (::OI::Log::Init() != OI_OK)
	{
		::OI::Log::InitLocal(NULL);
	}
	if ((iCode = ::OI::Redis::RedisClient::GetInstance().Init()) != OI_OK)
	{
		fprintf(stderr, "redis client init failed code=%d\n", iCode);
		return 1;
	}
	::OI::Redis::RedisClient::GetInstance().StartClient();
	if ((iCode = RoomServer::GetInstance().Init()) != OI_OK || (iCode = RoomServer::GetInstance().StartServer()) != OI_OK)
	{
		fprintf(stderr, "room rpc listen failed code=%d\n", iCode);
		return 1;
	}
	if ((iCode = RoomUdp::GetInstance().Init()) != OI_OK || (iCode = RoomUdp::GetInstance().StartServer()) != OI_OK)
	{
		fprintf(stderr, "udp bind failed code=%d\n", iCode);
		return 1;
	}
	std::string sIp;
	::OI::Base::Config::GlobalConfig().GetAs("RoomUdp.ip", sIp);
	printf("lanroom server listen %s:%u\n", sIp.c_str(), RoomUdp::GetInstance().LocalPort());
	fflush(stdout);
	return ::OI::Base::RunFramework();
}
