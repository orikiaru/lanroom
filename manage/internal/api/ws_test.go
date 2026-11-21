package api

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"strings"
	"testing"
	"time"
)

type testWSClient struct {
	conn net.Conn
	br   *bufio.Reader
	bw   *bufio.Writer
}

func dialTestWS(t *testing.T, rawURL string) *testWSClient {
	t.Helper()
	addr := strings.TrimPrefix(rawURL, "http://")
	slash := strings.Index(addr, "/")
	host := addr[:slash]
	path := addr[slash:]

	conn, err := net.DialTimeout("tcp", host, 3*time.Second)
	if err != nil {
		t.Fatalf("dial %s: %v", host, err)
	}
	var keyBytes [16]byte
	_, _ = rand.Read(keyBytes[:])
	key := base64.StdEncoding.EncodeToString(keyBytes[:])
	req := "GET " + path + " HTTP/1.1\r\n" +
		"Host: " + host + "\r\n" +
		"Upgrade: websocket\r\n" +
		"Connection: Upgrade\r\n" +
		"Sec-WebSocket-Key: " + key + "\r\n" +
		"Sec-WebSocket-Version: 13\r\n\r\n"
	if _, err := conn.Write([]byte(req)); err != nil {
		_ = conn.Close()
		t.Fatalf("write upgrade: %v", err)
	}
	br := bufio.NewReader(conn)
	line, err := br.ReadString('\n')
	if err != nil {
		_ = conn.Close()
		t.Fatalf("read status: %v", err)
	}
	if !strings.Contains(line, "101") {
		_ = conn.Close()
		t.Fatalf("expected 101 Switching Protocols, got %q", line)
	}
	for {
		h, err := br.ReadString('\n')
		if err != nil {
			_ = conn.Close()
			t.Fatalf("read header: %v", err)
		}
		if h == "\r\n" {
			break
		}
	}
	return &testWSClient{conn: conn, br: br, bw: bufio.NewWriter(conn)}
}

func (c *testWSClient) close() {
	_ = c.conn.Close()
}

func (c *testWSClient) sendJSON(t *testing.T, v any) {
	t.Helper()
	payload, err := json.Marshal(v)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	var mask [4]byte
	_, _ = rand.Read(mask[:])
	hdr := []byte{0x81}
	n := len(payload)
	if n < 126 {
		hdr = append(hdr, 0x80|byte(n))
	} else {
		hdr = append(hdr, 0x80|126, byte(n>>8), byte(n))
	}
	hdr = append(hdr, mask[:]...)
	masked := make([]byte, n)
	for i := range payload {
		masked[i] = payload[i] ^ mask[i%4]
	}
	_ = c.conn.SetWriteDeadline(time.Now().Add(3 * time.Second))
	if _, err := c.bw.Write(hdr); err != nil {
		t.Fatalf("write hdr: %v", err)
	}
	if _, err := c.bw.Write(masked); err != nil {
		t.Fatalf("write payload: %v", err)
	}
	if err := c.bw.Flush(); err != nil {
		t.Fatalf("flush: %v", err)
	}
}

func (c *testWSClient) readJSON(t *testing.T) map[string]any {
	t.Helper()
	_ = c.conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	for {
		var hdr [2]byte
		if _, err := io.ReadFull(c.br, hdr[:]); err != nil {
			t.Fatalf("read frame hdr: %v", err)
		}
		op := hdr[0] & 0x0f
		plen := uint64(hdr[1] & 0x7f)
		if plen == 126 {
			var ext [2]byte
			if _, err := io.ReadFull(c.br, ext[:]); err != nil {
				t.Fatalf("read ext16: %v", err)
			}
			plen = uint64(binary.BigEndian.Uint16(ext[:]))
		} else if plen == 127 {
			var ext [8]byte
			if _, err := io.ReadFull(c.br, ext[:]); err != nil {
				t.Fatalf("read ext64: %v", err)
			}
			plen = binary.BigEndian.Uint64(ext[:])
		}
		buf := make([]byte, int(plen))
		if plen > 0 {
			if _, err := io.ReadFull(c.br, buf); err != nil {
				t.Fatalf("read payload: %v", err)
			}
		}
		if op == 0x9 { // ignore ping
			continue
		}
		if op != 0x1 {
			t.Fatalf("unexpected opcode %d", op)
		}
		var out map[string]any
		if err := json.Unmarshal(buf, &out); err != nil {
			t.Fatalf("unmarshal %q: %v", string(buf), err)
		}
		return out
	}
}

func TestPanelWebSocketRealtimeAndActions(t *testing.T) {
	srv, ts := testServer(t)

	// Unauthenticated WS upgrade should be rejected with 401
	resp, err := http.Get(ts.URL + "/api/ws")
	if err != nil {
		t.Fatalf("get /api/ws: %v", err)
	}
	_ = resp.Body.Close()
	if resp.StatusCode != http.StatusUnauthorized {
		t.Fatalf("expected 401 without token, got %d", resp.StatusCode)
	}

	code, loginBody := call(t, ts, http.MethodPost, "/api/login", "", map[string]string{"username": "admin@example.com", "password": "admin-pass"})
	if code != http.StatusOK {
		t.Fatalf("login failed: %d %v", code, loginBody)
	}
	adminToken := loginBody["token"].(string)
	ws := dialTestWS(t, ts.URL+"/api/ws?token="+adminToken)
	defer ws.close()

	// 1. Initial snapshot arrives immediately
	first := ws.readJSON(t)
	if first["type"] != "snapshot" {
		t.Fatalf("expected initial snapshot, got %v", first["type"])
	}
	if first["map"] == nil || first["rooms"] == nil {
		t.Fatalf("expected map & rooms in snapshot, got %v", first)
	}

	// 2. Ping / Pong round-trip
	ws.sendJSON(t, map[string]any{"type": "ping", "ts": 123456})
	pong := ws.readJSON(t)
	if pong["type"] != "pong" || int64(pong["ts"].(float64)) != 123456 {
		t.Fatalf("unexpected pong: %v", pong)
	}

	// 3. Create room over WS (no HTTP POST)
	ws.sendJSON(t, map[string]any{"type": "room:create", "id": "req-1", "name": "ws-lab"})
	var gotAck, gotSnap bool
	for !(gotAck && gotSnap) {
		msg := ws.readJSON(t)
		if msg["type"] == "ack" && msg["id"] == "req-1" {
			if msg["ok"] != true {
				t.Fatalf("room:create failed: %v", msg)
			}
			gotAck = true
		} else if msg["type"] == "snapshot" {
			gotSnap = true
		}
	}

	// 4. Issue room ticket over WS
	ws.sendJSON(t, map[string]any{"type": "room:ticket", "id": "req-2", "name": "ws-lab"})
	for {
		msg := ws.readJSON(t)
		if msg["type"] == "ack" && msg["id"] == "req-2" {
			data := msg["data"].(map[string]any)
			if msg["ok"] != true || data["token"] == "" {
				t.Fatalf("room:ticket failed: %v", msg)
			}
			break
		}
	}

	// 5. Simulate an agent coming online in Redis and verify WS pushes a live "join" event
	if err := srv.store.Set(context.Background(), liveKey("ws-lab"), "02:aa:bb:cc:dd:01 10.66.0.2 8.8.8.8:40001 1 0 0\n", 0); err != nil {
		t.Fatalf("seed live member: %v", err)
	}
	var sawJoin bool
	for i := 0; i < 4; i++ {
		msg := ws.readJSON(t)
		if msg["type"] != "snapshot" {
			continue
		}
		if evs, ok := msg["events"].([]any); ok {
			for _, rawEv := range evs {
				ev := rawEv.(map[string]any)
				if ev["kind"] == "join" && ev["room"] == "ws-lab" && ev["virtualIp"] == "10.66.0.2" {
					sawJoin = true
					break
				}
			}
		}
		if sawJoin {
			break
		}
	}
	if !sawJoin {
		t.Fatalf("expected WS push with live join event")
	}

	// 6. Query members & kick over WS
	ws.sendJSON(t, map[string]any{"type": "room:members", "id": "req-3", "name": "ws-lab"})
	for {
		msg := ws.readJSON(t)
		if msg["type"] == "ack" && msg["id"] == "req-3" {
			data := msg["data"].(map[string]any)
			items := data["items"].([]any)
			if len(items) != 1 {
				t.Fatalf("expected 1 member, got %d", len(items))
			}
			break
		}
	}

	ws.sendJSON(t, map[string]any{"type": "kick", "id": "req-4", "room": "ws-lab", "mac": "02:aa:bb:cc:dd:01"})
	for {
		msg := ws.readJSON(t)
		if msg["type"] == "ack" && msg["id"] == "req-4" {
			if msg["ok"] != true {
				t.Fatalf("kick failed: %v", msg)
			}
			break
		}
	}
	kicked, _ := srv.store.Get(context.Background(), kickKey("ws-lab", "02:aa:bb:cc:dd:01"))
	if kicked != "1" {
		t.Fatalf("expected kick key set in store, got %q", kicked)
	}
}
