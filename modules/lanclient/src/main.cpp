#include "roomclient.h"
#include "oi_app.h"
#include "oi_base.h"
#include "oi_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>

int main(int iArgumentCount, char* vArguments[])
{
	const char* pConfig = "config/client.toml";
	std::string sServer = "127.0.0.1";
	std::string sRoom;
	std::string sMac;
	std::string sBind = "0.0.0.0";
	int iServerPort = 17000;
	int iNicPort = 0;
	bool bDrop = false;
	for (int iIndex = 1; iIndex < iArgumentCount; ++iIndex)
	{
		std::string sArg = vArguments[iIndex];
		if (sArg == "--config" && iIndex + 1 < iArgumentCount)
		{
			pConfig = vArguments[++iIndex];
		}
		else if (sArg == "--server" && iIndex + 1 < iArgumentCount)
		{
			sServer = vArguments[++iIndex];
		}
		else if (sArg == "--server-port" && iIndex + 1 < iArgumentCount)
		{
			iServerPort = atoi(vArguments[++iIndex]);
		}
		else if (sArg == "--room" && iIndex + 1 < iArgumentCount)
		{
			sRoom = vArguments[++iIndex];
		}
		else if (sArg == "--mac" && iIndex + 1 < iArgumentCount)
		{
			sMac = vArguments[++iIndex];
		}
		else if (sArg == "--bind" && iIndex + 1 < iArgumentCount)
		{
			sBind = vArguments[++iIndex];
		}
		else if (sArg == "--nic-port" && iIndex + 1 < iArgumentCount)
		{
			iNicPort = atoi(vArguments[++iIndex]);
		}
		else if (sArg == "--drop")
		{
			bDrop = true;
		}
	}
	int iCode = ::OI::Base::StartFramework(pConfig);
	if (iCode != 0)
	{
		return iCode;
	}
	if (!::OI::Log::Init())
	{
		::OI::Log::InitLocal(NULL);
	}
	if (!RoomClient::GetInstance().Prepare(sRoom, sMac, sBind, sServer, (uint16_t)iServerPort, (uint16_t)iNicPort, bDrop))
	{
		fprintf(stderr, "usage: lanroom-client --room name --mac aa:bb:cc:dd:ee:ff --nic-port N [--bind ip] [--server ip] [--server-port N]\n");
		return 2;
	}
	if (!RoomClient::GetInstance().Init())
	{
		fprintf(stderr, "RoomClient Init error\n");
		return 1;
	}
	RoomClient::GetInstance().StartClient();
	RoomClient::GetInstance().StartWork();
	iCode = ::OI::Base::RunFramework();
	RoomClient::GetInstance().JoinWork();
	return iCode;
}
