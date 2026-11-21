import { useEffect, useRef, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { open } from "@tauri-apps/plugin-dialog";
import "./App.css";

type Phase = "idle" | "joining" | "joined" | "error";

interface Peer {
  mac: string;
  source: string;
  virtualIp: string;
  direct: boolean;
  myself: boolean;
  place: string;
  live: string;
}

interface RoomState {
  phase: Phase;
  virtualIp: string;
  tun: string;
  message: string;
  downloadDir: string;
  peers: Peer[];
}

interface Transfer {
  id: string;
  direction: "send" | "receive";
  name: string;
  peer: string;
  done: number;
  total: number;
  bytesPerSecond: number;
  state: "active" | "done" | "error" | "cancelled";
  message: string;
}

const emptyRoom: RoomState = {
  phase: "idle",
  virtualIp: "",
  tun: "",
  message: "",
  downloadDir: "",
  peers: [],
};

const inTauri = typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

const previewPeers: Peer[] = [
  { mac: "02:11:22:33:44:55", source: "192.168.1.8:53623", virtualIp: "10.66.0.1", direct: true, myself: true, place: "192.168.1.8:53623", live: "本机 · 状态经隧道发出" },
  { mac: "02:aa:bb:cc:dd:ee", source: "203.0.113.9:41002", virtualIp: "10.66.0.2", direct: true, myself: false, place: "203.0.113.9:41002", live: "在线 · 直连 · 隧道 UDP" },
  { mac: "02:10:20:30:40:50", source: "198.51.100.4:22001", virtualIp: "10.66.0.3", direct: false, myself: false, place: "198.51.100.4:22001", live: "在线 · 中转 · 隧道 TCP" },
];

const previewTransfers: Transfer[] = [
  { id: "a", direction: "send", name: "设计稿.pdf", peer: "10.66.0.2", done: 720_000, total: 1_800_000, bytesPerSecond: 240_000, state: "active", message: "" },
  { id: "b", direction: "receive", name: "照片.png", peer: "10.66.0.2", done: 240_000, total: 240_000, bytesPerSecond: 0, state: "done", message: "" },
];

function formatBytes(value: number) {
  if (!value) return "0 B";
  const units = ["B", "KB", "MB", "GB"];
  let size = value;
  let index = 0;
  while (size >= 1024 && index < units.length - 1) {
    size /= 1024;
    index += 1;
  }
  return `${size >= 10 || index === 0 ? size.toFixed(0) : size.toFixed(1)} ${units[index]}`;
}

function App() {
  const [room, setRoom] = useState<RoomState>(emptyRoom);
  const [server, setServer] = useState("49.233.79.122");
  const [port, setPort] = useState("17000");
  const [manageUrl, setManageUrl] = useState("http://49.233.79.122:8080");
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [accountToken, setAccountToken] = useState("");
  const [ownedRooms, setOwnedRooms] = useState<string[]>([]);
  const [roomName, setRoomName] = useState("");
  const [bind, setBind] = useState("");
  const [selected, setSelected] = useState("");
  const [transfers, setTransfers] = useState<Transfer[]>([]);
  const [dragging, setDragging] = useState(false);
  const [notice, setNotice] = useState("");
  const [preview, setPreview] = useState<Phase | "transfer">("idle");
  const selectedRef = useRef("");
  selectedRef.current = selected;

  const shown = inTauri ? room : previewRoom(preview);
  const shownTransfers = inTauri ? transfers : preview === "transfer" ? previewTransfers : [];
  const joined = shown.phase === "joined";
  const joining = shown.phase === "joining";

  useEffect(() => {
    if (!inTauri) return;
    let unlistenRoom: (() => void) | undefined;
    let unlistenTransfer: (() => void) | undefined;
    let unlistenDrop: (() => void) | undefined;
    void (async () => {
      try {
        const dir = await invoke<string>("download_dir");
        setRoom((current) => ({ ...current, downloadDir: dir }));
      } catch {
        /* the command appears after the webview is ready */
      }
      unlistenRoom = await listen<RoomState>("room://state", (event) => {
        setRoom(event.payload);
        setNotice(event.payload.phase === "error" ? event.payload.message : "");
        if (event.payload.phase === "idle") setSelected("");
      });
      unlistenTransfer = await listen<Transfer>("transfer://update", (event) => {
        setTransfers((current) => {
          const next = current.filter((item) => item.id !== event.payload.id);
          return [event.payload, ...next].slice(0, 40);
        });
      });
      const { getCurrentWindow } = await import("@tauri-apps/api/window");
      unlistenDrop = await getCurrentWindow().onDragDropEvent((event) => {
        const payload = event.payload as { type?: string; paths?: string[] };
        if (payload.type === "enter" || payload.type === "over") setDragging(true);
        if (payload.type === "leave") setDragging(false);
        if (payload.type === "drop") {
          setDragging(false);
          void deliver(payload.paths ?? [], selectedRef.current);
        }
      });
    })();
    return () => {
      unlistenRoom?.();
      unlistenTransfer?.();
      unlistenDrop?.();
    };
  }, []);

  async function deliver(paths: string[], peer: string) {
    if (!peer) {
      setNotice("先选择一位成员");
      return;
    }
    if (paths.length === 0) return;
    setNotice("");
    try {
      await invoke("send_files", { peer, paths });
    } catch (err) {
      setNotice(String(err));
    }
  }

  async function accountFetch(path: string, init: RequestInit = {}) {
    const headers = new Headers(init.headers);
    headers.set("Content-Type", "application/json");
    if (accountToken) headers.set("Authorization", `Bearer ${accountToken}`);
    const response = await fetch(`${manageUrl.replace(/\/$/, "")}${path}`, { ...init, headers });
    const body = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(typeof body.detail === "string" ? body.detail : "管理服务没有响应");
    return body;
  }

  async function loginAccount() {
    setNotice("");
    try {
      const body = await accountFetch("/api/login", { method: "POST", body: JSON.stringify({ username, password }) });
      setAccountToken(body.token);
      const rooms = await fetch(`${manageUrl.replace(/\/$/, "")}/api/rooms`, { headers: { Authorization: `Bearer ${body.token}` } }).then((response) => response.json());
      const names = (rooms.items || []).map((item: { name: string }) => item.name);
      setOwnedRooms(names);
      if (names[0]) setRoomName(names[0]);
    } catch (err) {
      setNotice(err instanceof Error ? err.message : String(err));
    }
  }

  async function createOwnedRoom() {
    const name = roomName.trim();
    if (!name) return;
    try {
      await accountFetch("/api/rooms", { method: "POST", body: JSON.stringify({ name }) });
      setOwnedRooms((current) => current.includes(name) ? current : [name, ...current]);
    } catch (err) {
      setNotice(err instanceof Error ? err.message : String(err));
    }
  }

  async function join() {
    setNotice("");
    setTransfers([]);
    if (!accountToken) {
      setNotice("请先登录账号");
      return;
    }
    try {
      const ticket = await accountFetch(`/api/rooms/${encodeURIComponent(roomName)}/ticket`, { method: "POST" });
      await invoke("join_room", {
        server,
        port: Number(port) || 17000,
        room: roomName,
        bind,
        token: ticket.token,
      });
    } catch (err) {
      setNotice(err instanceof Error ? err.message : String(err));
    }
  }

  async function leave() {
    setNotice("");
    await invoke("leave_room");
  }

  async function chooseFiles() {
    const picked = await open({ multiple: true, directory: false });
    if (!picked) return;
    const paths = Array.isArray(picked) ? picked : [picked];
    await deliver(paths, selected);
  }

  async function chooseDir() {
    const picked = await open({ directory: true, multiple: false });
    if (typeof picked !== "string") return;
    await invoke("set_download_dir", { path: picked });
    setRoom((current) => ({ ...current, downloadDir: picked }));
  }

  return (
    <div className="app">
      <header className="titlebar" data-tauri-drag-region>
        <div>
          <p className="brand">Lanroom</p>
          <p className="brand-note">房间里的文件，直接送到对方的虚拟地址</p>
        </div>
        <p className="title-status">{shown.tun || "未连接"}</p>
      </header>
      <div className="body">
        <aside className="rail">
          <label>
            管理地址
            <input value={manageUrl} onChange={(event) => setManageUrl(event.target.value)} disabled={!inTauri || joining || joined} />
          </label>
          <div className="split">
            <label>
              账号
              <input value={username} onChange={(event) => setUsername(event.target.value)} disabled={!inTauri || joining || joined} />
            </label>
            <label>
              密码
              <input type="password" value={password} onChange={(event) => setPassword(event.target.value)} disabled={!inTauri || joining || joined} />
            </label>
          </div>
          <button className="text-button" onClick={() => void loginAccount()} disabled={!inTauri || joining || joined}>{accountToken ? "已登录，刷新房间" : "登录账号"}</button>
          <label>
            服务器
            <input value={inTauri ? server : "49.233.79.122"} onChange={(event) => setServer(event.target.value)} disabled={!inTauri || joining || joined} />
          </label>
          <div className="split">
            <label>
              端口
              <input value={inTauri ? port : "17000"} onChange={(event) => setPort(event.target.value)} disabled={!inTauri || joining || joined} />
            </label>
            <label>
              房间
              <input
                list="owned-rooms"
                value={inTauri ? roomName : joined || joining ? "studio" : ""}
                onChange={(event) => setRoomName(event.target.value)}
                placeholder="选择或输入房间名"
                disabled={!inTauri || joining || joined}
              />
              {ownedRooms.length > 0 && (
                <datalist id="owned-rooms">
                  {ownedRooms.map((name) => <option key={name} value={name} />)}
                </datalist>
              )}
            </label>
          </div>
          <details className="advanced">
            <summary>本机绑定</summary>
            <input value={bind} onChange={(event) => setBind(event.target.value)} placeholder="0.0.0.0" disabled={!inTauri || joining || joined} />
          </details>
          {accountToken && !joined && !joining && (
            <button className="text-button" onClick={() => void createOwnedRoom()} disabled={!inTauri}>用这个名字新建房间</button>
          )}
          <div className="actions">
            {joined || joining ? (
              <button className="ghost" onClick={() => void leave()} disabled={!inTauri}>
                {joining ? "取消" : "离开"}
              </button>
            ) : (
              <button className="primary" onClick={() => void join()} disabled={!inTauri}>
                加入
              </button>
            )}
          </div>
          {shown.virtualIp && (
            <div className="self-card">
              <span>虚拟地址</span>
              <strong>{shown.virtualIp}</strong>
            </div>
          )}
          <div className="member-head">房间成员</div>
          <div className="members">
            {shown.peers.length === 0 && <p className="muted">{joined ? "房间里还没有别人" : "加入后会显示在这里"}</p>}
            {shown.peers.map((peer) => (
              <button key={peer.virtualIp} className={selected === peer.virtualIp ? "member selected" : "member"} onClick={() => setSelected(peer.virtualIp)} disabled={!joined}>
                <span className={peer.live.startsWith("在线") || peer.myself ? "dot on" : "dot"} />
                <span>
                  <strong>{peer.myself ? "本机" : peer.virtualIp}</strong>
                  <small>{peer.place || peer.virtualIp}</small>
                  <small>{peer.live}</small>
                </span>
              </button>
            ))}
          </div>
          <button className="text-button" onClick={() => void chooseDir()} disabled={!inTauri}>
            接收到 {shown.downloadDir || "~/Downloads/Lanroom"}
          </button>
        </aside>
        <main className="stage">
          {!joined && (
            <section className="empty">
              <h1>{joining ? "正在加入房间" : shown.phase === "error" ? "没有连上" : "把文件交给房间里的人"}</h1>
              <p>{shown.message || (joining ? "正在创建虚拟网卡并等待地址。" : "先在左侧填写房间名。加入之后，选择一位成员，再把文件拖进来。")}</p>
            </section>
          )}
          {joined && (
            <>
              <section className={dragging ? "drop hot" : "drop"} onClick={() => void chooseFiles()}>
                <h1>{selected ? `发送给 ${selected}` : "选择一位成员"}</h1>
                <p>{dragging ? "松开即可发送" : "拖入文件，或点击选择。同一位成员的多个文件会按顺序发送。"}</p>
              </section>
              <section className="transfers">
                {shownTransfers.length === 0 && <p className="muted">还没有传输</p>}
                {shownTransfers.map((item) => {
                  const ratio = item.total > 0 ? Math.min(100, (item.done / item.total) * 100) : 0;
                  return (
                    <article key={item.id} className="transfer">
                      <div className="transfer-top">
                        <strong>{item.name}</strong>
                        <span>{item.direction === "send" ? "发送" : "接收"} · {item.peer}</span>
                      </div>
                      <div className="bar"><span style={{ width: `${ratio}%` }} /></div>
                      <div className="transfer-meta">
                        <span>{formatBytes(item.done)} / {formatBytes(item.total)}</span>
                        <span>{item.state === "active" ? `${formatBytes(item.bytesPerSecond)}/s` : labelState(item)}</span>
                        {item.state === "active" && inTauri && (
                          <button onClick={() => void invoke("cancel_transfer", { id: item.id })}>取消</button>
                        )}
                      </div>
                    </article>
                  );
                })}
              </section>
            </>
          )}
          {notice && <p className="notice">{notice}</p>}
          {!inTauri && (
            <div className="preview">
              <span>界面预览</span>
              {(["idle", "joining", "error", "transfer"] as const).map((item) => (
                <button key={item} className={preview === item ? "on" : ""} onClick={() => setPreview(item)}>
                  {item === "idle" ? "未加入" : item === "joining" ? "正在加入" : item === "error" ? "失败" : "传输中"}
                </button>
              ))}
            </div>
          )}
        </main>
      </div>
    </div>
  );
}

function previewRoom(preview: Phase | "transfer"): RoomState {
  if (preview === "idle") return { ...emptyRoom, downloadDir: "~/Downloads/Lanroom" };
  if (preview === "joining") {
    return { ...emptyRoom, phase: "joining", message: "如果系统询问管理员权限，请允许。", downloadDir: "~/Downloads/Lanroom" };
  }
  if (preview === "error") {
    return { ...emptyRoom, phase: "error", message: "虚拟网卡没有启动。请允许管理员权限。", downloadDir: "~/Downloads/Lanroom" };
  }
  return {
    phase: "joined",
    virtualIp: "10.66.0.1",
    tun: "utun12 10.66.0.1/24",
    message: "",
    downloadDir: "~/Downloads/Lanroom",
    peers: previewPeers,
  };
}

function labelState(item: Transfer) {
  if (item.state === "done") return "完成";
  if (item.state === "cancelled") return "已取消";
  return item.message || "失败";
}

export default App;
