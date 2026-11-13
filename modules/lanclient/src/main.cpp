#include "client_gui.h"
#include "nic_mgr.h"
#include "room_mgr.h"
#include "roomclient.h"
#include "roomudp.h"
#include "oi_app.h"
#include "oi_base.h"
#include "oi_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>

int main(int iArgumentCount, char* vArguments[])
{
	const char* pConfig = "config/client.toml";
	std::string sServer = "120.48.43.244";
	std::string sRoom;
	std::string sMac;
	std::string sBind = "0.0.0.0";
	int iServerPort = 17000;
	int iNicPort = 0;
	bool bDrop = false;
	bool bTcp = false;
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
		else if (sArg == "--tcp")
		{
			bTcp = true;
		}
	}
	if (sRoom.empty())
	{
#ifdef _WIN32
		ClientGui stGui;
		return stGui.Run(pConfig, sServer, iServerPort, sBind);
#else
		fprintf(stderr, "usage: lanroom-client --room name --mac aa:bb:cc:dd:ee:ff --nic-port N [--bind ip] [--server ip] [--server-port N]\n");
		return 2;
#endif
	}
	int iCode = ::OI::Base::StartFramework(pConfig);
	if (iCode != 0)
	{
		return iCode;
	}
	if (::OI::Log::Init() != OI_OK)
	{
		::OI::Log::InitLocal(NULL);
	}
	if (!RoomMgr::GetInstance().Prepare(sRoom, sMac, sServer, (uint16_t)iServerPort, bDrop, bTcp) || !NicMgr::GetInstance().Listen((uint16_t)iNicPort))
	{
		fprintf(stderr, "usage: lanroom-client --room name --mac aa:bb:cc:dd:ee:ff --nic-port N [--bind ip] [--server ip] [--server-port N]\n");
		return 2;
	}
	if ((iCode = RoomUdp::GetInstance().Init(sBind)) != OI_OK || (iCode = RoomUdp::GetInstance().StartServer()) != OI_OK)
	{
		fprintf(stderr, "udp bind failed code=%d\n", iCode);
		return 1;
	}
	printf("lanroom client port %u\n", RoomUdp::GetInstance().LocalPort());
	fflush(stdout);
	if ((iCode = RoomClient::GetInstance().Init()) != OI_OK)
	{
		fprintf(stderr, "RoomClient Init error code=%d\n", iCode);
		return 1;
	}
	RoomClient::GetInstance().UseHost(sServer);
	RoomClient::GetInstance().Start();
	iCode = ::OI::Base::RunFramework();
	RoomMgr::GetInstance().Stop();
	return iCode;
}
