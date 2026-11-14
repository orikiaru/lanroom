#include "client_gui_session.h"
#include "client_page.h"
#include "lan_addr.h"
#include "room_mgr.h"
#include "roomclient.h"
#include "roomudp.h"
#include "tun_mgr.h"
#include "json/json.h"
#include "oi_app.h"
#include "oi_log.h"
#include "oi_thread.h"
#include "rpc_enums.h"
#include "webview/webview.h"
#include <atomic>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

int RunClientWindow(const char* pConfig, const std::string& sServer, int iPort, const std::string& sBind)
{
	int iCode = ::OI::Base::StartFramework(pConfig);
	if (iCode != 0)
	{
		return iCode;
	}
	if (::OI::Log::Init() != OI_OK)
	{
		::OI::Log::InitLocal(NULL);
	}
	::Lan::MacAddress stHostMac;
	std::string sMac = stHostMac.FromUplink() ? stHostMac.Colon() : "";
	TunMgr::GetInstance().Start();
	std::string sBindLocal = sBind.empty() ? "0.0.0.0" : sBind;
	std::atomic<bool> bJoined{false};
	std::atomic<bool> bOpen{true};
	std::thread stJoin;
	try
	{
		webview::webview stWindow(false, nullptr);
		std::string sTitle = TunMgr::GetInstance().Status();
		size_t iSpace = sTitle.find(' ');
		if (iSpace != std::string::npos)
		{
			sTitle.resize(iSpace);
		}
		stWindow.set_title(sTitle);
		stWindow.set_size(460, 720, WEBVIEW_HINT_NONE);
#ifdef _WIN32
		if (sTitle != "lanroom")
		{
			std::wstring sWide(sTitle.begin(), sTitle.end());
			HWND hWnd = FindWindowW(NULL, sWide.c_str());
			if (hWnd != NULL)
			{
				RECT stRect;
				GetWindowRect(hWnd, &stRect);
				SetWindowPos(hWnd, NULL, 520, 40, stRect.right - stRect.left, stRect.bottom - stRect.top, SWP_NOZORDER);
			}
		}
#endif
		stWindow.bind(
		    "join",
		    [&](const std::string& sId, const std::string& sRequest, void*)
		    {
			    if (stJoin.joinable())
			    {
				    stJoin.join();
			    }
			    stJoin = std::thread([&, sId, sRequest]()
			    {
				    std::string sResult;
				    if (bJoined.load())
				    {
					    Json::CharReaderBuilder stBuilder;
					    std::unique_ptr<Json::CharReader> pReader(stBuilder.newCharReader());
					    Json::Value stArgs;
					    std::string sError;
					    if (!pReader->parse(sRequest.data(), sRequest.data() + sRequest.size(), &stArgs, &sError) || !stArgs.isArray() || stArgs.size() < 3)
					    {
						    sResult = "参数不对";
					    }
					    else
					    {
						    int iJoinPort = atoi(stArgs[2].asString().c_str());
						    bool bTcp = stArgs.size() >= 4 && stArgs[3].asString() == "1";
						    if (iJoinPort <= 0 || iJoinPort > 65535)
						    {
							    sResult = "端口不对";
						    }
						    else if (!RoomMgr::GetInstance().Retarget(stArgs[0].asString(), stArgs[1].asString(), (uint16_t)iJoinPort, bTcp))
						    {
							    sResult = "房间名不能为空";
						    }
						    else
						    {
							    RoomClient::GetInstance().UseHost(stArgs[1].asString());
						    }
					    }
				    }
				    else
				    {
					    Json::CharReaderBuilder stBuilder;
					    std::unique_ptr<Json::CharReader> pReader(stBuilder.newCharReader());
					    Json::Value stArgs;
					    std::string sError;
					    if (!pReader->parse(sRequest.data(), sRequest.data() + sRequest.size(), &stArgs, &sError) || !stArgs.isArray() || stArgs.size() < 3)
					    {
						    sResult = "参数不对";
					    }
					    else
					    {
						std::string sRoom = stArgs[0].asString();
						std::string sHost = stArgs[1].asString();
						int iJoinPort = atoi(stArgs[2].asString().c_str());
						bool bTcp = stArgs.size() >= 4 && stArgs[3].asString() == "1";
						int32_t iInitCode = OI_OK;
						if (sMac.empty())
						{
							sResult = "读不到上网网卡的 MAC";
						}
						else if (iJoinPort <= 0 || iJoinPort > 65535)
						{
							sResult = "端口不对";
						}
						else if (!RoomMgr::GetInstance().Prepare(sRoom, sMac, sHost, (uint16_t)iJoinPort, false, bTcp))
						{
							sResult = "房间名不能为空";
						}
						else if ((iInitCode = RoomUdp::GetInstance().Init(sBindLocal)) != OI_OK || (iInitCode = RoomUdp::GetInstance().StartServer()) != OI_OK)
						{
							sResult = "UDP 绑定失败 code=" + std::to_string(iInitCode);
						}
						else if ((iInitCode = RoomClient::GetInstance().Init()) != OI_OK)
						{
							sResult = "连接初始化失败 code=" + std::to_string(iInitCode);
						}
						    else
						    {
							    RoomClient::GetInstance().UseHost(sHost);
							    RoomClient::GetInstance().Start();
							    bJoined.store(true);
						    }
					    }
				    }
				    if (bOpen.load())
				    {
					    stWindow.resolve(sId, 0, sResult);
				    }
			    });
		    },
		    nullptr);
		stWindow.bind("leave", [&](const std::string&) -> std::string
		{
			RoomMgr::GetInstance().RequestLeave();
			return "";
		});
		stWindow.bind("roomState", [&](const std::string&) -> std::string
		{
			RoomMgr::View stView = RoomMgr::GetInstance().Current();
			const char* pState = "未加入";
			if (stView.bLeaving)
			{
				pState = "正在退出";
			}
			else if (!stView.bArmed && !stView.bJoined)
			{
				pState = "未加入";
			}
			else if (bJoined.load() && !RoomClient::GetInstance().IsConnect())
			{
				pState = "正在连接服务器";
			}
			else if (bJoined.load() && RoomMgr::GetInstance().JoinCode() == ::OI::RpcData::ROOM_ERR_FULL)
			{
				pState = "房间已满";
			}
			else if (bJoined.load() && !stView.bJoined)
			{
				pState = "正在加入";
			}
			else if (stView.bJoined && stView.sPeer.empty())
			{
				pState = "房间里还没有别人";
			}
			else if (stView.bTcp)
			{
				pState = "TCP 转发";
			}
			else if (stView.bDirect)
			{
				pState = "直连";
			}
			else if (stView.bJoined)
			{
				pState = "服务器转发";
			}
			Json::Value stStatus(Json::objectValue);
			stStatus["state"] = pState;
			stStatus["virt"] = stView.bJoined ? ::Lan::Ipv4::Text(stView.iVirtualIp) : "-";
			stStatus["peer"] = stView.sPeer.empty() ? "-" : stView.sPeer;
			stStatus["members"] = stView.sPeer.empty() ? "-" : stView.sPeer;
			stStatus["local"] = bJoined.load() ? std::to_string(RoomUdp::GetInstance().LocalPort()) : "-";
			stStatus["tcp"] = bJoined.load() ? RoomClient::GetInstance().GetStrIP() + ":" + RoomClient::GetInstance().GetStrPort() : "-";
			stStatus["nic"] = TunMgr::GetInstance().Status();
			stStatus["mode"] = !stView.bJoined ? "" : (stView.bTcp ? "tcp" : "udp");
			Json::StreamWriterBuilder stWriter;
			stWriter["indentation"] = "";
			return Json::writeString(stWriter, stStatus);
		});
		stWindow.set_html(ClientPage(sServer, iPort, sMac));
#ifdef _WIN32
		HWND hConsole = GetConsoleWindow();
		if (hConsole != NULL)
		{
			ShowWindow(hConsole, SW_HIDE);
		}
#endif
		std::thread stFramework([]()
		{
			::OI::Base::RunFramework();
		});
		stWindow.run();
		bOpen.store(false);
		if (stJoin.joinable())
		{
			stJoin.join();
		}
		TunMgr::GetInstance().Stop();
		RoomMgr::GetInstance().Stop();
		::OI::Thread::ThreadPool::GetInstance().RequestStop(0);
		if (stFramework.joinable())
		{
			stFramework.join();
		}
	}
	catch (const webview::exception& stError)
	{
		fprintf(stderr, "webview: %s\n", stError.what());
		return 1;
	}
	return 0;
}
