#include "roomserver.h"
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
	const char* pConfig = iArgumentCount > 1 ? vArguments[1] : "config/server.toml";
	int iCode = ::OI::Base::StartFramework(pConfig);
	if (iCode != 0)
	{
		return iCode;
	}
	if (!::OI::Log::Init())
	{
		::OI::Log::InitLocal(NULL);
	}
	if (!::OI::Redis::RedisClient::GetInstance().Init())
	{
		fprintf(stderr, "redis client init failed\n");
		return 1;
	}
	::OI::Redis::RedisClient::GetInstance().StartClient();
	if (!RoomServer::GetInstance().Init() || !RoomServer::GetInstance().StartServer())
	{
		fprintf(stderr, "room rpc listen failed\n");
		return 1;
	}
	std::string sIp = "0.0.0.0";
	std::string sPort = "17000";
	::OI::Base::Config::GlobalConfig().GetAs("LanServer.ip", sIp);
	::OI::Base::Config::GlobalConfig().GetAs("LanServer.port", sPort);
	if (!RoomServer::GetInstance().OpenUdp(sIp, (uint16_t)atoi(sPort.c_str())))
	{
		fprintf(stderr, "udp bind failed\n");
		return 1;
	}
	printf("lanroom server listen %s:%u\n", sIp.c_str(), RoomServer::GetInstance().UdpPort());
	fflush(stdout);
	return ::OI::Base::RunFramework();
}
