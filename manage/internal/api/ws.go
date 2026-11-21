package api

import (
	"bufio"
	"context"
	"crypto/sha1"
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/gin-gonic/gin"
)

const (
	wsMagicGUID     = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
	wsMaxFrameBytes = 256 * 1024
)

type wsConn struct {
	netConn net.Conn
	br      *bufio.Reader
	bw      *bufio.Writer
	wmu     sync.Mutex
	closed  bool
}

func upgradeWebSocket(w http.ResponseWriter, r *http.Request) (*wsConn, error) {
	if !headerContainsToken(r.Header.Get("Connection"), "upgrade") ||
		!strings.EqualFold(strings.TrimSpace(r.Header.Get("Upgrade")), "websocket") {
		return nil, fail(http.StatusBadRequest, "无效的 WebSocket 升级请求")
	}
	key := strings.TrimSpace(r.Header.Get("Sec-WebSocket-Key"))
	if key == "" {
		return nil, fail(http.StatusBadRequest, "缺少 Sec-WebSocket-Key")
	}
	hijacker, ok := w.(http.Hijacker)
	if !ok {
		return nil, errors.New("http.Hijacker not supported")
	}
	conn, rw, err := hijacker.Hijack()
	if err != nil {
		return nil, err
	}
	h := sha1.Sum([]byte(key + wsMagicGUID))
	accept := base64.StdEncoding.EncodeToString(h[:])
	resp := "HTTP/1.1 101 Switching Protocols\r\n" +
		"Upgrade: websocket\r\n" +
		"Connection: Upgrade\r\n" +
		"Sec-WebSocket-Accept: " + accept + "\r\n\r\n"
	if _, err := rw.WriteString(resp); err != nil {
		_ = conn.Close()
		return nil, err
	}
	if err := rw.Flush(); err != nil {
		_ = conn.Close()
		return nil, err
	}
	return &wsConn{
		netConn: conn,
		br:      rw.Reader,
		bw:      rw.Writer,
	}, nil
}

func headerContainsToken(header, token string) bool {
	for _, part := range strings.Split(header, ",") {
		if strings.EqualFold(strings.TrimSpace(part), token) {
			return true
		}
	}
	return false
}

func (c *wsConn) close() {
	c.wmu.Lock()
	if c.closed {
		c.wmu.Unlock()
		return
	}
	c.closed = true
	_ = c.writeFrameLocked(0x8, nil)
	c.wmu.Unlock()
	_ = c.netConn.Close()
}

func (c *wsConn) writeJSON(v any) error {
	raw, err := json.Marshal(v)
	if err != nil {
		return err
	}
	return c.writeFrame(0x1, raw)
}

func (c *wsConn) writeFrame(opcode byte, payload []byte) error {
	c.wmu.Lock()
	defer c.wmu.Unlock()
	if c.closed {
		return net.ErrClosed
	}
	return c.writeFrameLocked(opcode, payload)
}

func (c *wsConn) writeFrameLocked(opcode byte, payload []byte) error {
	_ = c.netConn.SetWriteDeadline(time.Now().Add(10 * time.Second))
	header := make([]byte, 0, 10)
	header = append(header, 0x80|(opcode&0x0f))
	n := len(payload)
	switch {
	case n < 126:
		header = append(header, byte(n))
	case n <= 65535:
		header = append(header, 126, byte(n>>8), byte(n))
	default:
		header = append(header, 127)
		var lenBuf [8]byte
		binary.BigEndian.PutUint64(lenBuf[:], uint64(n))
		header = append(header, lenBuf[:]...)
	}
	if _, err := c.bw.Write(header); err != nil {
		return err
	}
	if n > 0 {
		if _, err := c.bw.Write(payload); err != nil {
			return err
		}
	}
	return c.bw.Flush()
}

func (c *wsConn) readMessage() (byte, []byte, error) {
	var msgOp byte
	var buf []byte
	for {
		_ = c.netConn.SetReadDeadline(time.Now().Add(75 * time.Second))
		var hdr [2]byte
		if _, err := io.ReadFull(c.br, hdr[:]); err != nil {
			return 0, nil, err
		}
		fin := (hdr[0] & 0x80) != 0
		op := hdr[0] & 0x0f
		masked := (hdr[1] & 0x80) != 0
		plen := uint64(hdr[1] & 0x7f)
		switch plen {
		case 126:
			var ext [2]byte
			if _, err := io.ReadFull(c.br, ext[:]); err != nil {
				return 0, nil, err
			}
			plen = uint64(binary.BigEndian.Uint16(ext[:]))
		case 127:
			var ext [8]byte
			if _, err := io.ReadFull(c.br, ext[:]); err != nil {
				return 0, nil, err
			}
			plen = binary.BigEndian.Uint64(ext[:])
		}
		if plen > wsMaxFrameBytes {
			return 0, nil, errors.New("frame too large")
		}
		var maskKey [4]byte
		if masked {
			if _, err := io.ReadFull(c.br, maskKey[:]); err != nil {
				return 0, nil, err
			}
		}
		payload := make([]byte, int(plen))
		if plen > 0 {
			if _, err := io.ReadFull(c.br, payload); err != nil {
				return 0, nil, err
			}
			if masked {
				for i := range payload {
					payload[i] ^= maskKey[i%4]
				}
			}
		}
		// Control frames can be interleaved inside fragmented messages
		switch op {
		case 0x8: // Close
			return 0x8, payload, io.EOF
		case 0x9: // Ping
			_ = c.writeFrame(0xA, payload)
			continue
		case 0xA: // Pong
			continue
		case 0x0: // Continuation
			if msgOp == 0 {
				return 0, nil, errors.New("unexpected continuation frame")
			}
			buf = append(buf, payload...)
		case 0x1, 0x2: // Text / Binary
			msgOp = op
			buf = append(buf[:0], payload...)
		default:
			return 0, nil, fmt.Errorf("unsupported opcode %d", op)
		}
		if len(buf) > wsMaxFrameBytes {
			return 0, nil, errors.New("message too large")
		}
		if fin {
			return msgOp, buf, nil
		}
	}
}

type wsClientMsg struct {
	Type  string `json:"type"`
	ID    string `json:"id,omitempty"`
	Room  string `json:"room,omitempty"`
	Query string `json:"query,omitempty"`
	Name  string `json:"name,omitempty"`
	MAC   string `json:"mac,omitempty"`
	TS    int64  `json:"ts,omitempty"`
}

type wsMemberBrief struct {
	ID        string
	Room      string
	VirtualIP string
	City      string
	Country   string
	Mode      string
	Source    string
}

type wsLiveEvent struct {
	ID        string `json:"id"`
	Kind      string `json:"kind"` // "join" | "leave" | "mode_change"
	Room      string `json:"room"`
	MemberID  string `json:"memberId"`
	VirtualIP string `json:"virtualIp"`
	City      string `json:"city"`
	Country   string `json:"country"`
	Mode      string `json:"mode"`
	PrevMode  string `json:"prevMode,omitempty"`
	Source    string `json:"source"`
	TS        string `json:"ts"`
}

func extractMemberBriefs(mapPayload map[string]any) map[string]wsMemberBrief {
	out := map[string]wsMemberBrief{}
	if mapPayload == nil {
		return out
	}
	raw, ok := mapPayload["members"].([]map[string]any)
	if !ok {
		return out
	}
	for _, m := range raw {
		id, _ := m["id"].(string)
		if id == "" {
			continue
		}
		room, _ := m["room"].(string)
		vip, _ := m["virtualIp"].(string)
		city, _ := m["city"].(string)
		country, _ := m["country"].(string)
		mode, _ := m["mode"].(string)
		src, _ := m["source"].(string)
		out[id] = wsMemberBrief{
			ID: id, Room: room, VirtualIP: vip, City: city, Country: country, Mode: mode, Source: src,
		}
	}
	return out
}

func diffMemberEvents(prev, curr map[string]wsMemberBrief, ts string, seq int64) []wsLiveEvent {
	var events []wsLiveEvent
	for id, cur := range curr {
		old, existed := prev[id]
		if !existed {
			events = append(events, wsLiveEvent{
				ID:        fmt.Sprintf("join:%s:%d", id, seq),
				Kind:      "join",
				Room:      cur.Room,
				MemberID:  id,
				VirtualIP: cur.VirtualIP,
				City:      cur.City,
				Country:   cur.Country,
				Mode:      cur.Mode,
				Source:    cur.Source,
				TS:        ts,
			})
		} else if old.Mode != cur.Mode {
			events = append(events, wsLiveEvent{
				ID:        fmt.Sprintf("mode:%s:%s:%d", id, cur.Mode, seq),
				Kind:      "mode_change",
				Room:      cur.Room,
				MemberID:  id,
				VirtualIP: cur.VirtualIP,
				City:      cur.City,
				Country:   cur.Country,
				Mode:      cur.Mode,
				PrevMode:  old.Mode,
				Source:    cur.Source,
				TS:        ts,
			})
		}
	}
	for id, old := range prev {
		if _, exists := curr[id]; !exists {
			events = append(events, wsLiveEvent{
				ID:        fmt.Sprintf("leave:%s:%d", id, seq),
				Kind:      "leave",
				Room:      old.Room,
				MemberID:  id,
				VirtualIP: old.VirtualIP,
				City:      old.City,
				Country:   old.Country,
				Mode:      old.Mode,
				Source:    old.Source,
				TS:        ts,
			})
		}
	}
	return events
}

func (s *Server) panelWS(c *gin.Context) {
	rawToken := strings.TrimSpace(c.Query("token"))
	if rawToken == "" {
		header := c.GetHeader("Authorization")
		if strings.HasPrefix(header, "Bearer ") {
			rawToken = strings.TrimSpace(strings.TrimPrefix(header, "Bearer "))
		}
	}
	act, err := s.authToken(rawToken)
	if err != nil {
		writeErr(c, err)
		return
	}

	ws, err := upgradeWebSocket(c.Writer, c.Request)
	if err != nil {
		writeErr(c, err)
		return
	}
	defer ws.close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	var stateMu sync.Mutex
	roomFilter := strings.TrimSpace(c.Query("room"))
	roomQuery := strings.TrimSpace(c.Query("q"))
	lastDigest := ""
	var prevMembers map[string]wsMemberBrief
	var seq int64

	forceCh := make(chan bool, 4)
	triggerPush := func(resetDiff bool) {
		select {
		case forceCh <- resetDiff:
		default:
		}
	}

	pushSnapshot := func(force bool, resetDiff bool) error {
		stateMu.Lock()
		rf := roomFilter
		rq := roomQuery
		stateMu.Unlock()

		// Refresh actor permissions & active status so revoked sessions close promptly
		liveActor, authErr := s.authToken(rawToken)
		if authErr != nil {
			_ = ws.writeJSON(map[string]any{"type": "unauthorized", "detail": authErr.Error()})
			return authErr
		}
		act = liveActor

		var mapPayload map[string]any
		if allows(act.perms, "map:read") {
			mp, err := s.buildWorldMap(ctx, act, rf)
			if err != nil {
				return err
			}
			mapPayload = mp
		}
		var roomsPayload []map[string]any
		if allows(act.perms, "room:read") {
			rp, err := s.buildRooms(ctx, act, rq)
			if err != nil {
				return err
			}
			roomsPayload = rp
		}

		body := map[string]any{
			"map":   mapPayload,
			"rooms": roomsPayload,
			"room":  rf,
			"query": rq,
		}
		encoded, err := json.Marshal(body)
		if err != nil {
			return err
		}
		sum := sha256.Sum256(encoded)
		digest := hex.EncodeToString(sum[:])

		stateMu.Lock()
		changed := digest != lastDigest
		if !force && !changed {
			stateMu.Unlock()
			return nil
		}
		seq++
		curSeq := seq
		ts := stamp(s.now())
		currMembers := extractMemberBriefs(mapPayload)
		var events []wsLiveEvent
		if prevMembers != nil && !resetDiff {
			events = diffMemberEvents(prevMembers, currMembers, ts, curSeq)
		}
		prevMembers = currMembers
		lastDigest = digest
		stateMu.Unlock()

		frame := map[string]any{
			"type":   "snapshot",
			"seq":    curSeq,
			"ts":     ts,
			"room":   rf,
			"query":  rq,
			"map":    mapPayload,
			"rooms":  roomsPayload,
			"events": events,
		}
		return ws.writeJSON(frame)
	}

	// Send initial snapshot immediately upon upgrade
	if err := pushSnapshot(true, true); err != nil {
		return
	}

	// Background push + heartbeat loop
	done := make(chan struct{})
	go func() {
		defer close(done)
		pollTicker := time.NewTicker(1 * time.Second)
		pingTicker := time.NewTicker(25 * time.Second)
		defer pollTicker.Stop()
		defer pingTicker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case resetDiff := <-forceCh:
				if err := pushSnapshot(true, resetDiff); err != nil {
					cancel()
					return
				}
			case <-pollTicker.C:
				if err := pushSnapshot(false, false); err != nil {
					cancel()
					return
				}
			case <-pingTicker.C:
				if err := ws.writeFrame(0x9, []byte("hb")); err != nil {
					cancel()
					return
				}
			}
		}
	}()

	// Inbound WebSocket command reader
	for {
		op, payload, err := ws.readMessage()
		if err != nil {
			cancel()
			break
		}
		if op != 0x1 {
			continue
		}
		var msg wsClientMsg
		if err := json.Unmarshal(payload, &msg); err != nil {
			_ = ws.writeJSON(map[string]any{"type": "error", "detail": "无效的指令格式"})
			continue
		}

		replyAck := func(ok bool, data any, err error) {
			resp := map[string]any{
				"type": "ack",
				"id":   msg.ID,
				"ok":   ok,
			}
			if data != nil {
				resp["data"] = data
			}
			if err != nil {
				resp["error"] = err.Error()
			}
			_ = ws.writeJSON(resp)
		}

		switch msg.Type {
		case "ping":
			_ = ws.writeJSON(map[string]any{
				"type":     "pong",
				"ts":       msg.TS,
				"serverTs": stamp(s.now()),
			})
		case "subscribe":
			stateMu.Lock()
			roomFilter = strings.TrimSpace(msg.Room)
			roomQuery = strings.TrimSpace(msg.Query)
			stateMu.Unlock()
			if msg.ID != "" {
				replyAck(true, map[string]any{"room": roomFilter, "query": roomQuery}, nil)
			}
			triggerPush(true)
		case "refresh":
			triggerPush(false)
		case "kick":
			err := s.doKickMember(ctx, act, msg.Room, msg.MAC)
			replyAck(err == nil, map[string]any{"ok": err == nil}, err)
			if err == nil {
				triggerPush(false)
			}
		case "room:create":
			name, err := s.doCreateRoom(ctx, act, msg.Name)
			replyAck(err == nil, map[string]any{"name": name}, err)
			if err == nil {
				triggerPush(false)
			}
		case "room:close":
			err := s.doCloseRoom(ctx, act, msg.Name)
			replyAck(err == nil, map[string]any{"ok": err == nil}, err)
			if err == nil {
				triggerPush(false)
			}
		case "room:ticket":
			tok, exp, err := s.doIssueTicket(act, msg.Name)
			if err != nil {
				replyAck(false, nil, err)
			} else {
				replyAck(true, map[string]any{"token": tok, "expiresAt": exp.UTC().Format(time.RFC3339)}, nil)
			}
		case "room:members":
			items, err := s.buildRoomMembers(ctx, act, msg.Name)
			if err != nil {
				replyAck(false, nil, err)
			} else {
				replyAck(true, map[string]any{"room": msg.Name, "items": items}, nil)
			}
		default:
			if msg.ID != "" {
				replyAck(false, nil, fmt.Errorf("未知指令: %s", msg.Type))
			}
		}
	}
	<-done
}
