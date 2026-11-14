# 一台机器上跑 todo.md 里的房间用例。客户端不连接 Redis。
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SERVER = BUILD / ("lanroom-server.exe" if sys.platform == "win32" else "lanroom-server")
CLIENT = BUILD / ("lanroom-client.exe" if sys.platform == "win32" else "lanroom-client")
REDIS = ("127.0.0.1", 6379)
REDIS_PROC = None
SERVER_PORT = 17000
PROBE_ID = 4106
FAILS = []


def probe_frame(room, mac, saw=0):
    def field(seq, data):
        return struct.pack("<II", seq, len(data)) + data
    body = struct.pack("<Q", 0) + field(0, room) + field(1, mac) + field(2, bytes([saw]))
    return struct.pack("<II", PROBE_ID, len(body)) + body


def redis_call(*parts):
    blobs = []
    for part in parts:
        data = part if isinstance(part, (bytes, bytearray)) else str(part).encode()
        blobs.append(b"$%d\r\n" % len(data) + data + b"\r\n")
    payload = b"*%d\r\n" % len(parts) + b"".join(blobs)
    with socket.create_connection(REDIS, 2) as conn:
        conn.sendall(payload)
        conn.settimeout(2)
        buffer = b""
        while b"\r\n" not in buffer:
            buffer += conn.recv(4096)
        if buffer.startswith(b"$-1"):
            return None
        if buffer.startswith(b"$"):
            length = int(buffer[1:].split(b"\r\n", 1)[0])
            rest = buffer.split(b"\r\n", 1)[1]
            while len(rest) < length + 2:
                rest += conn.recv(length + 2)
            return rest[:length]
        if buffer.startswith(b":") or buffer.startswith(b"+"):
            return buffer.split(b"\r\n", 1)[0]
        raise RuntimeError(buffer[:200])


def pb_fields(blob):
    fields = []
    offset = 0
    while offset + 8 <= len(blob):
        seq = int.from_bytes(blob[offset:offset + 4], "little")
        size = int.from_bytes(blob[offset + 4:offset + 8], "little")
        offset += 8
        if offset + size > len(blob):
            break
        fields.append((seq, blob[offset:offset + size]))
        offset += size
    return fields


def decode_room(blob):
    if blob is None:
        return None
    relay = 0
    tcp = 0
    members = []
    for seq, data in pb_fields(blob):
        if seq == 0 and len(data) == 4:
            relay = int.from_bytes(data, "little")
        elif seq == 2:
            for field, value in pb_fields(data):
                if field == 0 and len(value) == 1:
                    tcp = value[0]
        elif seq == 1:
            member = {"mac": b"", "ip": 0, "port": 0, "virtual": 0, "direct": 0, "saw": 0}
            for field, value in pb_fields(data):
                if field == 0:
                    member["mac"] = value
                elif field == 1 and len(value) == 4:
                    member["ip"] = int.from_bytes(value, "little")
                elif field == 2 and len(value) == 2:
                    member["port"] = int.from_bytes(value, "little")
                elif field == 3 and len(value) == 4:
                    member["virtual"] = int.from_bytes(value, "little")
                elif field == 4 and len(value) == 1:
                    member["direct"] = value[0]
                elif field == 5 and len(value) == 1:
                    member["saw"] = value[0]
            members.append(member)
    return {"relay": relay, "tcp": tcp, "members": members}


LIVE = []


class Proc:
    def __init__(self, args):
        self.proc = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.lines = []
        self.thread = threading.Thread(target=self._drain, daemon=True)
        self.thread.start()
        LIVE.append(self)

    def _drain(self):
        for raw in self.proc.stdout:
            self.lines.append(raw.decode("utf-8", "replace"))

    def stop(self):
        if self in LIVE:
            LIVE.remove(self)
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait(timeout=5)


class Nic:
    def __init__(self, port):
        self.conn = socket.create_connection(("127.0.0.1", port), 3)
        self.conn.settimeout(3)
        self.buffer = b""

    def close(self):
        self.conn.close()

    def line(self):
        while b"\n" not in self.buffer:
            self.buffer += self.conn.recv(4096)
        text, self.buffer = self.buffer.split(b"\n", 1)
        return text.decode()

    def cmd(self, text):
        self.conn.sendall((text + "\n").encode())
        return self.line()

    def write(self, payload):
        self.conn.sendall(("WRITE %d\n" % len(payload)).encode() + payload)
        return self.line()

    def read(self):
        reply = self.cmd("READ")
        if reply == "empty":
            return None
        assert reply.startswith("frame ")
        size = int(reply.split()[1])
        while len(self.buffer) < size:
            self.buffer += self.conn.recv(size)
        data = self.buffer[:size]
        self.buffer = self.buffer[size:]
        return data

    def peers(self):
        head = self.cmd("PEERS")
        count = int(head.split()[1])
        return [self.line() for _ in range(count)]


def start_client(room, mac, nic_port, bind="0.0.0.0", drop=False, tcp=False):
    args = [str(CLIENT), "--config", "config/client.toml", "--server", "127.0.0.1", "--server-port", str(SERVER_PORT), "--room", room, "--mac", mac, "--nic-port", str(nic_port), "--bind", bind]
    if drop:
        args.append("--drop")
    if tcp:
        args.append("--tcp")
    proc = Proc(args)
    nic = None
    deadline = time.time() + 8
    while time.time() < deadline:
        if proc.proc.poll() is not None:
            break
        if nic is None:
            try:
                nic = Nic(nic_port)
            except OSError:
                time.sleep(0.05)
                continue
        try:
            reply = nic.cmd("VIRT")
        except OSError:
            nic.close()
            nic = None
            time.sleep(0.05)
            continue
        if reply.startswith("virt ") and reply != "virt 0.0.0.0":
            return proc, nic
        time.sleep(0.05)
    if nic is not None:
        nic.close()
    proc.stop()
    raise RuntimeError("client not joined: " + "".join(proc.lines[-20:]))


def wait_members(room, count, timeout=4):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = decode_room(redis_call("GET", room))
        if last is not None and len(last["members"]) == count:
            return last
        if count == 0 and last is None:
            return None
        time.sleep(0.1)
    return last


def check(name, ok):
    print(("PASS " if ok else "FAIL ") + name, flush=True)
    if not ok:
        FAILS.append(name)


def cleanup(*items):
    for item in items:
        try:
            if isinstance(item, Proc):
                item.stop()
            elif isinstance(item, Nic):
                item.close()
        except Exception:
            pass


def redis_up(port):
    try:
        with socket.create_connection(("127.0.0.1", port), 0.4) as conn:
            conn.sendall(b"*1\r\n$4\r\nPING\r\n")
            conn.settimeout(0.4)
            return conn.recv(64).startswith(b"+PONG")
    except OSError:
        return False


def find_redis_server():
    env = os.environ.get("REDIS_SERVER")
    if env and Path(env).is_file():
        return env
    found = shutil.which("redis-server")
    if found:
        return found
    tools = Path.home() / "tools"
    if tools.is_dir():
        hits = sorted(tools.rglob("redis-server.exe"))
        if hits:
            return str(hits[0])
    return None


def ensure_redis():
    global REDIS, REDIS_PROC
    if redis_up(6379):
        REDIS = ("127.0.0.1", 6379)
        return True
    binary = find_redis_server()
    if binary is None:
        print("找不到 redis-server，设 REDIS_SERVER 指向可执行文件")
        return False
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    REDIS_PROC = subprocess.Popen(
        [binary, "--bind", "127.0.0.1", "--port", str(port), "--save", "", "--appendonly", "no", "--dir", os.environ.get("TEMP", ".")],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    deadline = time.time() + 5
    while time.time() < deadline:
        if redis_up(port):
            REDIS = ("127.0.0.1", port)
            print("redis listen %s:%d" % REDIS, flush=True)
            return True
        if REDIS_PROC.poll() is not None:
            print("redis-server 退出了")
            return False
        time.sleep(0.05)
    print("redis-server 没有开始监听")
    REDIS_PROC.kill()
    return False


def server_config():
    text = (ROOT / "config" / "server.toml").read_text(encoding="utf-8")
    text = re.sub(r"(\[RedisClient\].*?port\s*=\s*\")[^\"]+\"", lambda match: match.group(1) + str(REDIS[1]) + "\"", text, count=1, flags=re.S)
    path = BUILD / "server-test.toml"
    path.write_text(text, encoding="utf-8")
    return str(path)


def stop_redis():
    if REDIS_PROC is not None and REDIS_PROC.poll() is None:
        REDIS_PROC.kill()
        REDIS_PROC.wait(timeout=5)


def main():
    if not ensure_redis():
        stop_redis()
        return 1
    server = Proc([str(SERVER), server_config()])
    try:
        deadline = time.time() + 8
        while time.time() < deadline and not any("lanroom server listen" in line for line in server.lines):
            if server.proc.poll() is not None:
                print("".join(server.lines))
                return 1
            time.sleep(0.05)
        if not any("lanroom server listen" in line for line in server.lines):
            print("".join(server.lines))
            return 1
        case_room(server)
        case_address()
        case_forward()
        case_frames()
    finally:
        for proc in list(LIVE):
            proc.stop()
        stop_redis()
    print("failed", len(FAILS))
    return 1 if FAILS else 0


def case_room(server):
    redis_call("DEL", "alpha", "beta", "gamma")
    a, na = start_client("alpha", "02:00:00:00:00:01", 18101, "127.0.0.1")
    b, nb = start_client("alpha", "02:00:00:00:00:02", 18102, "127.0.0.2")
    room = wait_members("alpha", 2)
    check("1 same room one key", room is not None and len(room["members"]) == 2 and redis_call("EXISTS", "alpha") in (b":1", b":1\r"))
    cleanup(na, nb, a, b)
    c, nc = start_client("beta", "02:00:00:00:00:03", 18103)
    d, nd = start_client("gamma", "02:00:00:00:00:04", 18104)
    beta = wait_members("beta", 1)
    gamma = wait_members("gamma", 1)
    nc.write(b"only-beta")
    time.sleep(0.6)
    leaked = nd.read()
    check("2 rooms isolated", beta is not None and gamma is not None and leaked is None)
    cleanup(nc, nd, c, d)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.sendto(probe_frame(b"", bytes.fromhex("020000000003")), ("127.0.0.1", SERVER_PORT))
    time.sleep(0.3)
    check("3 empty room rejected", server.proc.poll() is None and (redis_call("EXISTS", "") in (b":0", None) or redis_call("GET", "") is None))
    check("4 client has no redis code", b"RedisClient" not in CLIENT.read_bytes())
    e, ne = start_client("alpha", "02:00:00:00:00:01", 18105, "127.0.0.1")
    again = wait_members("alpha", 1)
    port1 = again["members"][0]["port"] if again else 0
    cleanup(ne, e)
    f, nf = start_client("alpha", "02:00:00:00:00:01", 18106, "127.0.0.1")
    moved = wait_members("alpha", 1)
    check("5 rejoin keeps user", moved is not None and len(moved["members"]) == 1 and moved["members"][0]["port"] != port1)
    g, ng = start_client("alpha", "02:00:00:00:00:09", 18107, "127.0.0.3")
    wait_members("alpha", 2)
    cleanup(nf, f)
    time.sleep(2.5)
    left = wait_members("alpha", 1, timeout=2)
    check("6 heartbeat expiry", left is not None and len(left["members"]) == 1 and left["members"][0]["mac"] == bytes.fromhex("020000000009"))
    cleanup(ng, g)
    redis_call("DEL", "alpha")
    h, nh = start_client("alpha", "02:00:00:00:00:01", 18108, "127.0.0.1")
    wait_members("alpha", 1)
    nh.cmd("PEERS")
    time.sleep(0.5)
    once = decode_room(redis_call("GET", "alpha"))
    check("7 duplicate join stays one", once is not None and len(once["members"]) == 1)
    cleanup(nh, h)
    i, ni = start_client("alpha", "02:00:00:00:00:0a", 18109, "127.0.0.1")
    j, nj = start_client("alpha", "02:00:00:00:00:0a", 18110, "127.0.0.4")
    two_ip = wait_members("alpha", 2)
    check("8 different source ip", two_ip is not None and len({item["ip"] for item in two_ip["members"]}) == 2)
    cleanup(ni, nj, i, j)
    redis_call("DEL", "alpha")
    k, nk = start_client("alpha", "02:00:00:00:00:0b", 18111, "127.0.0.1")
    l, nl = start_client("alpha", "02:00:00:00:00:0c", 18112, "127.0.0.1")
    two_mac = wait_members("alpha", 2)
    check("9 different mac", two_mac is not None and two_mac["members"][0]["mac"] != two_mac["members"][1]["mac"] and two_mac["members"][0]["ip"] == two_mac["members"][1]["ip"])
    cleanup(nk, nl, k, l)
    sock.sendto(probe_frame(b"room", b"\x01\x02"), ("127.0.0.1", SERVER_PORT))
    time.sleep(0.3)
    check("10 bad mac rejected", server.proc.poll() is None and redis_call("GET", "room") is None)
    sock.close()
    redis_call("DEL", "alpha", "beta", "gamma", "room")


def case_address():
    redis_call("DEL", "alpha")
    a, na = start_client("alpha", "02:00:00:00:00:11", 18121, "127.0.0.1")
    b, nb = start_client("alpha", "02:00:00:00:00:12", 18122, "127.0.0.2")
    room = wait_members("alpha", 2)
    subnet = room is not None and all((item["virtual"] & 0xFFFFFF00) == 0x0A420000 for item in room["members"])
    distinct = room is not None and room["members"][0]["virtual"] != room["members"][1]["virtual"]
    kept = room["members"][0]["virtual"] if room else 0
    cleanup(na, a)
    c, nc = start_client("alpha", "02:00:00:00:00:11", 18123, "127.0.0.1")
    again = wait_members("alpha", 2)
    same = again and any(item["virtual"] == kept for item in again["members"])
    peer_lines = nc.peers()
    check("11 observed source ip", room is not None and {item["ip"] for item in room["members"]} == {0x7F000001, 0x7F000002})
    check("12 virtual addresses", bool(subnet and distinct and same and peer_lines))
    port_line = nc.cmd("PORT")
    local_port = int(port_line.split()[1]) if port_line.startswith("port ") else 0
    mine = next((item for item in again["members"] if item["mac"] == bytes.fromhex("020000000011")), None) if again else None
    check("14 registration port is the socket port", mine is not None and mine["port"] == local_port)
    cleanup(nb, nc, b, c)


def case_forward():
    redis_call("DEL", "alpha")
    a, na = start_client("alpha", "02:00:00:00:00:21", 18131, "127.0.0.1")
    b, nb = start_client("alpha", "02:00:00:00:00:22", 18132, "127.0.0.2")
    wait_members("alpha", 2)
    time.sleep(1.2)
    room = decode_room(redis_call("GET", "alpha"))
    told = na.peers()
    other = next(item for item in room["members"] if item["mac"] == bytes.fromhex("020000000022"))
    learned = any(str(other["port"]) in line and "127.0.0.2" in line for line in told)
    na.write(b"direct-frame")
    time.sleep(0.8)
    got = nb.read()
    after = decode_room(redis_call("GET", "alpha"))
    check("13 peer endpoint told", learned)
    check("15 direct skips relay", got == b"direct-frame" and after is not None and after["relay"] == 0)
    cleanup(na, nb, a, b)
    c, nc = start_client("alpha", "02:00:00:00:00:21", 18133, "127.0.0.1", drop=True)
    d, nd = start_client("alpha", "02:00:00:00:00:22", 18134, "127.0.0.2", drop=True)
    wait_members("alpha", 2)
    time.sleep(0.8)
    before = decode_room(redis_call("GET", "alpha"))["relay"]
    nc.write(b"relay-frame")
    time.sleep(0.8)
    got = nd.read()
    mid = decode_room(redis_call("GET", "alpha"))
    check("16 relay when punch blocked", got == b"relay-frame" and mid["relay"] > before)
    nc.cmd("DROP 0")
    nd.cmd("DROP 0")
    time.sleep(1.2)
    base = decode_room(redis_call("GET", "alpha"))["relay"]
    nc.write(b"back-direct")
    time.sleep(0.8)
    got = nd.read()
    later = decode_room(redis_call("GET", "alpha"))
    check("17 direct again", got == b"back-direct" and later["relay"] == base)
    cleanup(nc, nd, c, d)
    redis_call("DEL", "alone")
    e, ne = start_client("alone", "02:00:00:00:00:31", 18135)
    one = wait_members("alone", 1)
    ne.write(b"nobody")
    time.sleep(0.5)
    alone = decode_room(redis_call("GET", "alone"))
    check("18 single user does not relay", one is not None and alone is not None and alone["relay"] == 0 and alone["members"][0]["direct"] == 0)
    cleanup(ne, e)
    redis_call("DEL", "tcproom")
    f, nf = start_client("tcproom", "02:00:00:00:00:51", 18151, "127.0.0.1", tcp=True)
    g, ng = start_client("tcproom", "02:00:00:00:00:52", 18152, "127.0.0.2")
    wait_members("tcproom", 2)
    time.sleep(0.8)
    before = decode_room(redis_call("GET", "tcproom"))
    nf.write(b"tcp-frame")
    time.sleep(0.8)
    got = ng.read()
    after = decode_room(redis_call("GET", "tcproom"))
    stayed = after is not None and after["tcp"] == 1 and all(item["direct"] == 0 for item in after["members"])
    check("21 tcp room carries the frame", before is not None and before["tcp"] == 1 and got == b"tcp-frame" and stayed and after["relay"] > before["relay"])
    cleanup(nf, ng, f, g)
    redis_call("DEL", "tcpcast")
    h, nh = start_client("tcpcast", "02:00:00:00:00:61", 18161, "127.0.0.1", tcp=True)
    i, ni = start_client("tcpcast", "02:00:00:00:00:62", 18162, "127.0.0.2")
    j, nj = start_client("tcpcast", "02:00:00:00:00:63", 18163, "127.0.0.3")
    room = wait_members("tcpcast", 3)
    time.sleep(0.8)
    virt = {item["mac"]: item["virtual"] for item in (room["members"] if room else [])}
    target = virt.get(bytes.fromhex("020000000062"), 0)
    unicast = bytes([0x45]) + b"\x00" * 15 + target.to_bytes(4, "big") + b"to-62"
    nh.write(unicast)
    time.sleep(0.5)
    hit = ni.read()
    stray = nj.read()
    check("22 tcp unicast reaches only the target", target != 0 and hit == unicast and stray is None)
    spread = bytes([0x45]) + b"\x00" * 15 + bytes([255, 255, 255, 255]) + b"to-all"
    nh.write(spread)
    time.sleep(0.5)
    check("23 tcp broadcast reaches everyone else", ni.read() == spread and nj.read() == spread)
    cleanup(nh, ni, nj, h, i, j)


def case_frames():
    redis_call("DEL", "alpha")
    a, na = start_client("alpha", "02:00:00:00:00:41", 18141, "127.0.0.1")
    b, nb = start_client("alpha", "02:00:00:00:00:42", 18142, "127.0.0.2")
    wait_members("alpha", 2)
    time.sleep(1.2)
    na.write(b"one")
    na.write(b"two")
    time.sleep(0.8)
    first = nb.read()
    second = nb.read()
    check("19 two datagrams two frames", first == b"one" and second == b"two")
    before = decode_room(redis_call("GET", "alpha"))
    rejected = na.write(b"x" * 2000)
    time.sleep(0.3)
    after = decode_room(redis_call("GET", "alpha"))
    same = before is not None and after is not None and before["relay"] == after["relay"] and [(item["mac"], item["ip"], item["port"], item["virtual"]) for item in before["members"]] == [(item["mac"], item["ip"], item["port"], item["virtual"]) for item in after["members"]]
    check("20 oversize rejected", rejected == "fail" and same)
    cleanup(na, nb, a, b)


if __name__ == "__main__":
    sys.exit(main())
