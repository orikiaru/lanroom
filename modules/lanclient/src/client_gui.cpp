#include "client_gui.h"
#include "lan_addr.h"
#include "room_mgr.h"
#include "roomclient.h"
#include "roomudp.h"
#include "tun_mgr.h"
#include "json/json.h"
#include "oi_app.h"
#include "oi_log.h"
#include "rpc_enums.h"
#include "oi_thread.h"
#include "webview/webview.h"
#include <atomic>
#include <stdio.h>
#include <thread>
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

namespace
{
std::string Quote(const std::string& sText)
{
	std::string sOut;
	sOut.push_back('"');
	for (unsigned char iChar : sText)
	{
		if (iChar == '\\' || iChar == '"')
		{
			sOut.push_back('\\');
		}
		sOut.push_back((char)iChar);
	}
	sOut.push_back('"');
	return sOut;
}

const char* kPage = R"page(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
  body { margin: 0; font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif; background: #eef1f4; color: #1c2430; }
  main { box-sizing: border-box; min-height: 100vh; padding: 28px 32px; }
  h1 { margin: 0 0 4px; font-size: 22px; font-weight: 650; }
  .sub { margin: 0 0 22px; color: #667085; font-size: 13px; }
  label { display: block; margin: 14px 0 6px; font-size: 13px; color: #475467; }
  input { width: 100%; box-sizing: border-box; height: 36px; border: 1px solid #d0d5dd; border-radius: 8px; padding: 0 12px; font: inherit; background: #fff; }
  input:focus { outline: 2px solid #b9d4ff; border-color: #3b82f6; }
  input:disabled { background: #f8fafc; color: #667085; }
  .check { display: flex; align-items: center; gap: 8px; margin-top: 14px; font-size: 13px; color: #475467; }
  .check input { width: auto; height: auto; }
  .split { display: flex; gap: 12px; }
  .split > div { flex: 1; }
  .bar { display: flex; align-items: center; justify-content: space-between; margin-top: 16px; }
  .mac { font-size: 13px; color: #667085; }
  button { height: 36px; padding: 0 18px; border: 0; border-radius: 8px; background: #175cd3; color: #fff; font: inherit; cursor: pointer; }
  button.leave { background: #b42318; }
  button:disabled { background: #98a2b3; cursor: default; }
  .err { min-height: 20px; margin-top: 8px; color: #b42318; font-size: 13px; }
  section { margin-top: 22px; padding: 16px 18px; background: #fff; border: 1px solid #e4e7ec; border-radius: 12px; }
  .state { font-size: 18px; font-weight: 650; }
  .direct { color: #067647; }
  .relay { color: #b54708; }
  .wait { color: #175cd3; }
  .meta { margin-top: 10px; font-size: 13px; line-height: 1.8; color: #344054; white-space: pre-line; }
</style>
</head>
<body>
<main>
  <h1>lanroom</h1>
  <p class="sub">同一个房间里的两台机器，打通后直连，打不通就由服务器转发。</p>
  <label>房间</label>
  <input id="room" autocomplete="off">
  <div class="split">
    <div><label>服务器</label><input id="server" autocomplete="off"></div>
    <div><label>UDP 端口</label><input id="port" inputmode="numeric"></div>
  </div>
  <label class="check"><input id="tcp" type="checkbox">强制 TCP 转发</label>
  <div class="bar">
    <div class="mac" id="mac"></div>
    <button id="join">加入</button>
  </div>
  <div class="err" id="err"></div>
  <section>
    <div class="state" id="state">未加入</div>
    <div class="meta" id="meta"></div>
  </section>
</main>
<script>
const room = document.getElementById("room");
const server = document.getElementById("server");
const port = document.getElementById("port");
const join = document.getElementById("join");
const tcp = document.getElementById("tcp");
const state = document.getElementById("state");
server.value = __SERVER__;
port.value = __PORT__;
document.getElementById("mac").textContent = "本机 MAC  " + __MAC__;
function asObject(raw) {
  if (typeof raw === "string") {
    try { return JSON.parse(raw); } catch (e) { return {}; }
  }
  return raw || {};
}
function paint(v) {
  v = asObject(v);
  state.className = "state";
  state.textContent = v.state || "未加入";
  if (v.state === "直连") state.classList.add("direct");
  else if (v.state === "服务器转发" || v.state === "TCP 转发") state.classList.add("relay");
  else if (v.state === "正在加入" || v.state === "正在连接服务器" || v.state === "房间已满") state.classList.add("wait");
  document.getElementById("meta").textContent =
    "虚拟网卡  " + (v.nic || "-") + "\n控制连接  " + (v.tcp || "-") + "\n转发  " + (v.mode === "tcp" ? "TCP" : (v.mode === "udp" ? "UDP" : "-")) + "\n虚拟地址  " + (v.virt || "-") + "\n成员  " + (v.members || "-") + "\n本机端口  " + (v.local || "-");
  if (v.mode === "tcp") tcp.checked = true;
  const inside = v.state === "直连" || v.state === "服务器转发" || v.state === "TCP 转发" || v.state === "房间里还没有别人";
  room.disabled = inside;
  server.disabled = inside;
  port.disabled = inside;
  tcp.disabled = inside;
  join.textContent = inside ? "退出" : "加入";
  join.className = inside ? "leave" : "";
  join.disabled = v.state === "正在加入" || v.state === "正在连接服务器" || v.state === "正在退出";
}
join.addEventListener("click", async () => {
  document.getElementById("err").textContent = "";
  if (join.textContent === "退出") {
    paint({state: "正在退出"});
    try {
      const err = await window.leave();
      document.getElementById("err").textContent = err || "";
      if (!err) paint({state: "未加入"});
    } catch (e) {
      document.getElementById("err").textContent = String(e);
      paint({state: "未加入"});
    }
    return;
  }
  paint({state: "正在加入"});
  try {
    const err = await window.join(room.value, server.value, port.value, tcp.checked ? "1" : "0");
    document.getElementById("err").textContent = err || "";
    if (err) paint({state: "未加入"});
  } catch (e) {
    document.getElementById("err").textContent = String(e);
    paint({state: "未加入"});
  }
});
setInterval(async () => {
  try {
    paint(await window.roomState());
  } catch (e) {
    document.getElementById("err").textContent = String(e);
  }
}, 400);
paint({state: "未加入"});
</script>
</body>
</html>)page";
}

std::string Page(const std::string& sServer, int iPort, const std::string& sMac)
{
	std::string sPage = kPage;
	auto fnPut = [&sPage](const char* pKey, const std::string& sValue)
	{
		std::string sToken = pKey;
		size_t iAt = sPage.find(sToken);
		if (iAt != std::string::npos)
		{
			sPage.replace(iAt, sToken.size(), sValue);
		}
	};
	fnPut("__SERVER__", Quote(sServer));
	fnPut("__PORT__", Quote(std::to_string(iPort > 0 ? iPort : 17000)));
	fnPut("__MAC__", Quote(sMac));
	return sPage;
}

namespace
{
bool IsElevated()
{
	HANDLE hToken = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
	{
		return false;
	}
	TOKEN_ELEVATION stElevation = {};
	DWORD iSize = 0;
	BOOL bOk = GetTokenInformation(hToken, TokenElevation, &stElevation, sizeof(stElevation), &iSize);
	CloseHandle(hToken);
	return bOk && stElevation.TokenIsElevated;
}

bool RelaunchElevated()
{
	wchar_t vPath[MAX_PATH];
	if (GetModuleFileNameW(NULL, vPath, MAX_PATH) == 0)
	{
		return false;
	}
	int iArgc = 0;
	wchar_t** vArgv = CommandLineToArgvW(GetCommandLineW(), &iArgc);
	std::wstring sArgs;
	for (int iIndex = 1; iIndex < iArgc; ++iIndex)
	{
		if (!sArgs.empty())
		{
			sArgs.push_back(L' ');
		}
		sArgs.push_back(L'"');
		sArgs += vArgv[iIndex];
		sArgs.push_back(L'"');
	}
	if (vArgv != NULL)
	{
		LocalFree(vArgv);
	}
	SHELLEXECUTEINFOW stInfo = {};
	stInfo.cbSize = sizeof(stInfo);
	stInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	stInfo.lpVerb = L"runas";
	stInfo.lpFile = vPath;
	stInfo.lpParameters = sArgs.empty() ? NULL : sArgs.c_str();
	stInfo.nShow = SW_SHOWNORMAL;
	return ShellExecuteExW(&stInfo) == TRUE;
}
}

int ClientGui::Run(const char* pConfig, const std::string& sServer, int iPort, const std::string& sBind)
{
	if (!IsElevated())
	{
		if (RelaunchElevated())
		{
			return 0;
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
						    else if (!RoomUdp::GetInstance().Init(sBindLocal) || !RoomUdp::GetInstance().StartServer())
						    {
							    sResult = "UDP 绑定失败";
						    }
						    else if (!RoomClient::GetInstance().Init())
						    {
							    sResult = "连接初始化失败";
						    }
						    else
						    {
							    RoomClient::GetInstance().UseHost(sHost);
							    RoomClient::GetInstance().StartClient();
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
		stWindow.set_html(Page(sServer, iPort, sMac));
		HWND hConsole = GetConsoleWindow();
		if (hConsole != NULL)
		{
			ShowWindow(hConsole, SW_HIDE);
		}
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
