#pragma once
#include <string>

inline std::string QuoteJs(const std::string& sText)
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

inline const char* ClientPageSource()
{
	return R"page(<!DOCTYPE html>
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

inline std::string ClientPage(const std::string& sServer, int iPort, const std::string& sMac)
{
	std::string sPage = ClientPageSource();
	auto fnPut = [&sPage](const char* pKey, const std::string& sValue)
	{
		std::string sToken = pKey;
		size_t iAt = sPage.find(sToken);
		if (iAt != std::string::npos)
		{
			sPage.replace(iAt, sToken.size(), sValue);
		}
	};
	fnPut("__SERVER__", QuoteJs(sServer));
	fnPut("__PORT__", QuoteJs(std::to_string(iPort > 0 ? iPort : 17000)));
	fnPut("__MAC__", QuoteJs(sMac));
	return sPage;
}
