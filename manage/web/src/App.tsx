import { FormEvent, Suspense, lazy, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import {
  Activity,
  Check,
  ChevronLeft,
  ChevronRight,
  Compass,
  Copy,
  Crosshair,
  DoorOpen,
  Globe,
  KeyRound,
  Layers,
  LogOut,
  Map as MapIcon,
  MapPin,
  Minus,
  Network,
  Pause,
  Play,
  Plus,
  Radio,
  RefreshCw,
  ScrollText,
  Search,
  Server,
  Shield,
  Sparkles,
  UserPlus,
  Users,
  Wifi,
  WifiOff,
  X,
  Zap,
} from "lucide-react";
import {
  api,
  can,
  setToken,
  token,
  type LoginLog,
  type MapMember,
  type Member,
  type Role,
  type User,
} from "./api";
import { Globe3D, type Globe3DHandle, type SelectedEntity } from "./Globe3D";
import type { DetailMapStyle, ZoomTelemetry } from "./globeTiles";
import { useLivePanel, type LiveEvent, type LivePanelController, type WsStatus } from "./ws";

const LazyMapView = lazy(() => import("./MapView").then((m) => ({ default: m.MapView })));
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { Checkbox } from "@/components/ui/checkbox";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import { Textarea } from "@/components/ui/textarea";

type Page = "map" | "rooms" | "users" | "roles" | "logs" | "login";
type Session = "checking" | "in" | "out";

function pageFromHash(): Page {
  const name = location.hash.replace("#/", "") as Page;
  return ["map", "rooms", "users", "roles", "logs", "login"].includes(name) ? name : "map";
}

function when(value: string) {
  return value.replace("T", " ").slice(0, 19);
}

function roleBadgeStyle(role: string) {
  if (role === "admin") return "border-violet-500/40 bg-violet-500/15 text-violet-200";
  if (role === "operator") return "border-cyan-500/40 bg-cyan-500/15 text-cyan-200";
  return "border-slate-500/35 bg-slate-500/15 text-slate-200";
}

function modeBadge(mode?: string) {
  if (mode === "direct") {
    return (
      <span className="inline-flex items-center gap-1.5 rounded-full border border-emerald-500/40 bg-emerald-500/15 px-2.5 py-0.5 text-xs font-medium text-emerald-300">
        <span className="h-1.5 w-1.5 rounded-full bg-emerald-400" />
        P2P 直连
      </span>
    );
  }
  if (mode === "relay") {
    return (
      <span className="inline-flex items-center gap-1.5 rounded-full border border-amber-500/40 bg-amber-500/15 px-2.5 py-0.5 text-xs font-medium text-amber-300">
        <span className="h-1.5 w-1.5 rounded-full bg-amber-400" />
        服务器中转
      </span>
    );
  }
  if (mode === "standby") {
    return (
      <span className="inline-flex items-center gap-1.5 rounded-full border border-sky-500/40 bg-sky-500/15 px-2.5 py-0.5 text-xs font-medium text-sky-300">
        <span className="h-1.5 w-1.5 rounded-full bg-sky-400" />
        单人待机
      </span>
    );
  }
  return (
    <span className="inline-flex items-center gap-1.5 rounded-full border border-slate-600/40 bg-slate-700/20 px-2.5 py-0.5 text-xs text-slate-400">
      空闲
    </span>
  );
}

function WsStatusPill({ status, rttMs, compact = false }: { status: WsStatus; rttMs: number | null; compact?: boolean }) {
  const isLive = status === "live";
  const isRetry = status === "reconnecting" || status === "connecting";
  const dotClass = isLive
    ? "bg-emerald-400 text-emerald-400"
    : isRetry
      ? "bg-amber-400 text-amber-400"
      : "bg-rose-400 text-rose-400";
  const pillClass = isLive
    ? "border-emerald-500/35 bg-emerald-500/12 text-emerald-200"
    : isRetry
      ? "border-amber-500/35 bg-amber-500/12 text-amber-200"
      : "border-rose-500/35 bg-rose-500/12 text-rose-200";
  const label = isLive ? "WS 实时" : status === "reconnecting" ? "WS 重连中" : status === "connecting" ? "WS 连接中" : "WS 离线";

  return (
    <span className={`inline-flex items-center gap-1.5 rounded-full border px-2 py-0.5 text-[10px] font-medium ${pillClass}`}>
      <span className={`live-dot ${dotClass}`} style={{ width: 6, height: 6 }} />
      <span>{label}</span>
      {!compact && isLive && rttMs != null && <span className="mono-num text-emerald-300/90">{rttMs}ms</span>}
    </span>
  );
}

export function App() {
  const saved = token();
  const [user, setUser] = useState<User | null>(null);
  const [page, setPage] = useState<Page>(saved ? pageFromHash() : "login");
  const [error, setError] = useState("");
  const [session, setSession] = useState<Session>(saved ? "checking" : "out");

  const wsEnabled = session === "in" && !!user && page !== "login";
  const live = useLivePanel(wsEnabled);

  useEffect(() => {
    const onHash = () => {
      setError("");
      setPage(pageFromHash());
    };
    window.addEventListener("hashchange", onHash);
    if (!token()) {
      setSession("out");
      return () => window.removeEventListener("hashchange", onHash);
    }
    api
      .me()
      .then((next) => {
        setUser(next);
        setSession("in");
        if (pageFromHash() === "login") location.hash = "#/map";
      })
      .catch(() => {
        setUser(null);
        setSession("out");
        setPage("login");
      });
    return () => window.removeEventListener("hashchange", onHash);
  }, []);

  if (session === "checking") {
    return (
      <div className="login-grid-bg grid min-h-screen place-items-center">
        <div className="flex items-center gap-3 text-sm text-slate-300">
          <RefreshCw className="h-4 w-4 animate-spin text-cyan-400" />
          正在连接 Lanroom 控制台...
        </div>
      </div>
    );
  }

  if (session === "out" || !user || page === "login") {
    return (
      <Login
        onUser={(next) => {
          setUser(next);
          setSession("in");
        }}
      />
    );
  }

  const links: { id: Page; label: string; desc: string; code?: string; icon: typeof Globe }[] = [
    { id: "map", label: "3D 全球拓扑", desc: "地球视图与实时链路", code: "map:read", icon: Globe },
    { id: "rooms", label: "房间管理", desc: "虚拟网段与票据", code: "room:read", icon: DoorOpen },
    { id: "users", label: "用户账号", desc: "成员与启停控制", code: "user:read", icon: Users },
    { id: "roles", label: "角色权限", desc: "RBAC 访问策略", code: "role:read", icon: Shield },
    { id: "logs", label: "登录审计", desc: "来源 IP 与归属地", code: "user:read", icon: ScrollText },
  ];

  function logout() {
    void api.logout();
    setToken("");
    setUser(null);
    setSession("out");
    location.hash = "#/login";
  }

  const isMapPage = page === "map";

  return (
    <div className="flex min-h-screen bg-[#030712] text-slate-100">
      {/* Sleek Obsidian Sidebar */}
      <aside className="flex w-60 shrink-0 flex-col border-r border-slate-800/75 bg-[#050b16]">
        <div className="flex items-center gap-3 border-b border-slate-800/70 px-4 py-4">
          <div className="grid h-10 w-10 place-items-center rounded-xl border border-cyan-400/35 bg-gradient-to-br from-cyan-500/25 via-sky-500/15 to-violet-500/25 shadow-[0_0_24px_rgba(56,189,248,0.22)]">
            <Network className="h-5 w-5 text-cyan-300" />
          </div>
          <div className="min-w-0">
            <div className="flex items-center gap-1.5">
              <span className="text-sm font-semibold tracking-[0.14em] text-white">LANROOM</span>
              <span className="rounded bg-cyan-500/15 px-1.5 py-0.5 text-[10px] font-medium text-cyan-300">WS·3D</span>
            </div>
            <p className="truncate text-[11px] text-slate-400">全球虚拟局域网控制台</p>
          </div>
        </div>

        <nav className="flex-1 space-y-1 px-2.5 py-3.5">
          {links
            .filter((link) => !link.code || can(user, link.code))
            .map((link) => {
              const Icon = link.icon;
              const active = page === link.id;
              return (
                <a
                  key={link.id}
                  href={`#/${link.id}`}
                  className={`group relative flex items-center gap-3 rounded-xl px-3 py-2.5 text-sm transition-all ${
                    active
                      ? "bg-gradient-to-r from-cyan-500/18 to-sky-500/6 text-white shadow-[inset_0_0_0_1px_rgba(56,189,248,0.28)]"
                      : "text-slate-400 hover:bg-slate-800/45 hover:text-slate-100"
                  }`}
                >
                  {active && (
                    <span className="absolute left-0 top-2 bottom-2 w-1 rounded-r-full bg-cyan-400 shadow-[0_0_10px_#38bdf8]" />
                  )}
                  <Icon className={`h-4 w-4 shrink-0 ${active ? "text-cyan-300" : "text-slate-500 group-hover:text-slate-300"}`} />
                  <div className="min-w-0">
                    <div className="font-medium leading-tight">{link.label}</div>
                    <div className="truncate text-[11px] text-slate-500 group-hover:text-slate-400">{link.desc}</div>
                  </div>
                </a>
              );
            })}
        </nav>

        {/* Live WebSocket Channel Telemetry Card */}
        <div className="px-3 pb-2.5">
          <div className="rounded-xl border border-slate-800/80 bg-[#081122] px-3 py-2.5">
            <div className="flex items-center justify-between gap-2">
              <div className="flex items-center gap-1.5 text-[11px] font-medium text-slate-300">
                {live.status === "live" ? (
                  <Wifi className="h-3.5 w-3.5 text-emerald-400" />
                ) : (
                  <WifiOff className="h-3.5 w-3.5 text-amber-400" />
                )}
                <span>实时通道</span>
              </div>
              <WsStatusPill status={live.status} rttMs={live.rttMs} compact />
            </div>
            <div className="mono-num mt-1.5 flex items-center justify-between text-[10px] text-slate-400">
              <span>{live.rttMs != null ? `RTT ${live.rttMs} ms` : "等待握手"}</span>
              <span>{live.lastSync ? `同步 ${live.lastSync}` : "—"}</span>
            </div>
          </div>
        </div>

        {/* Bottom User Profile Card */}
        <div className="border-t border-slate-800/70 p-3">
          <div className="flex items-center justify-between gap-2 rounded-xl border border-slate-800/80 bg-[#091224] p-2.5">
            <div className="min-w-0">
              <div className="flex items-center gap-1.5">
                <span className="truncate text-sm font-medium text-slate-100">{user.displayName || user.username}</span>
                <span className={`rounded-full border px-1.5 py-0.5 text-[10px] font-medium ${roleBadgeStyle(user.role)}`}>
                  {user.role}
                </span>
              </div>
              <p className="mono-num mt-0.5 truncate text-[11px] text-slate-500">{user.username}</p>
            </div>
            <Button
              variant="ghost"
              size="icon"
              title="退出登录"
              onClick={logout}
              className="h-8 w-8 shrink-0 text-slate-400 hover:bg-rose-500/15 hover:text-rose-300"
            >
              <LogOut className="h-4 w-4" />
            </Button>
          </div>
        </div>
      </aside>

      {/* Main Content */}
      <div className="flex min-w-0 flex-1 flex-col">
        {isMapPage ? (
          <main className="relative h-screen w-full overflow-hidden">
            <MapPage user={user} live={live} onError={setError} />
          </main>
        ) : (
          <main className="flex-1 overflow-y-auto bg-[radial-gradient(ellipse_at_top_right,rgba(56,189,248,0.06),transparent_45%),#030712] p-6 lg:p-8">
            <div className="mx-auto max-w-6xl">
              {error && (
                <div className="mb-5 flex items-center justify-between rounded-xl border border-rose-500/40 bg-rose-500/12 px-4 py-3 text-sm text-rose-200">
                  <span>{error}</span>
                  <button onClick={() => setError("")} className="text-rose-300 hover:text-white">
                    <X className="h-4 w-4" />
                  </button>
                </div>
              )}
              {page === "rooms" && <RoomsPage user={user} live={live} onError={setError} />}
              {page === "users" && <UsersPage onError={setError} />}
              {page === "roles" && <RolesPage onError={setError} />}
              {page === "logs" && <LogsPage onError={setError} />}
            </div>
          </main>
        )}
      </div>
    </div>
  );
}

function Login({ onUser }: { onUser: (user: User) => void }) {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError("");
    setBusy(true);
    try {
      const body = await api.login(username, password);
      setToken(body.token);
      onUser(body.user);
      location.hash = "#/map";
    } catch (err) {
      setError(err instanceof Error ? err.message : "登录失败");
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="login-grid-bg relative grid min-h-screen place-items-center overflow-hidden p-4">
      {/* Subtle ambient orbital rings */}
      <div className="pointer-events-none absolute h-[520px] w-[520px] rounded-full border border-cyan-500/10" />
      <div className="pointer-events-none absolute h-[760px] w-[760px] rounded-full border border-violet-500/10" />

      <div className="hud-glass relative z-10 w-full max-w-md rounded-2xl p-8">
        <form onSubmit={(event) => void submit(event)} className="grid gap-5">
          <div className="flex items-center gap-3.5">
            <div className="grid h-12 w-12 place-items-center rounded-2xl border border-cyan-400/40 bg-gradient-to-br from-cyan-500/30 via-sky-500/15 to-violet-500/30 shadow-[0_0_30px_rgba(56,189,248,0.28)]">
              <Globe className="h-6 w-6 text-cyan-300" />
            </div>
            <div>
              <p className="text-xs font-semibold tracking-[0.2em] text-cyan-400">LANROOM TELEMETRY</p>
              <h1 className="mt-0.5 text-2xl font-semibold text-white">登录网络控制台</h1>
            </div>
          </div>

          <p className="text-xs leading-relaxed text-slate-400">
            实时观测全球节点分布、3D P2P 直连与中转通信拓扑、房间票据与访问权限。
          </p>

          <div className="grid gap-2">
            <Label htmlFor="username" className="text-xs text-slate-300">
              账号
            </Label>
            <Input
              id="username"
              placeholder="输入管理员或成员账号"
              value={username}
              onChange={(event) => setUsername(event.target.value)}
              autoComplete="username"
              className="h-11 border-slate-700/70 bg-[#070f20] text-slate-100 placeholder:text-slate-500 focus-visible:border-cyan-400"
            />
          </div>

          <div className="grid gap-2">
            <Label htmlFor="password" className="text-xs text-slate-300">
              密码
            </Label>
            <Input
              id="password"
              type="password"
              placeholder="••••••••"
              value={password}
              onChange={(event) => setPassword(event.target.value)}
              autoComplete="current-password"
              className="h-11 border-slate-700/70 bg-[#070f20] text-slate-100 placeholder:text-slate-500 focus-visible:border-cyan-400"
            />
          </div>

          {error && (
            <div className="rounded-xl border border-rose-500/40 bg-rose-500/15 px-3.5 py-2.5 text-xs text-rose-200">
              {error}
            </div>
          )}

          <Button
            type="submit"
            disabled={busy}
            className="h-11 w-full bg-gradient-to-r from-cyan-500 to-sky-500 font-medium text-slate-950 shadow-[0_0_24px_rgba(56,189,248,0.35)] hover:from-cyan-400 hover:to-sky-400"
          >
            {busy ? "正在验证..." : "进入控制台"}
          </Button>
        </form>
      </div>
    </div>
  );
}

function PageHead({ title, subtitle, children }: { title: string; subtitle?: string; children?: ReactNode }) {
  return (
    <div className="mb-6 flex flex-wrap items-end justify-between gap-4">
      <div>
        <h1 className="text-2xl font-semibold tracking-tight text-white">{title}</h1>
        {subtitle && <p className="mt-1 text-sm text-slate-400">{subtitle}</p>}
      </div>
      <div className="flex flex-wrap items-center gap-2">{children}</div>
    </div>
  );
}

function StatCard({
  label,
  value,
  accent,
  icon: Icon,
}: {
  label: string;
  value: string | number;
  accent: string;
  icon: typeof Activity;
}) {
  return (
    <div className="panel-card flex items-center justify-between rounded-2xl px-4 py-3.5">
      <div>
        <p className="text-xs text-slate-400">{label}</p>
        <p className="mono-num mt-1 text-2xl font-semibold text-white">{value}</p>
      </div>
      <div className={`grid h-10 w-10 place-items-center rounded-xl border ${accent}`}>
        <Icon className="h-4 w-4" />
      </div>
    </div>
  );
}

const REGION_PRESETS: { id: string; label: string; lat?: number; lon?: number; dist?: number }[] = [
  { id: "fit", label: "全景" },
  { id: "apac", label: "亚太", lat: 26, lon: 112, dist: 2.38 },
  { id: "eu", label: "欧洲", lat: 50, lon: 10, dist: 2.35 },
  { id: "na", label: "北美", lat: 38, lon: -96, dist: 2.45 },
  { id: "server", label: "主节点" },
];

function MapPage({
  user,
  live,
  onError,
}: {
  user: User;
  live: LivePanelController;
  onError: (msg: string) => void;
}) {
  const globeRef = useRef<Globe3DHandle>(null);

  const [viewMode, setViewMode] = useState<"3d" | "2d">("3d");
  const [logins, setLogins] = useState(false);
  const [autoRotate, setAutoRotate] = useState(true);
  const [showControlLinks, setShowControlLinks] = useState(true);
  const [detailMapStyle, setDetailMapStyle] = useState<DetailMapStyle>("zh");
  const [zoomInfo, setZoomInfo] = useState<ZoomTelemetry>({ zoom: 3, scaleLabel: "全球视野", tilesActive: false });
  const [selected, setSelected] = useState<SelectedEntity | null>(null);
  const [showList, setShowList] = useState(true);
  const [showFeed, setShowFeed] = useState(true);
  const [kickBusy, setKickBusy] = useState(false);

  const canReadLogs = can(user, "user:read");
  const isLoginMode = logins && canReadLogs;

  const mapData = live.mapData;
  const server = mapData?.server ?? null;
  const rooms = mapData?.rooms ?? [];
  const rawMembers = mapData?.members ?? [];
  const links = mapData?.links ?? [];
  const cities = mapData?.cities ?? [];
  const countries = mapData?.countries ?? [];

  const loginMembers = useMemo<MapMember[]>(
    () =>
      (mapData?.logins ?? []).map((item, idx) => ({
        id: `login:${idx}:${item.username}:${item.ip}`,
        room: "登录",
        owner: item.username,
        mac: item.result,
        virtualIp: item.createdAt,
        source: item.ip,
        country: item.country,
        city: item.city,
        lat: item.lat,
        lon: item.lon,
      })),
    [mapData?.logins],
  );

  const activeMembers = isLoginMode ? loginMembers : rawMembers;
  const activeCities = isLoginMode ? [] : cities;
  const activeCountries = isLoginMode ? [] : countries;

  // Keep selected entity automatically synchronized with live WS snapshots
  const liveSelected = useMemo<SelectedEntity | null>(() => {
    if (!selected) return null;
    if (selected.kind === "server") {
      return server ? { kind: "server", server } : selected;
    }
    if (selected.kind === "member") {
      const fresh = activeMembers.find((m) => m.id && m.id === selected.member.id);
      return fresh ? { kind: "member", member: fresh } : selected;
    }
    const freshLink = links.find((l) => l.id === selected.link.id);
    return freshLink ? { kind: "link", link: freshLink } : selected;
  }, [selected, server, activeMembers, links]);

  const stats = useMemo(() => {
    const directLinks = links.filter((l) => l.kind === "direct").length;
    const relayLinks = links.filter((l) => l.kind === "relay").length;
    const activeRooms = new Set(rawMembers.map((m) => m.room)).size;
    return {
      nodes: rawMembers.length,
      rooms: activeRooms,
      directLinks,
      relayLinks,
    };
  }, [rawMembers, links]);

  const selectedId = useMemo(() => {
    if (!liveSelected) return null;
    if (liveSelected.kind === "server") return "server";
    if (liveSelected.kind === "member") return liveSelected.member.id ?? null;
    return liveSelected.link.id;
  }, [liveSelected]);

  async function kickSelectedMember(member: MapMember) {
    setKickBusy(true);
    try {
      await live.action("kick", { room: member.room, mac: member.mac });
      setSelected(null);
    } catch (err) {
      onError(err instanceof Error ? err.message : String(err));
    } finally {
      setKickBusy(false);
    }
  }

  function focusEvent(ev: LiveEvent) {
    const found = rawMembers.find((m) => m.id === ev.memberId);
    if (found) {
      setSelected({ kind: "member", member: found });
      if (found.lat != null && found.lon != null) {
        globeRef.current?.focusLatLon(found.lat, found.lon, 2.2);
      }
    }
  }

  return (
    <div className="relative h-full w-full overflow-hidden bg-[#030712]">
      {/* Main 3D Globe or 2D Tactical Map Canvas */}
      {viewMode === "3d" ? (
        <Globe3D
          ref={globeRef}
          server={server}
          members={activeMembers}
          links={links}
          countries={activeCountries}
          isLoginMode={isLoginMode}
          autoRotate={autoRotate}
          showControlLinks={showControlLinks}
          detailMapStyle={detailMapStyle}
          selectedId={selectedId}
          recentJoinIds={live.recentJoinIds}
          onZoomChange={setZoomInfo}
          onSelect={setSelected}
        />
      ) : (
        <Suspense
          fallback={
            <div className="grid h-full w-full place-items-center bg-[#040a17] text-xs text-slate-400">
              正在加载 2D 战术平面图...
            </div>
          }
        >
          <LazyMapView
            server={server}
            countries={activeCountries}
            cities={activeCities}
            members={activeMembers}
            links={links}
            isLoginMode={isLoginMode}
            showControlLinks={showControlLinks}
            onSelect={setSelected}
          />
        </Suspense>
      )}

      {/* Top Floating HUD Row */}
      <div className="pointer-events-none absolute inset-x-4 top-4 z-20 flex flex-wrap items-start justify-between gap-2.5">
        {/* Compact Left Telemetry Pill */}
        <div className="hud-glass pointer-events-auto flex flex-wrap items-center gap-3.5 rounded-2xl px-4 py-2.5">
          <div>
            <div className="flex items-center gap-2">
              <span className="text-xs font-semibold tracking-wider text-slate-100">
                {isLoginMode ? "全球登录溯源" : "全球实时拓扑"}
              </span>
              <WsStatusPill status={live.status} rttMs={live.rttMs} />
            </div>
            <p className="mt-0.5 text-[11px] text-slate-400">
              {server
                ? `主节点 ${server.city || server.ip} · ${server.ip}`
                : "拖拽旋转地球 · 点击节点或 3D 飞线查看遥测"}
            </p>
          </div>
          <div className="hidden h-7 w-px bg-slate-700/70 md:block" />
          <div className="flex items-center gap-3.5">
            <div>
              <div className="text-[10px] text-slate-400">在线节点</div>
              <div className="mono-num text-base font-semibold leading-tight text-cyan-300">{stats.nodes}</div>
            </div>
            <div>
              <div className="text-[10px] text-slate-400">活跃房间</div>
              <div className="mono-num text-base font-semibold leading-tight text-white">{stats.rooms}</div>
            </div>
            <div>
              <div className="text-[10px] text-slate-400">P2P 直连</div>
              <div className="mono-num text-base font-semibold leading-tight text-emerald-300">{stats.directLinks}</div>
            </div>
            <div>
              <div className="text-[10px] text-slate-400">中转链路</div>
              <div className="mono-num text-base font-semibold leading-tight text-amber-300">{stats.relayLinks}</div>
            </div>
          </div>
        </div>

        {/* Right View Controls */}
        <div className="pointer-events-auto flex flex-wrap items-center justify-end gap-2">
          <div className="hud-glass flex items-center gap-1.5 rounded-2xl p-1.5">
            <Select
              value={live.roomFilter || "all"}
              onValueChange={(value) => live.setRoomFilter(value === "all" ? "" : value)}
            >
              <SelectTrigger className="h-8 w-32 border-slate-700/60 bg-slate-900/75 text-xs text-slate-100">
                <SelectValue placeholder="全部房间" />
              </SelectTrigger>
              <SelectContent className="border-slate-700 bg-[#0b1326] text-slate-100">
                <SelectItem value="all">全部房间</SelectItem>
                {rooms.map((name) => (
                  <SelectItem key={name} value={name}>
                    {name}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>

            <div className="flex rounded-xl border border-slate-700/60 bg-slate-900/80 p-0.5 text-xs">
              <button
                onClick={() => setViewMode("3d")}
                className={`flex items-center gap-1 rounded-lg px-2.5 py-1 font-medium transition ${
                  viewMode === "3d" ? "bg-cyan-500/25 text-cyan-200 shadow-sm" : "text-slate-400 hover:text-slate-200"
                }`}
              >
                <Globe className="h-3.5 w-3.5" />
                3D 地球
              </button>
              <button
                onClick={() => setViewMode("2d")}
                className={`flex items-center gap-1 rounded-lg px-2.5 py-1 font-medium transition ${
                  viewMode === "2d" ? "bg-cyan-500/25 text-cyan-200 shadow-sm" : "text-slate-400 hover:text-slate-200"
                }`}
              >
                <MapIcon className="h-3.5 w-3.5" />
                2D 平面
              </button>
            </div>

            {canReadLogs && (
              <Button
                size="sm"
                variant="ghost"
                onClick={() => {
                  setLogins((v) => !v);
                  setSelected(null);
                }}
                className={`h-8 rounded-xl px-2.5 text-xs ${
                  isLoginMode
                    ? "bg-violet-500/25 text-violet-200 hover:bg-violet-500/35"
                    : "text-slate-300 hover:bg-slate-800/80 hover:text-white"
                }`}
              >
                <Sparkles className="mr-1 h-3.5 w-3.5" />
                {isLoginMode ? "节点拓扑" : "登录溯源"}
              </Button>
            )}
          </div>

          {/* 3D Camera & Layer Micro Controls */}
          <div className="hud-glass flex items-center gap-1 rounded-2xl p-1.5">
            {viewMode === "3d" && (
              <>
                <Button
                  size="icon"
                  variant="ghost"
                  title={
                    detailMapStyle === "zh"
                      ? "当前：暗黑中文详图（点击切换卫星实景）"
                      : detailMapStyle === "satellite"
                        ? "当前：卫星实景底图（点击切换暗黑全球）"
                        : detailMapStyle === "dark"
                          ? "当前：暗黑全球底图（点击切换纯矢量）"
                          : "当前：纯矢量地球（点击开启中文详图）"
                  }
                  onClick={() =>
                    setDetailMapStyle((prev) =>
                      prev === "zh" ? "satellite" : prev === "satellite" ? "dark" : prev === "dark" ? "off" : "zh",
                    )
                  }
                  className={`h-8 w-8 rounded-xl ${
                    detailMapStyle === "satellite"
                      ? "bg-amber-500/20 text-amber-300"
                      : detailMapStyle === "zh" || detailMapStyle === "dark"
                        ? "bg-emerald-500/20 text-emerald-300"
                        : "text-slate-400 hover:text-white"
                  }`}
                >
                  <Layers className="h-3.5 w-3.5" />
                </Button>
                <Button
                  size="icon"
                  variant="ghost"
                  title={autoRotate ? "暂停地球自转" : "开启地球自转"}
                  onClick={() => setAutoRotate((v) => !v)}
                  className={`h-8 w-8 rounded-xl ${autoRotate ? "bg-cyan-500/20 text-cyan-300" : "text-slate-400 hover:text-white"}`}
                >
                  {autoRotate ? <Pause className="h-3.5 w-3.5" /> : <Play className="h-3.5 w-3.5" />}
                </Button>
                <Button
                  size="icon"
                  variant="ghost"
                  title="聚焦服务器主节点"
                  onClick={() => {
                    globeRef.current?.focusServer();
                    if (server) setSelected({ kind: "server", server });
                  }}
                  className="h-8 w-8 rounded-xl text-violet-300 hover:bg-violet-500/20 hover:text-violet-200"
                >
                  <Server className="h-3.5 w-3.5" />
                </Button>
                <Button
                  size="icon"
                  variant="ghost"
                  title="适配全部节点视角"
                  onClick={() => globeRef.current?.fitAll()}
                  className="h-8 w-8 rounded-xl text-slate-300 hover:bg-slate-800 hover:text-white"
                >
                  <Compass className="h-3.5 w-3.5" />
                </Button>
              </>
            )}
            <Button
              size="icon"
              variant="ghost"
              title={showControlLinks ? "隐藏心跳控制线" : "显示心跳控制线"}
              onClick={() => setShowControlLinks((v) => !v)}
              className={`h-8 w-8 rounded-xl ${showControlLinks ? "bg-sky-500/20 text-sky-300" : "text-slate-400 hover:text-white"}`}
            >
              <Radio className="h-3.5 w-3.5" />
            </Button>
          </div>
        </div>
      </div>

      {/* Bottom-Left Floating Node List & Legend */}
      <div className="absolute bottom-4 left-4 z-20 flex w-72 max-w-[calc(100%-2rem)] flex-col gap-2">
        {showList && (
          <div className="hud-glass flex max-h-64 flex-col rounded-2xl p-3">
            <div className="mb-2 flex items-center justify-between">
              <div className="flex items-center gap-1.5">
                <span className="text-xs font-medium text-slate-200">
                  {isLoginMode ? "近期登录节点" : "在线节点与主服"}
                </span>
                <span className="mono-num rounded-full bg-cyan-500/15 px-1.5 py-0.5 text-[10px] text-cyan-300">
                  {activeMembers.length}
                </span>
              </div>
              <button onClick={() => setShowList(false)} className="text-[11px] text-slate-400 hover:text-slate-200">
                收起
              </button>
            </div>
            <div className="space-y-1 overflow-y-auto pr-1 text-xs">
              {server && !isLoginMode && (
                <button
                    onClick={() => {
                      setSelected({ kind: "server", server });
                      if (server.lat != null && server.lon != null) {
                        globeRef.current?.focusLatLon(server.lat, server.lon, 1.32);
                      }
                    }}
                  className="flex w-full items-center justify-between rounded-xl border border-violet-500/30 bg-violet-500/12 px-2.5 py-2 text-left transition hover:bg-violet-500/22"
                >
                  <div className="min-w-0">
                    <div className="flex items-center gap-1.5 font-medium text-violet-200">
                      <Server className="h-3.5 w-3.5 shrink-0" />
                      <span className="truncate">{server.name}</span>
                    </div>
                    <div className="mono-num mt-0.5 truncate text-[10px] text-violet-300/75">
                      {server.city || "核心机房"} · {server.ip}
                    </div>
                  </div>
                  <Crosshair className="h-3.5 w-3.5 shrink-0 text-violet-300/70" />
                </button>
              )}

              {activeMembers.length === 0 ? (
                <p className="py-3 text-center text-[11px] text-slate-400">
                  {isLoginMode ? "暂无登录位置记录" : "等待各地节点上线..."}
                </p>
              ) : (
                activeMembers.slice(0, 32).map((m, idx) => {
                  const key = m.id || `${m.room}:${m.mac}:${idx}`;
                  const isFresh = Boolean(m.id && live.recentJoinIds.includes(m.id));
                  const dotColor = isLoginMode
                    ? m.mac === "ok"
                      ? "bg-cyan-400"
                      : "bg-rose-400"
                    : m.mode === "direct"
                      ? "bg-emerald-400"
                      : m.mode === "relay"
                        ? "bg-amber-400"
                        : "bg-sky-400";
                  return (
                    <button
                      key={key}
                      onClick={() => {
                        setSelected({ kind: "member", member: m });
                        if (m.lat != null && m.lon != null) {
                          globeRef.current?.focusLatLon(m.lat, m.lon, 1.22);
                        }
                      }}
                      className={`flex w-full items-center justify-between gap-2 rounded-xl px-2.5 py-1.5 text-left transition ${
                        selectedId === key
                          ? "bg-cyan-500/20 text-white shadow-[inset_0_0_0_1px_rgba(56,189,248,0.35)]"
                          : isFresh
                            ? "bg-emerald-500/15 text-emerald-100"
                            : "text-slate-300 hover:bg-slate-800/70 hover:text-white"
                      }`}
                    >
                      <div className="flex min-w-0 items-center gap-2">
                        <span className={`h-2 w-2 shrink-0 rounded-full ${dotColor}`} />
                        <div className="min-w-0">
                          <div className="mono-num flex items-center gap-1.5 truncate text-[11px] font-medium">
                            <span className="truncate">{isLoginMode ? m.owner : `${m.room} · ${m.virtualIp}`}</span>
                            {isFresh && (
                              <span className="rounded bg-emerald-400/20 px-1 text-[9px] text-emerald-300">NEW</span>
                            )}
                          </div>
                          <div className="truncate text-[10px] text-slate-400">
                            {m.city || "未定位"} {m.country} · {m.source}
                          </div>
                        </div>
                      </div>
                      <MapPin className="h-3 w-3 shrink-0 text-slate-500" />
                    </button>
                  );
                })
              )}
            </div>
          </div>
        )}

        {/* Compact Legend Strip */}
        <div className="hud-glass flex flex-wrap items-center justify-between gap-x-3 gap-y-1.5 rounded-2xl px-3.5 py-2 text-[11px] text-slate-300">
          <div className="flex flex-wrap items-center gap-x-3 gap-y-1">
            <span className="inline-flex items-center gap-1.5">
              <span className="h-2.5 w-2.5 rounded-sm bg-violet-400 shadow-[0_0_8px_#a855f7]" />
              服务器
            </span>
            <span className="inline-flex items-center gap-1.5">
              <span className="h-2 w-2 rounded-full bg-emerald-400 shadow-[0_0_8px_#10b981]" />
              P2P 直连
            </span>
            <span className="inline-flex items-center gap-1.5">
              <span className="h-2 w-2 rounded-full bg-amber-400 shadow-[0_0_8px_#f59e0b]" />
              中转通信
            </span>
            <span className="inline-flex items-center gap-1.5">
              <span className="h-2 w-2 rounded-full bg-sky-400" />
              控制心跳
            </span>
          </div>
          {!showList && (
            <button onClick={() => setShowList(true)} className="text-cyan-300 hover:text-cyan-200">
              节点列表
            </button>
          )}
        </div>
      </div>

      {/* Bottom-Center Quick 3D Camera Region & Multi-Zoom Detail Bar */}
      {viewMode === "3d" && (
        <div className="pointer-events-none absolute bottom-4 left-1/2 z-20 hidden -translate-x-1/2 items-center gap-2 lg:flex">
          <div className="hud-pill pointer-events-auto flex items-center gap-1 rounded-full p-1">
            {REGION_PRESETS.map((preset) => (
              <button
                key={preset.id}
                onClick={() => {
                  if (preset.id === "fit") globeRef.current?.fitAll();
                  else if (preset.id === "server") globeRef.current?.focusServer();
                  else if (preset.lat != null && preset.lon != null) {
                    globeRef.current?.focusLatLon(preset.lat, preset.lon, preset.dist ?? 1.85);
                  }
                }}
                className="rounded-full px-3 py-1 text-[11px] font-medium text-slate-300 transition hover:bg-cyan-500/20 hover:text-cyan-200"
              >
                {preset.label}
              </button>
            ))}
            <div className="mx-1 h-4 w-px bg-slate-700/70" />
            <button
              title="缩小视野"
              onClick={() => globeRef.current?.zoomOut()}
              className="grid h-6 w-6 place-items-center rounded-full text-slate-300 transition hover:bg-slate-800 hover:text-white"
            >
              <Minus className="h-3.5 w-3.5" />
            </button>
            <span className="mono-num px-1.5 text-[10px] font-medium text-cyan-300">
              Z{zoomInfo.zoom} · {zoomInfo.scaleLabel}
            </span>
            <button
              title="放大查看更细区块"
              onClick={() => globeRef.current?.zoomIn()}
              className="grid h-6 w-6 place-items-center rounded-full text-slate-300 transition hover:bg-cyan-500/20 hover:text-cyan-200"
            >
              <Plus className="h-3.5 w-3.5" />
            </button>
            <div className="mx-1 h-4 w-px bg-slate-700/70" />
            <button
              title="切换细分区块底图风格"
              onClick={() =>
                setDetailMapStyle((prev) =>
                  prev === "zh" ? "satellite" : prev === "satellite" ? "dark" : prev === "dark" ? "off" : "zh",
                )
              }
              className={`rounded-full px-2.5 py-1 text-[10px] font-medium transition ${
                detailMapStyle === "satellite"
                  ? "bg-amber-500/20 text-amber-200"
                  : detailMapStyle === "zh" || detailMapStyle === "dark"
                    ? "bg-emerald-500/18 text-emerald-200"
                    : "text-slate-400 hover:text-slate-200"
              }`}
            >
              {detailMapStyle === "zh"
                ? "中文详图"
                : detailMapStyle === "satellite"
                  ? "卫星实景"
                  : detailMapStyle === "dark"
                    ? "暗黑全球"
                    : "纯矢量"}
            </button>
          </div>
        </div>
      )}

      {/* Bottom-Right Live WebSocket Activity Ticker (when Inspector Drawer is closed) */}
      {!liveSelected && live.events.length > 0 && (
        <div className="absolute bottom-4 right-4 z-20 w-72 max-w-[calc(100%-2rem)]">
          {showFeed ? (
            <div className="hud-glass rounded-2xl p-3">
              <div className="mb-2 flex items-center justify-between">
                <div className="flex items-center gap-1.5 text-xs font-medium text-slate-200">
                  <Zap className="h-3.5 w-3.5 text-cyan-400" />
                  <span>实时动态</span>
                </div>
                <button onClick={() => setShowFeed(false)} className="text-[11px] text-slate-400 hover:text-slate-200">
                  收起
                </button>
              </div>
              <div className="max-h-40 space-y-1.5 overflow-y-auto pr-1 text-[11px]">
                {live.events.slice(0, 6).map((ev) => {
                  const badge =
                    ev.kind === "join"
                      ? "border-emerald-500/40 bg-emerald-500/15 text-emerald-300"
                      : ev.kind === "leave"
                        ? "border-rose-500/40 bg-rose-500/15 text-rose-300"
                        : "border-amber-500/40 bg-amber-500/15 text-amber-300";
                  const label = ev.kind === "join" ? "上线" : ev.kind === "leave" ? "离线" : "切换";
                  return (
                    <button
                      key={ev.id}
                      onClick={() => focusEvent(ev)}
                      className="animate-event-in flex w-full items-center justify-between gap-2 rounded-xl border border-slate-800/70 bg-slate-900/55 px-2.5 py-1.5 text-left transition hover:border-cyan-500/35 hover:bg-slate-800/70"
                    >
                      <div className="min-w-0">
                        <div className="flex items-center gap-1.5">
                          <span className={`rounded border px-1 py-0.2 text-[9px] font-medium ${badge}`}>{label}</span>
                          <span className="mono-num truncate font-medium text-slate-100">
                            {ev.virtualIp || ev.room}
                          </span>
                        </div>
                        <div className="truncate text-[10px] text-slate-400">
                          {ev.room} · {ev.city || "未定位"} {ev.country}
                        </div>
                      </div>
                      <span className="mono-num shrink-0 text-[10px] text-slate-500">{ev.ts.slice(11, 19)}</span>
                    </button>
                  );
                })}
              </div>
            </div>
          ) : (
            <button
              onClick={() => setShowFeed(true)}
              className="hud-pill ml-auto flex items-center gap-1.5 rounded-full px-3 py-1.5 text-[11px] text-cyan-200 hover:border-cyan-400/40"
            >
              <Zap className="h-3 w-3 text-cyan-400" />
              实时动态 ({live.events.length})
            </button>
          )}
        </div>
      )}

      {/* Right Slide-Over Glass Telemetry Inspector Drawer */}
      {liveSelected && (
        <aside className="hud-glass absolute bottom-4 right-4 top-24 z-30 flex w-80 max-w-[calc(100%-2rem)] flex-col overflow-hidden rounded-2xl">
          <div className="flex items-center justify-between border-b border-slate-700/60 px-4 py-3.5">
            <div className="flex items-center gap-2">
              {liveSelected.kind === "server" && <Server className="h-4 w-4 text-violet-300" />}
              {liveSelected.kind === "member" && <MapPin className="h-4 w-4 text-cyan-300" />}
              {liveSelected.kind === "link" && <Layers className="h-4 w-4 text-emerald-300" />}
              <h2 className="text-sm font-semibold text-white">
                {liveSelected.kind === "server"
                  ? "服务器主节点"
                  : liveSelected.kind === "link"
                    ? "3D 通信链路详情"
                    : liveSelected.member.room === "登录"
                      ? "登录溯源详情"
                      : "房间节点遥测"}
              </h2>
            </div>
            <button
              onClick={() => setSelected(null)}
              className="rounded-lg p-1 text-slate-400 transition hover:bg-slate-800 hover:text-white"
            >
              <X className="h-4 w-4" />
            </button>
          </div>

          <div className="flex-1 space-y-4 overflow-y-auto p-4 text-xs">
            {liveSelected.kind === "server" && (
              <>
                <div className="rounded-xl border border-violet-500/30 bg-violet-500/10 p-3">
                  <div className="text-sm font-semibold text-violet-100">{liveSelected.server.name}</div>
                  <div className="mono-num mt-1 text-violet-300">{liveSelected.server.ip}</div>
                </div>
                <dl className="grid gap-2.5">
                  <InfoRow label="地理位置" value={`${liveSelected.server.city || "未知城市"} ${liveSelected.server.country || ""}`} />
                  <InfoRow
                    label="经纬度"
                    mono
                    value={
                      liveSelected.server.lat != null && liveSelected.server.lon != null
                        ? `${liveSelected.server.lat.toFixed(4)}, ${liveSelected.server.lon.toFixed(4)}`
                        : "未配置"
                    }
                  />
                  <InfoRow label="服务端口" mono value="17000/UDP · 17001/TCP" />
                  <InfoRow label="当前承载中转" mono value={`${stats.relayLinks} 条中转链路`} />
                  <InfoRow label="当前在线节点" mono value={`${stats.nodes} 个节点`} />
                </dl>
              </>
            )}

            {liveSelected.kind === "member" && (
              <>
                <div className="flex items-center justify-between rounded-xl border border-slate-700/60 bg-slate-900/60 p-3">
                  <div>
                    <div className="text-sm font-semibold text-white">
                      {liveSelected.member.room === "登录" ? liveSelected.member.owner : liveSelected.member.room}
                    </div>
                    <div className="mono-num mt-0.5 text-cyan-300">
                      {liveSelected.member.room === "登录" ? liveSelected.member.source : liveSelected.member.virtualIp}
                    </div>
                  </div>
                  {liveSelected.member.room !== "登录" ? (
                    modeBadge(liveSelected.member.mode)
                  ) : (
                    <Badge variant={liveSelected.member.mac === "ok" ? "secondary" : "destructive"}>
                      {liveSelected.member.mac === "ok" ? "成功" : liveSelected.member.mac === "logout" ? "退出" : "失败"}
                    </Badge>
                  )}
                </div>

                <dl className="grid gap-2.5">
                  {liveSelected.member.room === "登录" ? (
                    <>
                      <InfoRow label="登录账号" value={liveSelected.member.owner} />
                      <InfoRow label="来源地址" mono value={liveSelected.member.source} />
                      <InfoRow label="记录时间" mono value={when(liveSelected.member.virtualIp)} />
                    </>
                  ) : (
                    <>
                      <InfoRow label="房间拥有者" value={liveSelected.member.owner} />
                      <InfoRow label="虚拟地址" mono value={liveSelected.member.virtualIp || "—"} />
                      <InfoRow label="设备 MAC" mono value={liveSelected.member.mac} />
                      <InfoRow label="公网出口" mono value={liveSelected.member.source} />
                      <InfoRow
                        label="隧道模式"
                        value={
                          liveSelected.member.tcpRelay
                            ? "强制 TCP 中转"
                            : liveSelected.member.direct
                              ? "UDP 打洞直连"
                              : "自动中转 / 等待打洞"
                        }
                      />
                      <InfoRow label="累计中转帧" mono value={String(liveSelected.member.relayCount ?? 0)} />
                    </>
                  )}
                  <InfoRow
                    label="GeoIP 归属"
                    value={`${liveSelected.member.city || "未定位"} ${liveSelected.member.country || ""}`.trim()}
                  />
                  <InfoRow
                    label="坐标"
                    mono
                    value={
                      liveSelected.member.lat != null && liveSelected.member.lon != null
                        ? `${liveSelected.member.lat.toFixed(4)}, ${liveSelected.member.lon.toFixed(4)}`
                        : "—"
                    }
                  />
                </dl>

                <div className="flex flex-col gap-2 pt-2">
                  {liveSelected.member.lat != null && liveSelected.member.lon != null && viewMode === "3d" && (
                    <Button
                      size="sm"
                      variant="outline"
                      onClick={() => globeRef.current?.focusLatLon(liveSelected.member.lat!, liveSelected.member.lon!, 1.12)}
                      className="w-full border-slate-700 bg-slate-900/70 text-slate-200 hover:bg-slate-800"
                    >
                      <Crosshair className="mr-1.5 h-3.5 w-3.5 text-cyan-400" />
                      飞镜聚焦该节点
                    </Button>
                  )}
                  {liveSelected.member.room !== "登录" && can(user, "member:kick") && (
                    <Button
                      size="sm"
                      variant="destructive"
                      disabled={kickBusy}
                      onClick={() => void kickSelectedMember(liveSelected.member)}
                      className="w-full"
                    >
                      {kickBusy ? "正在移出..." : "移出该在线成员"}
                    </Button>
                  )}
                </div>
              </>
            )}

            {liveSelected.kind === "link" && (
              <>
                <div className="rounded-xl border border-slate-700/60 bg-slate-900/60 p-3">
                  <div className="mb-2">
                    {modeBadge(liveSelected.link.kind === "direct" ? "direct" : "relay")}
                  </div>
                  <div className="mono-num text-sm font-semibold text-white">
                    {liveSelected.link.from.label} ↔ {liveSelected.link.to.label}
                  </div>
                </div>
                <dl className="grid gap-2.5">
                  <InfoRow label="所属房间" value={liveSelected.link.room} />
                  <InfoRow
                    label="通信路径"
                    value={liveSelected.link.kind === "direct" ? "Node ↔ Node（UDP 穿透直连）" : "Node ↔ Server（TCP 中继转发）"}
                  />
                  <InfoRow
                    label="起点坐标"
                    mono
                    value={`${liveSelected.link.from.lat.toFixed(3)}, ${liveSelected.link.from.lon.toFixed(3)}`}
                  />
                  <InfoRow
                    label="终点坐标"
                    mono
                    value={`${liveSelected.link.to.lat.toFixed(3)}, ${liveSelected.link.to.lon.toFixed(3)}`}
                  />
                  {liveSelected.link.kind === "relay" && (
                    <InfoRow label="房间累计中转" mono value={`${liveSelected.link.relayCount ?? 0} 帧`} />
                  )}
                </dl>
                {viewMode === "3d" && (
                  <div className="grid grid-cols-2 gap-2 pt-2">
                    <Button
                      size="sm"
                      variant="outline"
                      onClick={() => globeRef.current?.focusLatLon(liveSelected.link.from.lat, liveSelected.link.from.lon, 2.2)}
                      className="border-slate-700 bg-slate-900/70 text-xs"
                    >
                      聚焦起点
                    </Button>
                    <Button
                      size="sm"
                      variant="outline"
                      onClick={() => globeRef.current?.focusLatLon(liveSelected.link.to.lat, liveSelected.link.to.lon, 2.2)}
                      className="border-slate-700 bg-slate-900/70 text-xs"
                    >
                      聚焦终点
                    </Button>
                  </div>
                )}
              </>
            )}
          </div>
        </aside>
      )}
    </div>
  );
}

function InfoRow({ label, value, mono }: { label: string; value: string; mono?: boolean }) {
  return (
    <div className="flex items-start justify-between gap-3 border-b border-slate-800/70 pb-2">
      <dt className="shrink-0 text-slate-400">{label}</dt>
      <dd className={`text-right text-slate-100 break-all ${mono ? "mono-num" : ""}`}>{value || "—"}</dd>
    </div>
  );
}

function RoomsPage({
  user,
  live,
  onError,
}: {
  user: User;
  live: LivePanelController;
  onError: (value: string) => void;
}) {
  const rooms = live.rooms;
  const [name, setName] = useState("");
  const [q, setQ] = useState(live.roomQuery);
  const [ticket, setTicket] = useState("");
  const [copied, setCopied] = useState(false);
  const [membersRoom, setMembersRoom] = useState<string | null>(null);
  const [members, setMembers] = useState<(Member & { room: string })[]>([]);

  async function loadMembers(roomName: string) {
    onError("");
    const res = await live.action<{ room: string; items: Member[] }>("room:members", { name: roomName });
    setMembersRoom(roomName);
    setMembers((res.items ?? []).map((item) => ({ room: roomName, ...item })));
  }

  // Keep open member dialog synchronized with live WS snapshots
  useEffect(() => {
    if (!membersRoom || !live.mapData?.members) return;
    const filtered = live.mapData.members
      .filter((m) => m.room === membersRoom)
      .map((m) => ({
        room: m.room,
        mac: m.mac,
        virtualIp: m.virtualIp,
        source: m.source,
        ip: m.ip,
        direct: m.direct,
        tcpRelay: m.tcpRelay,
        relayCount: m.relayCount,
      }));
    setMembers(filtered);
  }, [membersRoom, live.mapData?.members]);

  const totalOnline = rooms.reduce((acc, r) => acc + (r.onlineCount ?? 0), 0);
  const activeCount = rooms.filter((r) => (r.onlineCount ?? 0) > 0).length;

  return (
    <>
      <PageHead title="房间管理" subtitle="WebSocket 实时同步虚拟局域网房间、在线成员与 12 小时加入票据">
        <WsStatusPill status={live.status} rttMs={live.rttMs} />
        <div className="relative">
          <Search className="pointer-events-none absolute left-3 top-2.5 h-4 w-4 text-slate-500" />
          <Input
            className="w-48 border-slate-700/70 bg-[#0b1324] pl-9 text-sm"
            placeholder="搜索房间名"
            value={q}
            onChange={(event) => {
              const val = event.target.value;
              setQ(val);
              live.setRoomQuery(val);
            }}
          />
        </div>
        <Button variant="outline" className="border-slate-700/70 bg-[#0b1324]" onClick={() => live.refresh()}>
          <RefreshCw className="mr-1.5 h-3.5 w-3.5" />
          同步
        </Button>
      </PageHead>

      <div className="mb-6 grid gap-3 sm:grid-cols-3">
        <StatCard label="开放房间" value={rooms.length} accent="border-cyan-500/35 bg-cyan-500/15 text-cyan-300" icon={DoorOpen} />
        <StatCard label="活跃房间" value={activeCount} accent="border-emerald-500/35 bg-emerald-500/15 text-emerald-300" icon={Activity} />
        <StatCard label="在线节点总数" value={totalOnline} accent="border-violet-500/35 bg-violet-500/15 text-violet-300" icon={Network} />
      </div>

      {can(user, "room:write") && (
        <form
          className="panel-card mb-5 flex flex-wrap items-center gap-3 rounded-2xl p-4"
          onSubmit={(event) => {
            event.preventDefault();
            if (!name.trim()) return;
            void live
              .action("room:create", { name })
              .then(() => {
                onError("");
                setName("");
                if (q) {
                  setQ("");
                  live.setRoomQuery("");
                }
              })
              .catch((err) => onError(err instanceof Error ? err.message : "创建失败"));
          }}
        >
          <Input
            className="max-w-xs border-slate-700/70 bg-[#060d1c]"
            placeholder="输入新房间名（不能包含冒号）"
            value={name}
            onChange={(event) => setName(event.target.value)}
          />
          <Button type="submit" className="bg-cyan-500 text-slate-950 hover:bg-cyan-400">
            <Plus className="mr-1.5 h-4 w-4" />
            创建房间
          </Button>
        </form>
      )}

      <Card className="panel-card overflow-hidden rounded-2xl border-slate-800/80">
        <Table>
          <TableHeader>
            <TableRow className="border-slate-800/80 hover:bg-transparent">
              <TableHead className="text-slate-400">房间名</TableHead>
              <TableHead className="text-slate-400">拥有者</TableHead>
              <TableHead className="text-slate-400">在线节点</TableHead>
              <TableHead className="text-slate-400">链路状态</TableHead>
              <TableHead className="text-slate-400">创建时间</TableHead>
              <TableHead className="text-right text-slate-400">操作</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {rooms.length === 0 && (
              <TableRow>
                <TableCell colSpan={6} className="py-10 text-center text-sm text-slate-500">
                  暂无开放中的房间
                </TableCell>
              </TableRow>
            )}
            {rooms.map((room) => (
              <TableRow key={room.name} className="border-slate-800/70 hover:bg-slate-800/35">
                <TableCell className="font-medium text-white">{room.name}</TableCell>
                <TableCell className="text-slate-300">{room.owner}</TableCell>
                <TableCell className="mono-num text-cyan-300">{room.onlineCount ?? 0}</TableCell>
                <TableCell>{modeBadge(room.mode)}</TableCell>
                <TableCell className="mono-num text-xs text-slate-400">{when(room.createdAt)}</TableCell>
                <TableCell>
                  <div className="flex justify-end gap-2">
                    <Button
                      variant="outline"
                      size="sm"
                      className="border-slate-700/70 bg-slate-900/60"
                      onClick={() => void loadMembers(room.name).catch((err) => onError(err instanceof Error ? err.message : String(err)))}
                    >
                      成员
                    </Button>
                    {(room.mine || can(user, "room:manage")) && (
                      <Button
                        variant="outline"
                        size="sm"
                        className="border-cyan-500/35 bg-cyan-500/10 text-cyan-200 hover:bg-cyan-500/20"
                        onClick={() =>
                          void live
                            .action<{ token: string; expiresAt?: string }>("room:ticket", { name: room.name })
                            .then((body) => {
                              onError("");
                              setCopied(false);
                              setTicket(body.token);
                            })
                            .catch((err) => onError(err instanceof Error ? err.message : String(err)))
                        }
                      >
                        <KeyRound className="mr-1 h-3.5 w-3.5" />
                        票据
                      </Button>
                    )}
                    {(room.mine || can(user, "room:manage")) && (
                      <Button
                        variant="destructive"
                        size="sm"
                        onClick={() =>
                          void live
                            .action("room:close", { name: room.name })
                            .catch((err) => onError(err instanceof Error ? err.message : String(err)))
                        }
                      >
                        关闭
                      </Button>
                    )}
                  </div>
                </TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </Card>

      <Dialog
        open={ticket !== ""}
        onOpenChange={(open) => {
          if (!open) setTicket("");
        }}
      >
        <DialogContent className="border-slate-700/70 bg-[#0b1324] text-slate-100">
          <DialogHeader>
            <DialogTitle>房间加入票据</DialogTitle>
            <DialogDescription className="text-slate-400">12 小时内有效。桌面客户端登录后也会自动领取。</DialogDescription>
          </DialogHeader>
          <Textarea readOnly value={ticket} className="mono-num min-h-24 border-slate-700 bg-[#060d1c] text-xs text-cyan-200" />
          <DialogFooter>
            <Button
              onClick={() => {
                void navigator.clipboard.writeText(ticket);
                setCopied(true);
                window.setTimeout(() => setCopied(false), 2000);
              }}
              className="bg-cyan-500 text-slate-950 hover:bg-cyan-400"
            >
              {copied ? <Check className="mr-1.5 h-4 w-4" /> : <Copy className="mr-1.5 h-4 w-4" />}
              {copied ? "已复制" : "复制票据"}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog
        open={membersRoom !== null}
        onOpenChange={(open) => {
          if (!open) {
            setMembersRoom(null);
            setMembers([]);
          }
        }}
      >
        <DialogContent className="border-slate-700/70 bg-[#0b1324] text-slate-100 sm:max-w-lg">
          <DialogHeader>
            <DialogTitle>在线成员 · {membersRoom}</DialogTitle>
            <DialogDescription className="text-slate-400">WebSocket 实时推送活跃心跳成员及其隧道转发模式</DialogDescription>
          </DialogHeader>
          <div className="grid gap-2.5 text-xs">
            {members.length === 0 && <p className="py-6 text-center text-slate-500">该房间暂无在线成员</p>}
            {members.map((member) => (
              <div
                key={member.mac}
                className="flex items-center justify-between gap-3 rounded-xl border border-slate-800 bg-[#070f20] px-3.5 py-2.5"
              >
                <div className="min-w-0 space-y-0.5">
                  <div className="flex items-center gap-2">
                    <span className="mono-num font-semibold text-cyan-300">{member.virtualIp}</span>
                    {modeBadge(member.direct && !member.tcpRelay ? "direct" : "relay")}
                  </div>
                  <div className="mono-num truncate text-[11px] text-slate-400">
                    {member.mac} · {member.source}
                  </div>
                </div>
                {can(user, "member:kick") && (
                  <Button
                    variant="destructive"
                    size="sm"
                    onClick={() =>
                      void live
                        .action("kick", { room: member.room, mac: member.mac })
                        .then(() => loadMembers(member.room))
                        .catch((err) => onError(err instanceof Error ? err.message : String(err)))
                    }
                  >
                    移出
                  </Button>
                )}
              </div>
            ))}
          </div>
        </DialogContent>
      </Dialog>
    </>
  );
}

function UsersPage({ onError }: { onError: (value: string) => void }) {
  const [items, setItems] = useState<User[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [q, setQ] = useState("");
  const [open, setOpen] = useState(false);
  const [form, setForm] = useState({ username: "", password: "", display_name: "", role_key: "member" });

  function load(targetPage = page, query = q) {
    return api.users(query, targetPage, 20).then((body) => {
      setItems(body.items);
      setTotal(body.total);
    });
  }

  useEffect(() => {
    void load(page, q).catch((err) => onError(err instanceof Error ? err.message : String(err)));
  }, [page]);

  const totalPages = Math.max(1, Math.ceil(total / 20));

  return (
    <>
      <PageHead title="用户账号" subtitle={`共 ${total} 个账号，支持启停与角色分配`}>
        <div className="relative">
          <Search className="pointer-events-none absolute left-3 top-2.5 h-4 w-4 text-slate-500" />
          <Input
            className="w-48 border-slate-700/70 bg-[#0b1324] pl-9 text-sm"
            placeholder="搜索账号或昵称"
            value={q}
            onChange={(event) => setQ(event.target.value)}
            onKeyDown={(event) => {
              if (event.key === "Enter") {
                setPage(1);
                void load(1, q).catch((err) => onError(String(err)));
              }
            }}
          />
        </div>
        <Button
          variant="outline"
          className="border-slate-700/70 bg-[#0b1324]"
          onClick={() => {
            setPage(1);
            void load(1, q).catch((err) => onError(err instanceof Error ? err.message : String(err)));
          }}
        >
          查询
        </Button>
        <Button onClick={() => setOpen(true)} className="bg-cyan-500 text-slate-950 hover:bg-cyan-400">
          <UserPlus className="mr-1.5 h-4 w-4" />
          新建用户
        </Button>
      </PageHead>

      <Card className="panel-card overflow-hidden rounded-2xl border-slate-800/80">
        <Table>
          <TableHeader>
            <TableRow className="border-slate-800/80 hover:bg-transparent">
              <TableHead className="text-slate-400">账号</TableHead>
              <TableHead className="text-slate-400">显示名</TableHead>
              <TableHead className="text-slate-400">角色</TableHead>
              <TableHead className="text-slate-400">最近登录</TableHead>
              <TableHead className="text-slate-400">状态</TableHead>
              <TableHead className="text-right text-slate-400">操作</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {items.map((item) => (
              <TableRow key={item.id} className="border-slate-800/70 hover:bg-slate-800/35">
                <TableCell className="mono-num font-medium text-white">{item.username}</TableCell>
                <TableCell className="text-slate-300">{item.displayName}</TableCell>
                <TableCell>
                  <span className={`rounded-full border px-2 py-0.5 text-xs font-medium ${roleBadgeStyle(item.role)}`}>
                    {item.role}
                  </span>
                </TableCell>
                <TableCell className="mono-num text-xs text-slate-400">
                  {item.lastLoginAt ? `${when(item.lastLoginAt)} · ${item.lastLoginIp}` : "从未登录"}
                </TableCell>
                <TableCell>
                  <span
                    className={`inline-flex items-center gap-1.5 rounded-full border px-2.5 py-0.5 text-xs ${
                      item.isActive
                        ? "border-emerald-500/40 bg-emerald-500/12 text-emerald-300"
                        : "border-rose-500/40 bg-rose-500/12 text-rose-300"
                    }`}
                  >
                    <span className={`h-1.5 w-1.5 rounded-full ${item.isActive ? "bg-emerald-400" : "bg-rose-400"}`} />
                    {item.isActive ? "启用" : "停用"}
                  </span>
                </TableCell>
                <TableCell className="text-right">
                  <Button
                    variant="outline"
                    size="sm"
                    className="border-slate-700/70 bg-slate-900/60"
                    onClick={() =>
                      void api
                        .patchUser(item.id, { is_active: !item.isActive })
                        .then(() => load())
                        .catch((err) => onError(err instanceof Error ? err.message : String(err)))
                    }
                  >
                    {item.isActive ? "停用" : "启用"}
                  </Button>
                </TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </Card>

      <Pagination page={page} totalPages={totalPages} total={total} onChange={setPage} />

      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent className="border-slate-700/70 bg-[#0b1324] text-slate-100">
          <form
            className="grid gap-4"
            onSubmit={(event) => {
              event.preventDefault();
              void api
                .createUser(form)
                .then(() => {
                  setOpen(false);
                  setForm({ username: "", password: "", display_name: "", role_key: "member" });
                  return load(1, q);
                })
                .catch((err) => onError(err instanceof Error ? err.message : "创建失败"));
            }}
          >
            <DialogHeader>
              <DialogTitle>新建用户</DialogTitle>
              <DialogDescription className="text-slate-400">账号至少 2 位，密码至少 6 位</DialogDescription>
            </DialogHeader>
            <div className="grid gap-2">
              <Label htmlFor="new-username">账号</Label>
              <Input
                id="new-username"
                className="border-slate-700 bg-[#060d1c]"
                value={form.username}
                onChange={(event) => setForm({ ...form, username: event.target.value })}
              />
            </div>
            <div className="grid gap-2">
              <Label htmlFor="new-display">显示名</Label>
              <Input
                id="new-display"
                className="border-slate-700 bg-[#060d1c]"
                value={form.display_name}
                onChange={(event) => setForm({ ...form, display_name: event.target.value })}
              />
            </div>
            <div className="grid gap-2">
              <Label htmlFor="new-password">密码</Label>
              <Input
                id="new-password"
                type="password"
                className="border-slate-700 bg-[#060d1c]"
                value={form.password}
                onChange={(event) => setForm({ ...form, password: event.target.value })}
              />
            </div>
            <div className="grid gap-2">
              <Label>角色</Label>
              <Select value={form.role_key} onValueChange={(value) => setForm({ ...form, role_key: value })}>
                <SelectTrigger className="border-slate-700 bg-[#060d1c]">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent className="border-slate-700 bg-[#0d1629] text-slate-100">
                  <SelectItem value="member">成员</SelectItem>
                  <SelectItem value="operator">运维</SelectItem>
                  <SelectItem value="admin">管理员</SelectItem>
                </SelectContent>
              </Select>
            </div>
            <DialogFooter>
              <Button type="button" variant="outline" className="border-slate-700" onClick={() => setOpen(false)}>
                取消
              </Button>
              <Button type="submit" className="bg-cyan-500 text-slate-950 hover:bg-cyan-400">
                保存
              </Button>
            </DialogFooter>
          </form>
        </DialogContent>
      </Dialog>
    </>
  );
}

function RolesPage({ onError }: { onError: (value: string) => void }) {
  const [roles, setRoles] = useState<Role[]>([]);
  const [catalog, setCatalog] = useState<{ code: string; name: string }[]>([]);
  const [editing, setEditing] = useState<Role | null>(null);
  const [picked, setPicked] = useState<string[]>([]);

  function load() {
    return Promise.all([api.roles(), api.permissions()]).then(([roleBody, permBody]) => {
      setRoles(roleBody.items);
      setCatalog(permBody.items);
    });
  }

  useEffect(() => {
    void load().catch((err) => onError(err instanceof Error ? err.message : String(err)));
  }, []);

  const permLabel = (code: string) => {
    if (code === "*:*") return "全部权限 (*:*)";
    return catalog.find((c) => c.code === code)?.name ?? code;
  };

  return (
    <>
      <PageHead title="角色权限" subtitle="配置系统角色的细粒度功能权限" />

      <div className="grid gap-4 md:grid-cols-3">
        {roles.map((role) => (
          <div key={role.key} className="panel-card flex flex-col justify-between rounded-2xl p-5">
            <div>
              <div className="flex items-center justify-between gap-2">
                <div>
                  <h2 className="text-base font-semibold text-white">{role.name}</h2>
                  <p className="mono-num text-xs text-slate-400">{role.key}</p>
                </div>
                <span className={`rounded-full border px-2.5 py-0.5 text-xs font-medium ${roleBadgeStyle(role.key)}`}>
                  {role.permissions.includes("*:*") ? "超级权限" : `${role.permissions.length} 项`}
                </span>
              </div>
              <div className="mt-4 flex flex-wrap gap-1.5">
                {role.permissions.map((code) => (
                  <span
                    key={code}
                    className="rounded-lg border border-slate-700/70 bg-slate-900/70 px-2 py-1 text-[11px] text-slate-300"
                  >
                    {permLabel(code)}
                  </span>
                ))}
              </div>
            </div>
            <div className="mt-5 border-t border-slate-800/70 pt-3">
              <Button
                variant="outline"
                size="sm"
                className="w-full border-slate-700/70 bg-slate-900/60"
                onClick={() => {
                  setEditing(role);
                  setPicked(role.permissions);
                }}
              >
                编辑权限
              </Button>
            </div>
          </div>
        ))}
      </div>

      <Dialog
        open={editing != null}
        onOpenChange={(open) => {
          if (!open) setEditing(null);
        }}
      >
        <DialogContent className="border-slate-700/70 bg-[#0b1324] text-slate-100">
          <form
            className="grid gap-4"
            onSubmit={(event) => {
              event.preventDefault();
              if (!editing) return;
              void api
                .saveRole(editing.key, editing.name, picked)
                .then(() => {
                  setEditing(null);
                  return load();
                })
                .catch((err) => onError(err instanceof Error ? err.message : String(err)));
            }}
          >
            <DialogHeader>
              <DialogTitle>编辑角色 · {editing?.name}</DialogTitle>
              <DialogDescription className="text-slate-400">从已有权限目录中勾选，管理员支持通配权限 *:*</DialogDescription>
            </DialogHeader>
            <Label className="flex items-center gap-2 rounded-xl border border-violet-500/30 bg-violet-500/10 px-3 py-2.5 font-normal text-violet-200">
              <Checkbox
                checked={picked.includes("*:*")}
                onCheckedChange={(checked) =>
                  setPicked((current) =>
                    checked === true ? ["*:*"] : current.filter((code) => code !== "*:*"),
                  )
                }
              />
              全部权限（*:*）
            </Label>
            <div className="grid grid-cols-2 gap-2.5">
              {catalog.map((item) => {
                const wild = picked.includes("*:*");
                return (
                  <Label
                    key={item.code}
                    className={`flex items-center gap-2 rounded-xl border border-slate-800 bg-[#060d1c] px-3 py-2 font-normal ${
                      wild ? "opacity-50" : ""
                    }`}
                  >
                    <Checkbox
                      disabled={wild}
                      checked={wild || picked.includes(item.code)}
                      onCheckedChange={(checked) =>
                        setPicked((current) =>
                          checked === true
                            ? current.includes(item.code)
                              ? current
                              : [...current, item.code]
                            : current.filter((code) => code !== item.code),
                        )
                      }
                    />
                    <span className="text-xs text-slate-200">{item.name}</span>
                  </Label>
                );
              })}
            </div>
            <DialogFooter>
              <Button type="button" variant="outline" className="border-slate-700" onClick={() => setEditing(null)}>
                取消
              </Button>
              <Button type="submit" className="bg-cyan-500 text-slate-950 hover:bg-cyan-400">
                保存
              </Button>
            </DialogFooter>
          </form>
        </DialogContent>
      </Dialog>
    </>
  );
}

function LogsPage({ onError }: { onError: (value: string) => void }) {
  const [items, setItems] = useState<LoginLog[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [q, setQ] = useState("");

  function load(targetPage = page, query = q) {
    return api.logs(query, targetPage, 20).then((body) => {
      setItems(body.items);
      setTotal(body.total);
    });
  }

  useEffect(() => {
    void load(page, q).catch((err) => onError(err instanceof Error ? err.message : String(err)));
  }, [page]);

  const totalPages = Math.max(1, Math.ceil(total / 20));

  return (
    <>
      <PageHead title="登录审计" subtitle={`共 ${total} 条认证与登出日志，自动解析来源归属地`}>
        <div className="relative">
          <Search className="pointer-events-none absolute left-3 top-2.5 h-4 w-4 text-slate-500" />
          <Input
            className="w-48 border-slate-700/70 bg-[#0b1324] pl-9 text-sm"
            placeholder="按账号过滤"
            value={q}
            onChange={(event) => setQ(event.target.value)}
            onKeyDown={(event) => {
              if (event.key === "Enter") {
                setPage(1);
                void load(1, q).catch((err) => onError(String(err)));
              }
            }}
          />
        </div>
        <Button
          variant="outline"
          className="border-slate-700/70 bg-[#0b1324]"
          onClick={() => {
            setPage(1);
            void load(1, q).catch((err) => onError(err instanceof Error ? err.message : String(err)));
          }}
        >
          查询
        </Button>
      </PageHead>

      <Card className="panel-card overflow-hidden rounded-2xl border-slate-800/80">
        <Table>
          <TableHeader>
            <TableRow className="border-slate-800/80 hover:bg-transparent">
              <TableHead className="text-slate-400">账号</TableHead>
              <TableHead className="text-slate-400">结果</TableHead>
              <TableHead className="text-slate-400">来源 IP</TableHead>
              <TableHead className="text-slate-400">归属地</TableHead>
              <TableHead className="text-slate-400">时间</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {items.map((item, index) => (
              <TableRow key={`${item.username}-${item.createdAt}-${index}`} className="border-slate-800/70 hover:bg-slate-800/35">
                <TableCell className="mono-num font-medium text-white">{item.username}</TableCell>
                <TableCell>
                  <span
                    className={`inline-flex items-center gap-1.5 rounded-full border px-2.5 py-0.5 text-xs ${
                      item.result === "ok"
                        ? "border-emerald-500/40 bg-emerald-500/12 text-emerald-300"
                        : item.result === "logout"
                          ? "border-slate-600/50 bg-slate-700/30 text-slate-300"
                          : "border-rose-500/40 bg-rose-500/12 text-rose-300"
                    }`}
                  >
                    {item.result === "ok" ? "成功" : item.result === "logout" ? "退出" : "失败"}
                  </span>
                </TableCell>
                <TableCell className="mono-num text-xs text-cyan-200">{item.ip || "—"}</TableCell>
                <TableCell className="text-xs text-slate-300">
                  {item.city || item.country ? `${item.city || ""} ${item.country || ""}`.trim() : "—"}
                </TableCell>
                <TableCell className="mono-num text-xs text-slate-400">{when(item.createdAt)}</TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </Card>

      <Pagination page={page} totalPages={totalPages} total={total} onChange={setPage} />
    </>
  );
}

function Pagination({
  page,
  totalPages,
  total,
  onChange,
}: {
  page: number;
  totalPages: number;
  total: number;
  onChange: (next: number) => void;
}) {
  if (total <= 20) return null;
  return (
    <div className="mt-4 flex items-center justify-between text-xs text-slate-400">
      <span>
        第 <strong className="text-slate-200">{page}</strong> / {totalPages} 页 · 共 {total} 条
      </span>
      <div className="flex items-center gap-2">
        <Button
          size="sm"
          variant="outline"
          disabled={page <= 1}
          onClick={() => onChange(page - 1)}
          className="border-slate-700/70 bg-[#0b1324]"
        >
          <ChevronLeft className="mr-1 h-3.5 w-3.5" />
          上一页
        </Button>
        <Button
          size="sm"
          variant="outline"
          disabled={page >= totalPages}
          onClick={() => onChange(page + 1)}
          className="border-slate-700/70 bg-[#0b1324]"
        >
          下一页
          <ChevronRight className="ml-1 h-3.5 w-3.5" />
        </Button>
      </div>
    </div>
  );
}
