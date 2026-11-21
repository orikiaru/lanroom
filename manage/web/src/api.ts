export type User = {
  id: number;
  username: string;
  displayName: string;
  role: string;
  isActive: boolean;
  lastLoginAt: string | null;
  lastLoginIp: string;
  permissions: string[];
};

export type Room = {
  name: string;
  owner: string;
  ownerId: number;
  createdAt: string;
  mine: boolean;
  onlineCount?: number;
  mode?: "direct" | "relay" | "standby" | "idle";
};

export type Member = {
  mac: string;
  virtualIp: string;
  source: string;
  ip?: string;
  direct?: boolean;
  tcpRelay?: boolean;
  relayCount?: number;
};

export type ServerNode = {
  name: string;
  ip: string;
  country: string;
  city: string;
  lat: number | null;
  lon: number | null;
};

export type MapMember = Member & {
  id?: string;
  room: string;
  owner: string;
  mode?: "direct" | "relay" | "standby";
  country: string;
  city: string;
  lat: number | null;
  lon: number | null;
};

export type MapLinkEndpoint = {
  lat: number;
  lon: number;
  label: string;
};

export type MapLink = {
  id: string;
  kind: "direct" | "relay" | "control";
  room: string;
  fromId: string;
  toId: string;
  from: MapLinkEndpoint;
  to: MapLinkEndpoint;
  relayCount?: number;
};

export type MapCity = { country: string; city: string; lat: number; lon: number; count: number };
export type MapCountry = { country: string; count: number };
export type LoginPoint = {
  username: string;
  result: string;
  ip: string;
  createdAt: string;
  country: string;
  city: string;
  lat: number | null;
  lon: number | null;
};
export type Role = { key: string; name: string; isSystem: boolean; permissions: string[] };
export type LoginLog = {
  username: string;
  result: string;
  ip: string;
  country?: string;
  city?: string;
  createdAt: string;
};

export type MapPayload = {
  server: ServerNode | null;
  members: MapMember[];
  links: MapLink[];
  cities: MapCity[];
  countries: MapCountry[];
  logins: LoginPoint[];
  rooms: string[];
};

const TOKEN = "lanroom.token";

export function token() {
  return localStorage.getItem(TOKEN) || "";
}

export function setToken(value: string) {
  if (value) localStorage.setItem(TOKEN, value);
  else localStorage.removeItem(TOKEN);
}

export function can(user: User | null, code: string) {
  if (!user) return false;
  return user.permissions.includes("*:*") || user.permissions.includes(code);
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const headers = new Headers(init.headers);
  headers.set("Content-Type", "application/json");
  if (token()) headers.set("Authorization", `Bearer ${token()}`);
  const response = await fetch(path, { ...init, headers });
  if (response.status === 401) {
    setToken("");
    location.hash = "#/login";
  }
  if (!response.ok) {
    const body = await response.json().catch(() => ({ detail: response.statusText }));
    throw new Error(typeof body.detail === "string" ? body.detail : "请求失败");
  }
  return response.json() as Promise<T>;
}

export const api = {
  login: (username: string, password: string) =>
    request<{ token: string; user: User }>("/api/login", { method: "POST", body: JSON.stringify({ username, password }) }),
  me: () => request<User>("/api/me"),
  logout: () => request<{ ok: boolean }>("/api/logout", { method: "POST" }),
  rooms: (q = "") => request<{ items: Room[] }>(`/api/rooms?q=${encodeURIComponent(q)}`),
  createRoom: (name: string) => request<{ name: string }>("/api/rooms", { method: "POST", body: JSON.stringify({ name }) }),
  closeRoom: (name: string) => request(`/api/rooms/${encodeURIComponent(name)}/close`, { method: "POST" }),
  ticket: (name: string) => request<{ token: string; expiresAt?: string }>(`/api/rooms/${encodeURIComponent(name)}/ticket`, { method: "POST" }),
  members: (name: string) => request<{ items: Member[] }>(`/api/rooms/${encodeURIComponent(name)}/members`),
  kick: (name: string, mac: string) =>
    request(`/api/rooms/${encodeURIComponent(name)}/kick`, { method: "POST", body: JSON.stringify({ mac }) }),
  map: (room = "") => request<MapPayload>(`/api/map?room=${encodeURIComponent(room)}`),
  users: (q = "", page = 1, pageSize = 20) =>
    request<{ total: number; items: User[] }>(`/api/users?q=${encodeURIComponent(q)}&page=${page}&page_size=${pageSize}`),
  createUser: (body: { username: string; password: string; display_name: string; role_key: string }) =>
    request("/api/users", { method: "POST", body: JSON.stringify(body) }),
  patchUser: (id: number, body: { is_active?: boolean; role_key?: string; password?: string }) =>
    request(`/api/users/${id}`, { method: "PATCH", body: JSON.stringify(body) }),
  roles: () => request<{ items: Role[] }>("/api/roles"),
  permissions: () => request<{ items: { code: string; name: string }[] }>("/api/permissions"),
  saveRole: (key: string, name: string, permissions: string[]) =>
    request(`/api/roles/${key}`, { method: "PUT", body: JSON.stringify({ name, permissions }) }),
  logs: (q = "", page = 1, pageSize = 20) =>
    request<{ total: number; items: LoginLog[] }>(`/api/login-logs?q=${encodeURIComponent(q)}&page=${page}&page_size=${pageSize}`),
};
