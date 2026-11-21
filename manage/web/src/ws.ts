import { useCallback, useEffect, useRef, useState } from "react";
import { setToken, token, type MapPayload, type Room } from "./api";

export type WsStatus = "connecting" | "live" | "reconnecting" | "offline";

export type LiveEvent = {
  id: string;
  kind: "join" | "leave" | "mode_change";
  room: string;
  memberId: string;
  virtualIp: string;
  city: string;
  country: string;
  mode: string;
  prevMode?: string;
  source: string;
  ts: string;
};

type SnapshotFrame = {
  type: "snapshot";
  seq: number;
  ts: string;
  room?: string;
  query?: string;
  map?: MapPayload | null;
  rooms?: Room[] | null;
  events?: LiveEvent[] | null;
};

type PongFrame = {
  type: "pong";
  ts: number;
  serverTs: string;
};

type AckFrame = {
  type: "ack";
  id: string;
  ok: boolean;
  data?: unknown;
  error?: string;
};

type UnauthorizedFrame = {
  type: "unauthorized";
  detail?: string;
};

type ServerFrame = SnapshotFrame | PongFrame | AckFrame | UnauthorizedFrame | { type: string; detail?: string };

export type LivePanelController = {
  status: WsStatus;
  rttMs: number | null;
  lastSync: string;
  seq: number;
  mapData: MapPayload | null;
  rooms: Room[];
  events: LiveEvent[];
  recentJoinIds: string[];
  roomFilter: string;
  roomQuery: string;
  setRoomFilter: (room: string) => void;
  setRoomQuery: (query: string) => void;
  refresh: () => void;
  action: <T = Record<string, unknown>>(type: string, payload?: Record<string, unknown>) => Promise<T>;
};

function wsEndpoint(jwt: string, room: string, query: string): string {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  const params = new URLSearchParams();
  params.set("token", jwt);
  if (room) params.set("room", room);
  if (query) params.set("q", query);
  return `${proto}//${location.host}/api/ws?${params.toString()}`;
}

export function useLivePanel(enabled: boolean): LivePanelController {
  const [status, setStatus] = useState<WsStatus>(enabled ? "connecting" : "offline");
  const [rttMs, setRttMs] = useState<number | null>(null);
  const [lastSync, setLastSync] = useState("");
  const [seq, setSeq] = useState(0);
  const [mapData, setMapData] = useState<MapPayload | null>(null);
  const [rooms, setRooms] = useState<Room[]>([]);
  const [events, setEvents] = useState<LiveEvent[]>([]);
  const [recentJoinIds, setRecentJoinIds] = useState<string[]>([]);
  const [roomFilter, setRoomFilterState] = useState("");
  const [roomQuery, setRoomQueryState] = useState("");

  const wsRef = useRef<WebSocket | null>(null);
  const roomFilterRef = useRef("");
  const roomQueryRef = useRef("");
  const reqSeqRef = useRef(0);
  const pendingRef = useRef<
    Map<string, { resolve: (val: unknown) => void; reject: (err: Error) => void; timer: number }>
  >(new Map());

  const rejectAllPending = useCallback((reason: string) => {
    for (const [, p] of pendingRef.current.entries()) {
      window.clearTimeout(p.timer);
      p.reject(new Error(reason));
    }
    pendingRef.current.clear();
  }, []);

  const sendRaw = useCallback((msg: Record<string, unknown>): boolean => {
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return false;
    ws.send(JSON.stringify(msg));
    return true;
  }, []);

  const setRoomFilter = useCallback(
    (next: string) => {
      roomFilterRef.current = next;
      setRoomFilterState(next);
      sendRaw({ type: "subscribe", room: next, query: roomQueryRef.current });
    },
    [sendRaw],
  );

  const setRoomQuery = useCallback(
    (next: string) => {
      roomQueryRef.current = next;
      setRoomQueryState(next);
      sendRaw({ type: "subscribe", room: roomFilterRef.current, query: next });
    },
    [sendRaw],
  );

  const refresh = useCallback(() => {
    sendRaw({ type: "refresh" });
  }, [sendRaw]);

  const action = useCallback(
    <T = Record<string, unknown>>(type: string, payload: Record<string, unknown> = {}): Promise<T> => {
      const ws = wsRef.current;
      if (!ws || ws.readyState !== WebSocket.OPEN) {
        return Promise.reject(new Error("WebSocket 正在重连，请稍后重试"));
      }
      const id = `ws-${++reqSeqRef.current}-${Date.now().toString(36)}`;
      return new Promise<T>((resolve, reject) => {
        const timer = window.setTimeout(() => {
          pendingRef.current.delete(id);
          reject(new Error("WebSocket 指令超时"));
        }, 10000);
        pendingRef.current.set(id, {
          resolve: resolve as (val: unknown) => void,
          reject,
          timer,
        });
        ws.send(JSON.stringify({ ...payload, type, id }));
      });
    },
    [],
  );

  useEffect(() => {
    if (!enabled) {
      setStatus("offline");
      setRttMs(null);
      setMapData(null);
      setRooms([]);
      setEvents([]);
      wsRef.current?.close();
      wsRef.current = null;
      rejectAllPending("会话已退出");
      return;
    }

    let disposed = false;
    let attempt = 0;
    let reconnectTimer = 0;
    let pingTimer = 0;
    const joinTimers: number[] = [];

    const connect = () => {
      if (disposed) return;
      const jwt = token();
      if (!jwt) {
        setStatus("offline");
        return;
      }
      setStatus(attempt === 0 ? "connecting" : "reconnecting");
      const url = wsEndpoint(jwt, roomFilterRef.current, roomQueryRef.current);
      const ws = new WebSocket(url);
      wsRef.current = ws;

      ws.onopen = () => {
        if (disposed) {
          ws.close();
          return;
        }
        attempt = 0;
        setStatus("live");
        // Immediately probe RTT and keep ping loop active
        ws.send(JSON.stringify({ type: "ping", ts: Date.now() }));
        window.clearInterval(pingTimer);
        pingTimer = window.setInterval(() => {
          if (ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: "ping", ts: Date.now() }));
          }
        }, 5000);
      };

      ws.onmessage = (ev) => {
        if (disposed) return;
        let frame: ServerFrame;
        try {
          frame = JSON.parse(String(ev.data)) as ServerFrame;
        } catch {
          return;
        }

        if (frame.type === "pong") {
          const pong = frame as PongFrame;
          if (typeof pong.ts === "number") {
            setRttMs(Math.max(1, Date.now() - pong.ts));
          }
          return;
        }

        if (frame.type === "unauthorized") {
          disposed = true;
          setToken("");
          ws.close();
          location.hash = "#/login";
          return;
        }

        if (frame.type === "ack") {
          const ack = frame as AckFrame;
          const pending = pendingRef.current.get(ack.id);
          if (pending) {
            pendingRef.current.delete(ack.id);
            window.clearTimeout(pending.timer);
            if (ack.ok) pending.resolve(ack.data);
            else pending.reject(new Error(ack.error || "操作失败"));
          }
          return;
        }

        if (frame.type === "snapshot") {
          const snap = frame as SnapshotFrame;
          setSeq(snap.seq);
          const now = new Date();
          setLastSync(now.toTimeString().slice(0, 8));
          if (snap.map !== undefined && snap.map !== null) {
            setMapData(snap.map);
          }
          if (Array.isArray(snap.rooms)) {
            setRooms(snap.rooms);
          }
          if (Array.isArray(snap.events) && snap.events.length > 0) {
            setEvents((prev) => [...snap.events!, ...prev].slice(0, 24));
            const joined = snap.events.filter((e) => e.kind === "join").map((e) => e.memberId);
            if (joined.length > 0) {
              setRecentJoinIds((prev) => Array.from(new Set([...prev, ...joined])));
              const t = window.setTimeout(() => {
                setRecentJoinIds((prev) => prev.filter((id) => !joined.includes(id)));
              }, 4500);
              joinTimers.push(t);
            }
          }
        }
      };

      ws.onclose = () => {
        window.clearInterval(pingTimer);
        if (wsRef.current === ws) {
          wsRef.current = null;
        }
        rejectAllPending("WebSocket 连接已断开");
        if (disposed) return;
        attempt++;
        const delay = Math.min(8000, 750 * Math.pow(1.6, Math.min(attempt, 6)));
        setStatus("reconnecting");
        setRttMs(null);
        reconnectTimer = window.setTimeout(connect, delay);
      };

      ws.onerror = () => {
        ws.close();
      };
    };

    connect();

    return () => {
      disposed = true;
      window.clearTimeout(reconnectTimer);
      window.clearInterval(pingTimer);
      joinTimers.forEach((t) => window.clearTimeout(t));
      wsRef.current?.close();
      wsRef.current = null;
      rejectAllPending("页面已切换");
    };
  }, [enabled, rejectAllPending]);

  return {
    status,
    rttMs,
    lastSync,
    seq,
    mapData,
    rooms,
    events,
    recentJoinIds,
    roomFilter,
    roomQuery,
    setRoomFilter,
    setRoomQuery,
    refresh,
    action,
  };
}
