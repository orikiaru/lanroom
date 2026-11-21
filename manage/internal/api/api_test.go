package api

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

func testServer(t *testing.T) (*Server, *httptest.Server) {
	t.Helper()
	dir := t.TempDir()
	fixture := filepath.Join(dir, "geo.json")
	raw := []byte(`{"8.8.8.8":{"country":"US","city":"Mountain View","lat":37.386,"lon":-122.084},"1.1.1.1":{"country":"AU","city":"Sydney","lat":-33.8688,"lon":151.2093},"49.233.79.122":{"country":"CN","city":"Beijing","lat":39.9042,"lon":116.4074},"127.0.0.1":{"country":"CN","city":"Local","lat":31.23,"lon":121.47}}`)
	if err := os.WriteFile(fixture, raw, 0o644); err != nil {
		t.Fatal(err)
	}
	server, err := New(Config{
		DatabaseURL:   "sqlite://" + filepath.Join(dir, "lanroom.db"),
		RedisURL:      "memory://",
		AuthSecret:    "test-secret",
		AdminUser:     "admin@example.com",
		AdminPassword: "admin-pass",
		GeoIPFixture:  fixture,
		ServerIP:      "49.233.79.122",
		ServerName:    "Beijing Core",
	})
	if err != nil {
		t.Fatal(err)
	}
	return server, httptest.NewServer(server.Handler())
}

func call(t *testing.T, ts *httptest.Server, method, path, token string, body any) (int, map[string]any) {
	t.Helper()
	var reader io.Reader
	if body != nil {
		raw, err := json.Marshal(body)
		if err != nil {
			t.Fatal(err)
		}
		reader = bytes.NewReader(raw)
	}
	req, err := http.NewRequest(method, ts.URL+path, reader)
	if err != nil {
		t.Fatal(err)
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	res, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer res.Body.Close()
	var decoded map[string]any
	if err := json.NewDecoder(res.Body).Decode(&decoded); err != nil {
		t.Fatalf("%s %s: %v", method, path, err)
	}
	return res.StatusCode, decoded
}

func auth(t *testing.T, ts *httptest.Server, username, password string) (string, map[string]any) {
	t.Helper()
	code, body := call(t, ts, http.MethodPost, "/api/login", "", map[string]string{"username": username, "password": password})
	if code != 200 {
		t.Fatalf("login %s: %d %v", username, code, body)
	}
	token, _ := body["token"].(string)
	user, _ := body["user"].(map[string]any)
	return token, user
}

func TestMemberOperatorMapJWTAndLoginLog(t *testing.T) {
	server, ts := testServer(t)
	defer ts.Close()

	adminToken, adminUser := auth(t, ts, "admin@example.com", "admin-pass")
	perms, _ := adminUser["permissions"].([]any)
	if !hasPerm(perms, "*:*") {
		t.Fatalf("admin permissions: %v", perms)
	}
	if parts := strings.Split(adminToken, "."); len(parts) != 3 {
		t.Fatalf("jwt parts: %d", len(parts))
	}

	code, body := call(t, ts, http.MethodPost, "/api/users", adminToken, map[string]string{
		"username": "ops", "password": "ops-pass", "display_name": "运维", "role_key": "operator",
	})
	if code != 200 {
		t.Fatalf("create ops: %d %v", code, body)
	}

	code, body = call(t, ts, http.MethodPost, "/api/register", "", map[string]string{
		"username": "ada", "password": "ada-pass", "display_name": "Ada",
	})
	if code != 200 {
		t.Fatalf("register ada: %d %v", code, body)
	}
	memberToken, memberUser := auth(t, ts, "ada", "ada-pass")
	memberPerms, _ := memberUser["permissions"].([]any)
	if hasPerm(memberPerms, "room:manage") {
		t.Fatal("member has room:manage")
	}
	if code, body = call(t, ts, http.MethodGet, "/api/users", memberToken, nil); code != 403 {
		t.Fatalf("member users: %d %v", code, body)
	}

	if code, body = call(t, ts, http.MethodPost, "/api/rooms", memberToken, map[string]string{"name": "studio"}); code != 200 {
		t.Fatalf("create room: %d %v", code, body)
	}
	owner, ok := server.store.Get(t.Context(), roomKey("studio"))
	memberID, _ := memberUser["id"].(float64)
	if !ok || owner != strconv.FormatInt(int64(memberID), 10) {
		t.Fatalf("redis owner %q ok=%v id=%v", owner, ok, memberUser["id"])
	}
	code, body = call(t, ts, http.MethodPost, "/api/rooms/studio/ticket", memberToken, nil)
	if code != 200 || body["token"] == "" {
		t.Fatalf("ticket: %d %v", code, body)
	}
	ticket, _ := body["token"].(string)
	payload, err := base64.RawURLEncoding.DecodeString(strings.Split(ticket, ".")[0])
	if err != nil {
		t.Fatal(err)
	}
	lines := strings.Split(string(payload), "\n")
	if len(lines) != 3 || lines[0] != "studio" || lines[1] != strconv.FormatInt(int64(memberID), 10) {
		t.Fatalf("ticket payload %q", payload)
	}

	if code, body = call(t, ts, http.MethodPost, "/api/register", "", map[string]string{"username": "bea", "password": "bea-pass"}); code != 200 {
		t.Fatalf("register bea: %d %v", code, body)
	}
	beaToken, _ := auth(t, ts, "bea", "bea-pass")
	if code, _ = call(t, ts, http.MethodPost, "/api/rooms/studio/close", beaToken, nil); code != 403 {
		t.Fatalf("bea close: %d", code)
	}
	if code, _ = call(t, ts, http.MethodGet, "/api/rooms/studio/members", beaToken, nil); code != 403 {
		t.Fatalf("bea members: %d", code)
	}

	opsToken, _ := auth(t, ts, "ops", "ops-pass")
	code, body = call(t, ts, http.MethodGet, "/api/rooms", opsToken, nil)
	items, _ := body["items"].([]any)
	if code != 200 || len(items) == 0 || items[0].(map[string]any)["name"] != "studio" {
		t.Fatalf("ops rooms: %d %v", code, body)
	}
	server.store.Set(t.Context(), liveKey("studio"), "02:11:22:33:44:55 10.66.0.1 8.8.8.8:4242 1 0 0\n02:aa:bb:cc:dd:ee 10.66.0.2 1.1.1.1:5353 1 0 0\n", 0)
	code, body = call(t, ts, http.MethodGet, "/api/map", opsToken, nil)
	if code != 200 {
		t.Fatalf("map: %d %v", code, body)
	}
	srv, _ := body["server"].(map[string]any)
	if srv == nil || srv["city"] != "Beijing" || srv["name"] != "Beijing Core" {
		t.Fatalf("server node missing or wrong: %v", srv)
	}
	links, _ := body["links"].([]any)
	hasDirect := false
	hasControl := false
	for _, rawLink := range links {
		link := rawLink.(map[string]any)
		if link["kind"] == "direct" {
			hasDirect = true
		}
		if link["kind"] == "control" {
			hasControl = true
		}
	}
	if !hasDirect || !hasControl {
		t.Fatalf("expected direct and control links, got: %v", links)
	}
	members, _ := body["members"].([]any)
	point := members[0].(map[string]any)
	if point["city"] != "Mountain View" || point["lat"] != 37.386 || point["mode"] != "direct" {
		t.Fatalf("point: %v", point)
	}
	countries, _ := body["countries"].([]any)
	if countries[0].(map[string]any)["country"] != "US" {
		t.Fatalf("countries: %v", countries)
	}

	// Verify relay mode links when direct=0 or tcpRelay=1
	server.store.Set(t.Context(), liveKey("studio"), "02:11:22:33:44:55 10.66.0.1 8.8.8.8:4242 0 1 15\n02:aa:bb:cc:dd:ee 10.66.0.2 1.1.1.1:5353 0 1 15\n", 0)
	code, body = call(t, ts, http.MethodGet, "/api/map", opsToken, nil)
	if code != 200 {
		t.Fatalf("relay map: %d %v", code, body)
	}
	relayLinks := 0
	for _, rawLink := range body["links"].([]any) {
		if rawLink.(map[string]any)["kind"] == "relay" {
			relayLinks++
		}
	}
	if relayLinks != 2 {
		t.Fatalf("expected 2 relay links, got %d: %v", relayLinks, body["links"])
	}

	// Verify backward-compatible 3-column LiveText
	server.store.Set(t.Context(), liveKey("studio"), "02:11:22:33:44:55 10.66.0.1 8.8.8.8:4242\n", 0)
	code, body = call(t, ts, http.MethodGet, "/api/map", memberToken, nil)
	memberRows, _ := body["members"].([]any)
	if code != 200 || memberRows[0].(map[string]any)["room"] != "studio" {
		t.Fatalf("member map: %d %v", code, body)
	}
	if memberLogins, _ := body["logins"].([]any); len(memberLogins) != 0 {
		t.Fatalf("member should not see login logs on /api/map: %v", memberLogins)
	}
	code, body = call(t, ts, http.MethodGet, "/api/map", adminToken, nil)
	if adminLogins, _ := body["logins"].([]any); code != 200 || len(adminLogins) == 0 {
		t.Fatalf("admin should see login logs on /api/map: %d %v", code, body)
	}
	if code, _ = call(t, ts, http.MethodPost, "/api/rooms/studio/kick", opsToken, map[string]string{"mac": "02:AA:BB:CC:DD:EE"}); code != 200 {
		t.Fatalf("kick uppercase mac: %d", code)
	}
	if _, ok = server.store.Get(t.Context(), kickKey("studio", "02:aa:bb:cc:dd:ee")); !ok {
		t.Fatal("kick key was not normalized to lowercase mac")
	}
	if code, _ = call(t, ts, http.MethodPost, "/api/users", adminToken, map[string]string{"username": "a", "password": "123", "role_key": "member"}); code != 400 {
		t.Fatalf("short user/pass should fail: %d", code)
	}
	if code, _ = call(t, ts, http.MethodPut, "/api/roles/member", adminToken, map[string]any{"name": "成员", "permissions": []string{"room:read", "room:read", "room:write", "map:read"}}); code != 200 {
		t.Fatalf("duplicate role permissions should be deduplicated: %d", code)
	}

	if code, _ = call(t, ts, http.MethodPost, "/api/rooms/studio/close", opsToken, nil); code != 200 {
		t.Fatalf("close: %d", code)
	}
	if _, ok = server.store.Get(t.Context(), roomKey("studio")); ok {
		t.Fatal("room key still set")
	}
	if code, _ = call(t, ts, http.MethodPost, "/api/rooms/studio/ticket", memberToken, nil); code != 404 {
		t.Fatalf("ticket after close: %d", code)
	}

	tampered := adminToken[:len(adminToken)-1] + flip(adminToken[len(adminToken)-1])
	if code, body = call(t, ts, http.MethodGet, "/api/me", tampered, nil); code != 401 {
		t.Fatalf("bad signature: %d %v", code, body)
	}
	if code, _ = call(t, ts, http.MethodGet, "/api/me", "opaque-token", nil); code != 401 {
		t.Fatalf("opaque token: %d", code)
	}

	if code, _ = call(t, ts, http.MethodPost, "/api/login", "", map[string]string{"username": "ada", "password": "wrong-pass"}); code != 401 {
		t.Fatalf("bad password: %d", code)
	}
	if code, _ = call(t, ts, http.MethodPost, "/api/logout", adminToken, nil); code != 200 {
		t.Fatalf("logout: %d", code)
	}
	if code, _ = call(t, ts, http.MethodGet, "/api/me", adminToken, nil); code != 401 {
		t.Fatalf("me after logout: %d", code)
	}

	adminToken, _ = auth(t, ts, "admin@example.com", "admin-pass")
	code, body = call(t, ts, http.MethodGet, "/api/login-logs", adminToken, nil)
	if code != 200 {
		t.Fatalf("logs: %d %v", code, body)
	}
	seen := map[string]bool{}
	for _, item := range body["items"].([]any) {
		row := item.(map[string]any)
		seen[row["username"].(string)+" "+row["result"].(string)] = true
	}
	for _, key := range []string{"admin@example.com ok", "ada fail", "admin@example.com logout"} {
		if !seen[key] {
			t.Fatalf("missing %s in %v", key, seen)
		}
	}
}

func hasPerm(perms []any, code string) bool {
	for _, perm := range perms {
		if perm == code {
			return true
		}
	}
	return false
}

func flip(b byte) string {
	if b == 'a' {
		return "b"
	}
	return "a"
}
