package api

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
)

const pageSize = 20

var (
	permissions = [][2]string{
		{"user:read", "查看用户"},
		{"user:write", "管理用户"},
		{"role:read", "查看角色"},
		{"role:write", "管理角色"},
		{"room:read", "查看房间"},
		{"room:write", "创建自己的房间"},
		{"room:manage", "管理全部房间"},
		{"member:kick", "移出成员"},
		{"map:read", "查看全球地图"},
	}
	roles = []struct {
		key   string
		name  string
		codes []string
	}{
		{"member", "成员", []string{"room:read", "room:write", "map:read"}},
		{"operator", "运维", []string{"room:read", "room:write", "room:manage", "member:kick", "map:read"}},
		{"admin", "管理员", []string{"*:*"}},
	}
)

type httpError struct {
	code int
	msg  string
}

func (e httpError) Error() string { return e.msg }

func fail(code int, msg string) error { return httpError{code: code, msg: msg} }

func writeJSON(c *gin.Context, code int, value any) {
	c.JSON(code, value)
}

func writeErr(c *gin.Context, err error) {
	var httpErr httpError
	if errors.As(err, &httpErr) {
		c.JSON(httpErr.code, gin.H{"detail": httpErr.msg})
		return
	}
	c.JSON(http.StatusInternalServerError, gin.H{"detail": "请求失败"})
}

func readJSON(c *gin.Context, dest any) error {
	if c.Request.Body == nil || c.Request.ContentLength == 0 {
		return nil
	}
	if err := json.NewDecoder(c.Request.Body).Decode(dest); err != nil && !errors.Is(err, io.EOF) {
		return fail(http.StatusBadRequest, "请求格式不正确")
	}
	return nil
}

func clientIP(r *http.Request) string {
	if forwarded := r.Header.Get("X-Forwarded-For"); forwarded != "" {
		return strings.TrimSpace(strings.Split(forwarded, ",")[0])
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func queryInt(c *gin.Context, name string, fallback int) int {
	value, err := strconv.Atoi(c.Query(name))
	if err != nil || value < 1 {
		return fallback
	}
	return value
}

func allows(perms []string, code string) bool {
	for _, perm := range perms {
		if perm == "*:*" || perm == code {
			return true
		}
	}
	return false
}

func validRoom(name string) bool {
	if name == "" || len(name) > 64 || strings.Contains(name, ":") {
		return false
	}
	for _, ch := range name {
		if ch < 32 || ch == 127 {
			return false
		}
	}
	return true
}

func knownRole(key string) bool {
	for _, role := range roles {
		if role.key == key {
			return true
		}
	}
	return false
}

func knownPermission(code string) bool {
	if code == "*:*" {
		return true
	}
	for _, item := range permissions {
		if item[0] == code {
			return true
		}
	}
	return false
}

type actor struct {
	user  userRow
	perms []string
}

type userRow struct {
	ID          int64
	Username    string
	DisplayName string
	Password    string
	Role        string
	Active      bool
	LastLogin   sql.NullTime
	LastIP      string
	CreatedAt   time.Time
}

func (s *Server) seed() error {
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	for _, role := range roles {
		var id int64
		err := tx.QueryRow(s.q(`SELECT id FROM roles WHERE key = ?`), role.key).Scan(&id)
		if errors.Is(err, sql.ErrNoRows) {
			if s.postgres {
				err = tx.QueryRow(s.q(`INSERT INTO roles (key, name, is_system) VALUES (?, ?, ?) RETURNING id`), role.key, role.name, true).Scan(&id)
			} else {
				res, insertErr := tx.Exec(s.q(`INSERT INTO roles (key, name, is_system) VALUES (?, ?, ?)`), role.key, role.name, true)
				if insertErr != nil {
					return insertErr
				}
				id, err = res.LastInsertId()
			}
			if err != nil {
				return err
			}
			for _, code := range role.codes {
				if _, err := tx.Exec(s.q(`INSERT INTO role_permissions (role_id, permission) VALUES (?, ?)`), id, code); err != nil {
					return err
				}
			}
		} else if err != nil {
			return err
		}
	}
	var count int
	if err := tx.QueryRow(`SELECT COUNT(*) FROM users`).Scan(&count); err != nil {
		return err
	}
	if count == 0 && s.adminPass != "" {
		hash, err := hashPassword(s.adminPass)
		if err != nil {
			return err
		}
		if _, err := tx.Exec(s.q(`INSERT INTO users (username, display_name, password_hash, role_key, is_active, last_login_ip, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)`),
			s.adminUser, "管理员", hash, "admin", true, "", s.now()); err != nil {
			return err
		}
	}
	return tx.Commit()
}

func (s *Server) permissionsFor(roleKey string) ([]string, error) {
	rows, err := s.db.Query(s.q(`SELECT permission FROM role_permissions WHERE role_id = (SELECT id FROM roles WHERE key = ?)`), roleKey)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	perms := []string{}
	for rows.Next() {
		var code string
		if err := rows.Scan(&code); err != nil {
			return nil, err
		}
		perms = append(perms, code)
	}
	return perms, rows.Err()
}

func (s *Server) loadUser(id int64) (userRow, error) {
	row := s.db.QueryRow(s.q(`SELECT id, username, display_name, password_hash, role_key, is_active, last_login_at, last_login_ip, created_at FROM users WHERE id = ?`), id)
	return scanUser(row)
}

func (s *Server) userByName(name string) (userRow, error) {
	row := s.db.QueryRow(s.q(`SELECT id, username, display_name, password_hash, role_key, is_active, last_login_at, last_login_ip, created_at FROM users WHERE username = ?`), name)
	return scanUser(row)
}

type scanner interface{ Scan(dest ...any) error }

func scanUser(row scanner) (userRow, error) {
	var user userRow
	var active any
	var last any
	var created any
	err := row.Scan(&user.ID, &user.Username, &user.DisplayName, &user.Password, &user.Role, &active, &last, &user.LastIP, &created)
	if err != nil {
		return user, err
	}
	user.Active, err = asBool(active)
	if err != nil {
		return user, err
	}
	if parsed, ok, err := asTime(last); err != nil {
		return user, err
	} else if ok {
		user.LastLogin = sql.NullTime{Time: parsed, Valid: true}
	}
	parsed, ok, err := asTime(created)
	if err != nil || !ok {
		return user, err
	}
	user.CreatedAt = parsed
	return user, nil
}

func userJSON(user userRow, perms []string) map[string]any {
	var last any
	if user.LastLogin.Valid {
		last = stamp(user.LastLogin.Time)
	}
	if perms == nil {
		perms = []string{}
	}
	return map[string]any{
		"id":          user.ID,
		"username":    user.Username,
		"displayName": user.DisplayName,
		"role":        user.Role,
		"isActive":    user.Active,
		"lastLoginAt": last,
		"lastLoginIp": user.LastIP,
		"permissions": perms,
	}
}

func (s *Server) authToken(token string) (actor, error) {
	token = strings.TrimSpace(token)
	if token == "" {
		return actor{}, fail(http.StatusUnauthorized, "需要登录")
	}
	claims, err := parseJWT(s.secret, token, s.now())
	if err != nil {
		return actor{}, fail(http.StatusUnauthorized, "登录已失效")
	}
	userID, err := strconv.ParseInt(claims.Subject, 10, 64)
	if err != nil {
		return actor{}, fail(http.StatusUnauthorized, "登录已失效")
	}
	var sessionUser int64
	var expires any
	err = s.db.QueryRow(s.q(`SELECT user_id, expires_at FROM sessions WHERE token_hash = ?`), sha256Hex(claims.ID)).Scan(&sessionUser, &expires)
	if errors.Is(err, sql.ErrNoRows) {
		return actor{}, fail(http.StatusUnauthorized, "登录已失效")
	}
	if err != nil {
		return actor{}, err
	}
	exp, ok, err := asTime(expires)
	if err != nil || !ok || !exp.After(s.now()) || sessionUser != userID {
		return actor{}, fail(http.StatusUnauthorized, "登录已失效")
	}
	user, err := s.loadUser(userID)
	if errors.Is(err, sql.ErrNoRows) || (err == nil && !user.Active) {
		return actor{}, fail(http.StatusUnauthorized, "账号已停用")
	}
	if err != nil {
		return actor{}, err
	}
	perms, err := s.permissionsFor(user.Role)
	if err != nil {
		return actor{}, err
	}
	return actor{user: user, perms: perms}, nil
}

func (s *Server) current(c *gin.Context) (actor, error) {
	header := c.GetHeader("Authorization")
	token := strings.TrimPrefix(header, "Bearer ")
	if token == "" || token == header {
		return actor{}, fail(http.StatusUnauthorized, "需要登录")
	}
	return s.authToken(token)
}

func (s *Server) need(c *gin.Context, code string) (actor, error) {
	actor, err := s.current(c)
	if err != nil {
		return actor, err
	}
	if !allows(actor.perms, code) {
		return actor, fail(http.StatusForbidden, "没有权限")
	}
	return actor, nil
}

func (s *Server) logLogin(username, result, ip string) error {
	_, err := s.db.Exec(s.q(`INSERT INTO login_logs (username, result, ip, created_at) VALUES (?, ?, ?, ?)`), username, result, ip, s.now())
	return err
}

func (s *Server) register(c *gin.Context) {
	var body struct {
		Username    string `json:"username"`
		Password    string `json:"password"`
		DisplayName string `json:"display_name"`
	}
	if err := readJSON(c, &body); err != nil {
		writeErr(c, err)
		return
	}
	username := strings.TrimSpace(body.Username)
	if len(username) < 2 || len(body.Password) < 6 {
		writeErr(c, fail(http.StatusBadRequest, "账号至少 2 位，密码至少 6 位"))
		return
	}
	if _, err := s.userByName(username); err == nil {
		writeErr(c, fail(http.StatusConflict, "账号已存在"))
		return
	} else if !errors.Is(err, sql.ErrNoRows) {
		writeErr(c, err)
		return
	}
	hash, err := hashPassword(body.Password)
	if err != nil {
		writeErr(c, err)
		return
	}
	display := strings.TrimSpace(body.DisplayName)
	if display == "" {
		display = username
	}
	id, err := s.insertUser(username, display, hash, "member")
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]any{"id": id})
}

func (s *Server) insertUser(username, display, hash, role string) (int64, error) {
	if s.postgres {
		var id int64
		err := s.db.QueryRow(s.q(`INSERT INTO users (username, display_name, password_hash, role_key, is_active, last_login_ip, created_at) VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id`),
			username, display, hash, role, true, "", s.now()).Scan(&id)
		return id, err
	}
	res, err := s.db.Exec(s.q(`INSERT INTO users (username, display_name, password_hash, role_key, is_active, last_login_ip, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)`),
		username, display, hash, role, true, "", s.now())
	if err != nil {
		return 0, err
	}
	return res.LastInsertId()
}

func (s *Server) login(c *gin.Context) {
	var body struct {
		Username string `json:"username"`
		Password string `json:"password"`
	}
	if err := readJSON(c, &body); err != nil {
		writeErr(c, err)
		return
	}
	ip := clientIP(c.Request)
	username := strings.TrimSpace(body.Username)
	user, err := s.userByName(username)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		writeErr(c, err)
		return
	}
	if errors.Is(err, sql.ErrNoRows) || !user.Active || !verifyPassword(body.Password, user.Password) {
		_ = s.logLogin(username, "fail", ip)
		writeErr(c, fail(http.StatusUnauthorized, "账号或密码不正确"))
		return
	}
	jti, err := newID()
	if err != nil {
		writeErr(c, err)
		return
	}
	token, exp, err := issueJWT(s.secret, user.ID, jti, s.now())
	if err != nil {
		writeErr(c, err)
		return
	}
	tx, err := s.db.Begin()
	if err != nil {
		writeErr(c, err)
		return
	}
	defer tx.Rollback()
	now := s.now()
	if _, err := tx.Exec(s.q(`INSERT INTO sessions (token_hash, user_id, expires_at, created_at) VALUES (?, ?, ?, ?)`), sha256Hex(jti), user.ID, exp, now); err != nil {
		writeErr(c, err)
		return
	}
	if _, err := tx.Exec(s.q(`UPDATE users SET last_login_at = ?, last_login_ip = ? WHERE id = ?`), now, ip, user.ID); err != nil {
		writeErr(c, err)
		return
	}
	if _, err := tx.Exec(s.q(`INSERT INTO login_logs (username, result, ip, created_at) VALUES (?, ?, ?, ?)`), user.Username, "ok", ip, now); err != nil {
		writeErr(c, err)
		return
	}
	if err := tx.Commit(); err != nil {
		writeErr(c, err)
		return
	}
	user.LastLogin = sql.NullTime{Time: now, Valid: true}
	user.LastIP = ip
	perms, err := s.permissionsFor(user.Role)
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]any{"token": token, "user": userJSON(user, perms)})
}

func (s *Server) logout(c *gin.Context) {
	header := c.GetHeader("Authorization")
	token := strings.TrimPrefix(header, "Bearer ")
	if token != "" && token != header {
		if claims, err := parseJWT(s.secret, token, s.now()); err == nil {
			if userID, err := strconv.ParseInt(claims.Subject, 10, 64); err == nil {
				if user, err := s.loadUser(userID); err == nil {
					if res, err := s.db.Exec(s.q(`DELETE FROM sessions WHERE token_hash = ?`), sha256Hex(claims.ID)); err == nil && res != nil {
						if n, _ := res.RowsAffected(); n > 0 {
							_ = s.logLogin(user.Username, "logout", clientIP(c.Request))
						}
					}
				}
			}
		}
	}
	writeJSON(c, http.StatusOK, map[string]bool{"ok": true})
}

func (s *Server) me(c *gin.Context) {
	actor, err := s.current(c)
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, userJSON(actor.user, actor.perms))
}

func (s *Server) permissions(c *gin.Context) {
	if _, err := s.need(c, "role:read"); err != nil {
		writeErr(c, err)
		return
	}
	items := make([]map[string]string, 0, len(permissions))
	for _, item := range permissions {
		items = append(items, map[string]string{"code": item[0], "name": item[1]})
	}
	writeJSON(c, http.StatusOK, map[string]any{"items": items})
}

func (s *Server) roles(c *gin.Context) {
	if _, err := s.need(c, "role:read"); err != nil {
		writeErr(c, err)
		return
	}
	rows, err := s.db.Query(`SELECT id, key, name, is_system FROM roles ORDER BY id`)
	if err != nil {
		writeErr(c, err)
		return
	}
	defer rows.Close()
	items := []map[string]any{}
	for rows.Next() {
		var id int64
		var key, name string
		var system any
		if err := rows.Scan(&id, &key, &name, &system); err != nil {
			writeErr(c, err)
			return
		}
		flag, err := asBool(system)
		if err != nil {
			writeErr(c, err)
			return
		}
		codes, err := s.permissionsFor(key)
		if err != nil {
			writeErr(c, err)
			return
		}
		items = append(items, map[string]any{"key": key, "name": name, "isSystem": flag, "permissions": codes})
	}
	writeJSON(c, http.StatusOK, map[string]any{"items": items})
}

func (s *Server) updateRole(c *gin.Context) {
	if _, err := s.need(c, "role:write"); err != nil {
		writeErr(c, err)
		return
	}
	var body struct {
		Name        string   `json:"name"`
		Permissions []string `json:"permissions"`
	}
	if err := readJSON(c, &body); err != nil {
		writeErr(c, err)
		return
	}
	seen := make(map[string]struct{}, len(body.Permissions))
	uniquePerms := make([]string, 0, len(body.Permissions))
	for _, code := range body.Permissions {
		if !knownPermission(code) {
			writeErr(c, fail(http.StatusBadRequest, "包含未知权限"))
			return
		}
		if _, dup := seen[code]; !dup {
			seen[code] = struct{}{}
			uniquePerms = append(uniquePerms, code)
		}
	}
	key := c.Param("key")
	var id int64
	var name string
	err := s.db.QueryRow(s.q(`SELECT id, name FROM roles WHERE key = ?`), key).Scan(&id, &name)
	if errors.Is(err, sql.ErrNoRows) {
		writeErr(c, fail(http.StatusNotFound, "角色不存在"))
		return
	}
	if err != nil {
		writeErr(c, err)
		return
	}
	if strings.TrimSpace(body.Name) != "" {
		name = strings.TrimSpace(body.Name)
	}
	tx, err := s.db.Begin()
	if err != nil {
		writeErr(c, err)
		return
	}
	defer tx.Rollback()
	if _, err := tx.Exec(s.q(`UPDATE roles SET name = ? WHERE id = ?`), name, id); err != nil {
		writeErr(c, err)
		return
	}
	if _, err := tx.Exec(s.q(`DELETE FROM role_permissions WHERE role_id = ?`), id); err != nil {
		writeErr(c, err)
		return
	}
	for _, code := range uniquePerms {
		if _, err := tx.Exec(s.q(`INSERT INTO role_permissions (role_id, permission) VALUES (?, ?)`), id, code); err != nil {
			writeErr(c, err)
			return
		}
	}
	if err := tx.Commit(); err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]bool{"ok": true})
}

func (s *Server) users(c *gin.Context) {
	if _, err := s.need(c, "user:read"); err != nil {
		writeErr(c, err)
		return
	}
	q := strings.TrimSpace(c.Query("q"))
	page := queryInt(c, "page", 1)
	size := queryInt(c, "page_size", pageSize)
	query := `SELECT id, username, display_name, password_hash, role_key, is_active, last_login_at, last_login_ip, created_at FROM users`
	args := []any{}
	if q != "" {
		query += ` WHERE username LIKE ? OR display_name LIKE ?`
		like := "%" + q + "%"
		args = append(args, like, like)
	}
	query += ` ORDER BY id`
	rows, err := s.db.Query(s.q(query), args...)
	if err != nil {
		writeErr(c, err)
		return
	}
	defer rows.Close()
	all := []userRow{}
	for rows.Next() {
		user, err := scanUser(rows)
		if err != nil {
			writeErr(c, err)
			return
		}
		all = append(all, user)
	}
	start := (page - 1) * size
	if start > len(all) {
		start = len(all)
	}
	end := start + size
	if end > len(all) {
		end = len(all)
	}
	items := []map[string]any{}
	for _, user := range all[start:end] {
		perms, err := s.permissionsFor(user.Role)
		if err != nil {
			writeErr(c, err)
			return
		}
		items = append(items, userJSON(user, perms))
	}
	writeJSON(c, http.StatusOK, map[string]any{"total": len(all), "items": items})
}

func (s *Server) createUser(c *gin.Context) {
	if _, err := s.need(c, "user:write"); err != nil {
		writeErr(c, err)
		return
	}
	var body struct {
		Username    string `json:"username"`
		Password    string `json:"password"`
		DisplayName string `json:"display_name"`
		RoleKey     string `json:"role_key"`
	}
	if err := readJSON(c, &body); err != nil {
		writeErr(c, err)
		return
	}
	if body.RoleKey == "" {
		body.RoleKey = "member"
	}
	if !knownRole(body.RoleKey) {
		writeErr(c, fail(http.StatusBadRequest, "角色不存在"))
		return
	}
	username := strings.TrimSpace(body.Username)
	if len(username) < 2 || len(body.Password) < 6 {
		writeErr(c, fail(http.StatusBadRequest, "账号至少 2 位，密码至少 6 位"))
		return
	}
	if _, err := s.userByName(username); err == nil {
		writeErr(c, fail(http.StatusConflict, "账号已存在"))
		return
	} else if !errors.Is(err, sql.ErrNoRows) {
		writeErr(c, err)
		return
	}
	hash, err := hashPassword(body.Password)
	if err != nil {
		writeErr(c, err)
		return
	}
	display := strings.TrimSpace(body.DisplayName)
	if display == "" {
		display = username
	}
	id, err := s.insertUser(username, display, hash, body.RoleKey)
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]any{"id": id})
}

func (s *Server) patchUser(c *gin.Context) {
	if _, err := s.need(c, "user:write"); err != nil {
		writeErr(c, err)
		return
	}
	id, err := strconv.ParseInt(c.Param("id"), 10, 64)
	if err != nil {
		writeErr(c, fail(http.StatusNotFound, "用户不存在"))
		return
	}
	user, err := s.loadUser(id)
	if errors.Is(err, sql.ErrNoRows) {
		writeErr(c, fail(http.StatusNotFound, "用户不存在"))
		return
	}
	if err != nil {
		writeErr(c, err)
		return
	}
	var body struct {
		DisplayName *string `json:"display_name"`
		RoleKey     *string `json:"role_key"`
		IsActive    *bool   `json:"is_active"`
		Password    *string `json:"password"`
	}
	if err := readJSON(c, &body); err != nil {
		writeErr(c, err)
		return
	}
	if body.DisplayName != nil {
		user.DisplayName = strings.TrimSpace(*body.DisplayName)
	}
	if body.RoleKey != nil {
		if !knownRole(*body.RoleKey) {
			writeErr(c, fail(http.StatusBadRequest, "角色不存在"))
			return
		}
		user.Role = *body.RoleKey
	}
	if body.IsActive != nil {
		user.Active = *body.IsActive
	}
	if body.Password != nil && *body.Password != "" {
		hash, err := hashPassword(*body.Password)
		if err != nil {
			writeErr(c, err)
			return
		}
		user.Password = hash
	}
	_, err = s.db.Exec(s.q(`UPDATE users SET display_name = ?, role_key = ?, is_active = ?, password_hash = ? WHERE id = ?`),
		user.DisplayName, user.Role, user.Active, user.Password, user.ID)
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]bool{"ok": true})
}

type roomRow struct {
	ID      int64
	Name    string
	OwnerID int64
	Created time.Time
	Closed  sql.NullTime
}

func (s *Server) openRooms(ownerID int64, manage bool) ([]roomRow, error) {
	query := `SELECT id, name, owner_id, created_at, closed_at FROM rooms WHERE closed_at IS NULL`
	args := []any{}
	if !manage {
		query += ` AND owner_id = ?`
		args = append(args, ownerID)
	}
	query += ` ORDER BY id DESC`
	rows, err := s.db.Query(s.q(query), args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []roomRow
	for rows.Next() {
		room, err := scanRoom(rows)
		if err != nil {
			return nil, err
		}
		out = append(out, room)
	}
	return out, rows.Err()
}

func scanRoom(row scanner) (roomRow, error) {
	var room roomRow
	var created any
	var closed any
	if err := row.Scan(&room.ID, &room.Name, &room.OwnerID, &created, &closed); err != nil {
		return room, err
	}
	parsed, ok, err := asTime(created)
	if err != nil || !ok {
		return room, err
	}
	room.Created = parsed
	if parsed, ok, err := asTime(closed); err != nil {
		return room, err
	} else if ok {
		room.Closed = sql.NullTime{Time: parsed, Valid: true}
	}
	return room, nil
}

func (s *Server) buildRooms(ctx context.Context, act actor, q string) ([]map[string]any, error) {
	rows, err := s.openRooms(act.user.ID, allows(act.perms, "room:manage"))
	if err != nil {
		return nil, err
	}
	q = strings.TrimSpace(q)
	items := []map[string]any{}
	for _, room := range rows {
		if q != "" && !strings.Contains(room.Name, q) {
			continue
		}
		owner, _ := s.loadUser(room.OwnerID)
		text, _ := s.store.Get(ctx, liveKey(room.Name))
		live := parseLive(text)
		mode := "idle"
		if len(live) >= 2 {
			allDirect := true
			for _, m := range live {
				if !m.Direct || m.TCPRelay {
					allDirect = false
					break
				}
			}
			if allDirect {
				mode = "direct"
			} else {
				mode = "relay"
			}
		} else if len(live) == 1 {
			if live[0].TCPRelay {
				mode = "relay"
			} else {
				mode = "standby"
			}
		}
		items = append(items, map[string]any{
			"name":        room.Name,
			"owner":       owner.Username,
			"ownerId":     room.OwnerID,
			"createdAt":   stamp(room.Created),
			"mine":        room.OwnerID == act.user.ID,
			"onlineCount": len(live),
			"mode":        mode,
		})
	}
	return items, nil
}

func (s *Server) rooms(c *gin.Context) {
	act, err := s.need(c, "room:read")
	if err != nil {
		writeErr(c, err)
		return
	}
	items, err := s.buildRooms(c.Request.Context(), act, c.Query("q"))
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]any{"items": items})
}

func (s *Server) doCreateRoom(ctx context.Context, act actor, rawName string) (string, error) {
	if !allows(act.perms, "room:write") {
		return "", fail(http.StatusForbidden, "没有权限")
	}
	name := strings.TrimSpace(rawName)
	if !validRoom(name) {
		return "", fail(http.StatusBadRequest, "房间名不能包含冒号或控制字符")
	}
	var existingID int64
	var closed any
	err := s.db.QueryRow(s.q(`SELECT id, closed_at FROM rooms WHERE name = ?`), name).Scan(&existingID, &closed)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return "", err
	}
	if err == nil {
		if _, ok, timeErr := asTime(closed); timeErr != nil {
			return "", timeErr
		} else if ok {
			if _, err := s.db.Exec(s.q(`UPDATE rooms SET owner_id = ?, closed_at = NULL, created_at = ? WHERE id = ?`), act.user.ID, s.now(), existingID); err != nil {
				return "", err
			}
		} else {
			return "", fail(http.StatusConflict, "房间已存在")
		}
	} else if _, err := s.db.Exec(s.q(`INSERT INTO rooms (name, owner_id, created_at, closed_at) VALUES (?, ?, ?, NULL)`), name, act.user.ID, s.now()); err != nil {
		return "", err
	}
	if err := s.store.Set(ctx, roomKey(name), strconv.FormatInt(act.user.ID, 10), 0); err != nil {
		return "", err
	}
	return name, nil
}

func (s *Server) createRoom(c *gin.Context) {
	act, err := s.need(c, "room:write")
	if err != nil {
		writeErr(c, err)
		return
	}
	var body struct {
		Name string `json:"name"`
	}
	if err := readJSON(c, &body); err != nil {
		writeErr(c, err)
		return
	}
	name, err := s.doCreateRoom(c.Request.Context(), act, body.Name)
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]string{"name": name})
}

func (s *Server) openRoom(name string) (roomRow, error) {
	row := s.db.QueryRow(s.q(`SELECT id, name, owner_id, created_at, closed_at FROM rooms WHERE name = ? AND closed_at IS NULL`), name)
	room, err := scanRoom(row)
	if errors.Is(err, sql.ErrNoRows) {
		return room, fail(http.StatusNotFound, "房间不存在")
	}
	return room, err
}

func (s *Server) doCloseRoom(ctx context.Context, act actor, name string) error {
	if !allows(act.perms, "room:read") {
		return fail(http.StatusForbidden, "没有权限")
	}
	room, err := s.openRoom(name)
	if err != nil {
		return err
	}
	if room.OwnerID != act.user.ID && !allows(act.perms, "room:manage") {
		return fail(http.StatusForbidden, "只能关闭自己的房间")
	}
	if _, err := s.db.Exec(s.q(`UPDATE rooms SET closed_at = ? WHERE id = ?`), s.now(), room.ID); err != nil {
		return err
	}
	return s.store.Del(ctx, roomKey(room.Name), liveKey(room.Name))
}

func (s *Server) closeRoom(c *gin.Context) {
	act, err := s.need(c, "room:read")
	if err != nil {
		writeErr(c, err)
		return
	}
	if err := s.doCloseRoom(c.Request.Context(), act, c.Param("name")); err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]bool{"ok": true})
}

func (s *Server) doIssueTicket(act actor, name string) (string, time.Time, error) {
	if !allows(act.perms, "room:write") {
		return "", time.Time{}, fail(http.StatusForbidden, "没有权限")
	}
	room, err := s.openRoom(name)
	if err != nil {
		return "", time.Time{}, err
	}
	if room.OwnerID != act.user.ID && !allows(act.perms, "room:manage") {
		return "", time.Time{}, fail(http.StatusForbidden, "只能加入自己的房间")
	}
	token, exp := issueTicket(s.secret, room.Name, room.OwnerID, s.now())
	return token, exp, nil
}

func (s *Server) ticket(c *gin.Context) {
	act, err := s.need(c, "room:write")
	if err != nil {
		writeErr(c, err)
		return
	}
	token, exp, err := s.doIssueTicket(act, c.Param("name"))
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]string{"token": token, "expiresAt": exp.UTC().Format(time.RFC3339)})
}

type liveMember struct {
	MAC        string
	VirtualIP  string
	Source     string
	IP         string
	Direct     bool
	TCPRelay   bool
	RelayCount uint64
}

func parseLive(text string) []liveMember {
	var members []liveMember
	for _, line := range strings.Split(text, "\n") {
		parts := strings.Fields(line)
		if len(parts) < 3 {
			continue
		}
		ip := parts[2]
		if idx := strings.LastIndex(ip, ":"); idx > 0 {
			ip = ip[:idx]
		}
		item := liveMember{MAC: parts[0], VirtualIP: parts[1], Source: parts[2], IP: ip}
		if len(parts) >= 4 {
			item.Direct = parts[3] == "1"
		}
		if len(parts) >= 5 {
			item.TCPRelay = parts[4] == "1"
		}
		if len(parts) >= 6 {
			if count, err := strconv.ParseUint(parts[5], 10, 64); err == nil {
				item.RelayCount = count
			}
		}
		members = append(members, item)
	}
	return members
}

type serverNode struct {
	Name    string   `json:"name"`
	IP      string   `json:"ip"`
	Country string   `json:"country"`
	City    string   `json:"city"`
	Lat     *float64 `json:"lat"`
	Lon     *float64 `json:"lon"`
}

func (s *Server) serverInfo() *serverNode {
	if s.serverIP == "" && !s.hasServerCoord {
		return nil
	}
	info := &serverNode{
		Name:    s.serverName,
		IP:      s.serverIP,
		Country: s.serverCountry,
		City:    s.serverCity,
	}
	if s.hasServerCoord {
		lat, lon := s.serverLat, s.serverLon
		info.Lat = &lat
		info.Lon = &lon
		return info
	}
	if place := s.geo.lookup(s.serverIP); place != nil {
		if info.Country == "" {
			info.Country = place.Country
		}
		if info.City == "" {
			info.City = place.City
		}
		lat, lon := place.Lat, place.Lon
		info.Lat = &lat
		info.Lon = &lon
	}
	return info
}

func (s *Server) buildRoomMembers(ctx context.Context, act actor, name string) ([]map[string]any, error) {
	if !allows(act.perms, "room:read") {
		return nil, fail(http.StatusForbidden, "没有权限")
	}
	room, err := s.openRoom(name)
	if err != nil {
		return nil, err
	}
	if room.OwnerID != act.user.ID && !allows(act.perms, "room:manage") {
		return nil, fail(http.StatusForbidden, "不能查看这个房间")
	}
	text, _ := s.store.Get(ctx, liveKey(room.Name))
	items := []map[string]any{}
	for _, member := range parseLive(text) {
		items = append(items, map[string]any{
			"mac":        member.MAC,
			"virtualIp":  member.VirtualIP,
			"source":     member.Source,
			"ip":         member.IP,
			"direct":     member.Direct,
			"tcpRelay":   member.TCPRelay,
			"relayCount": member.RelayCount,
		})
	}
	return items, nil
}

func (s *Server) members(c *gin.Context) {
	act, err := s.need(c, "room:read")
	if err != nil {
		writeErr(c, err)
		return
	}
	items, err := s.buildRoomMembers(c.Request.Context(), act, c.Param("name"))
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]any{"items": items})
}

func (s *Server) doKickMember(ctx context.Context, act actor, name, rawMAC string) error {
	if !allows(act.perms, "member:kick") {
		return fail(http.StatusForbidden, "没有权限")
	}
	if _, err := s.openRoom(name); err != nil {
		return err
	}
	mac := strings.ToLower(strings.TrimSpace(rawMAC))
	if mac == "" {
		return fail(http.StatusBadRequest, "缺少成员地址")
	}
	return s.store.Set(ctx, kickKey(name, mac), "1", 120*time.Second)
}

func (s *Server) kick(c *gin.Context) {
	act, err := s.need(c, "member:kick")
	if err != nil {
		writeErr(c, err)
		return
	}
	var body struct {
		MAC string `json:"mac"`
	}
	if err := readJSON(c, &body); err != nil {
		writeErr(c, err)
		return
	}
	if err := s.doKickMember(c.Request.Context(), act, c.Param("name"), body.MAC); err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, map[string]bool{"ok": true})
}

func (s *Server) remember(room, mac, ip string, place *Place) error {
	cutoff := s.now().Add(-24 * time.Hour)
	if _, err := s.db.Exec(s.q(`DELETE FROM presence_points WHERE seen_at < ?`), cutoff); err != nil {
		return err
	}
	var city string
	var seen any
	err := s.db.QueryRow(s.q(`SELECT city, seen_at FROM presence_points WHERE room_name = ? AND member_mac = ? ORDER BY seen_at DESC LIMIT 1`), room, mac).Scan(&city, &seen)
	now := s.now()
	if err == nil {
		last, ok, timeErr := asTime(seen)
		if timeErr != nil {
			return timeErr
		}
		if ok && city == place.City && now.Sub(last) < 5*time.Minute {
			return nil
		}
	} else if !errors.Is(err, sql.ErrNoRows) {
		return err
	}
	_, err = s.db.Exec(s.q(`INSERT INTO presence_points (room_name, member_mac, source_ip, country, city, latitude, longitude, seen_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)`),
		room, mac, ip, place.Country, place.City, place.Lat, place.Lon, now)
	return err
}

func (s *Server) buildWorldMap(ctx context.Context, act actor, filter string) (map[string]any, error) {
	if !allows(act.perms, "map:read") {
		return nil, fail(http.StatusForbidden, "没有权限")
	}
	rows, err := s.openRooms(act.user.ID, allows(act.perms, "room:manage"))
	if err != nil {
		return nil, err
	}
	if filter != "" {
		kept := rows[:0]
		for _, room := range rows {
			if room.Name == filter {
				kept = append(kept, room)
			}
		}
		rows = kept
	}
	server := s.serverInfo()
	type locatedNode struct {
		id         string
		room       string
		virtualIP  string
		mac        string
		lat        float64
		lon        float64
		direct     bool
		tcpRelay   bool
		relayCount uint64
	}
	type linkEndpoint struct {
		Lat   float64 `json:"lat"`
		Lon   float64 `json:"lon"`
		Label string  `json:"label"`
	}
	type mapLink struct {
		ID         string       `json:"id"`
		Kind       string       `json:"kind"`
		Room       string       `json:"room"`
		FromID     string       `json:"fromId"`
		ToID       string       `json:"toId"`
		From       linkEndpoint `json:"from"`
		To         linkEndpoint `json:"to"`
		RelayCount uint64       `json:"relayCount"`
	}
	members := []map[string]any{}
	links := []mapLink{}
	for _, room := range rows {
		owner, _ := s.loadUser(room.OwnerID)
		text, _ := s.store.Get(ctx, liveKey(room.Name))
		roomLive := parseLive(text)
		roomNodes := make([]locatedNode, 0, len(roomLive))
		for _, member := range roomLive {
			place := s.geo.lookup(member.IP)
			mode := "standby"
			if len(roomLive) >= 2 {
				if member.Direct && !member.TCPRelay {
					mode = "direct"
				} else {
					mode = "relay"
				}
			} else if member.TCPRelay {
				mode = "relay"
			}
			nodeID := room.Name + ":" + member.MAC
			item := map[string]any{
				"id":         nodeID,
				"room":       room.Name,
				"owner":      owner.Username,
				"mac":        member.MAC,
				"virtualIp":  member.VirtualIP,
				"source":     member.Source,
				"direct":     member.Direct,
				"tcpRelay":   member.TCPRelay,
				"relayCount": member.RelayCount,
				"mode":       mode,
				"country":    "",
				"city":       "",
				"lat":        nil,
				"lon":        nil,
			}
			if place != nil {
				item["country"] = place.Country
				item["city"] = place.City
				item["lat"] = place.Lat
				item["lon"] = place.Lon
				if err := s.remember(room.Name, member.MAC, member.IP, place); err != nil {
					return nil, err
				}
				roomNodes = append(roomNodes, locatedNode{
					id:         nodeID,
					room:       room.Name,
					virtualIP:  member.VirtualIP,
					mac:        member.MAC,
					lat:        place.Lat,
					lon:        place.Lon,
					direct:     member.Direct,
					tcpRelay:   member.TCPRelay,
					relayCount: member.RelayCount,
				})
			}
			members = append(members, item)
		}
		// Build 3D communication topology links for this room
		for i := 0; i < len(roomNodes); i++ {
			a := roomNodes[i]
			if server != nil && server.Lat != nil && server.Lon != nil {
				kind := "control"
				if len(roomLive) >= 2 && (!a.direct || a.tcpRelay) {
					kind = "relay"
				}
				links = append(links, mapLink{
					ID:         kind + ":" + a.id + ":server",
					Kind:       kind,
					Room:       room.Name,
					FromID:     a.id,
					ToID:       "server",
					From:       linkEndpoint{Lat: a.lat, Lon: a.lon, Label: a.virtualIP},
					To:         linkEndpoint{Lat: *server.Lat, Lon: *server.Lon, Label: server.Name},
					RelayCount: a.relayCount,
				})
			}
			for j := i + 1; j < len(roomNodes); j++ {
				b := roomNodes[j]
				if a.direct && b.direct && !a.tcpRelay && !b.tcpRelay {
					links = append(links, mapLink{
						ID:     "direct:" + a.id + ":" + b.id,
						Kind:   "direct",
						Room:   room.Name,
						FromID: a.id,
						ToID:   b.id,
						From:   linkEndpoint{Lat: a.lat, Lon: a.lon, Label: a.virtualIP},
						To:     linkEndpoint{Lat: b.lat, Lon: b.lon, Label: b.virtualIP},
					})
				}
			}
		}
	}
	type cityBucket struct {
		Country string  `json:"country"`
		City    string  `json:"city"`
		Lat     float64 `json:"lat"`
		Lon     float64 `json:"lon"`
		Count   int     `json:"count"`
	}
	cities := []cityBucket{}
	cityIndex := map[string]int{}
	countryOrder := []string{}
	countryCount := map[string]int{}
	for _, member := range members {
		if member["lat"] == nil {
			continue
		}
		country := member["country"].(string)
		if _, ok := countryCount[country]; !ok {
			countryOrder = append(countryOrder, country)
		}
		countryCount[country]++
		key := country + "|" + member["city"].(string)
		if idx, ok := cityIndex[key]; ok {
			cities[idx].Count++
			continue
		}
		cityIndex[key] = len(cities)
		cities = append(cities, cityBucket{
			Country: country,
			City:    member["city"].(string),
			Lat:     member["lat"].(float64),
			Lon:     member["lon"].(float64),
			Count:   1,
		})
	}
	countries := []map[string]any{}
	for _, code := range countryOrder {
		countries = append(countries, map[string]any{"country": code, "count": countryCount[code]})
	}
	logins := []map[string]any{}
	if allows(act.perms, "user:read") {
		since := s.now().Add(-30 * 24 * time.Hour)
		logRows, err := s.db.Query(s.q(`SELECT username, result, ip, created_at FROM login_logs WHERE created_at >= ? ORDER BY id DESC LIMIT 200`), since)
		if err != nil {
			return nil, err
		}
		defer logRows.Close()
		for logRows.Next() {
			var username, result, ip string
			var created any
			if err := logRows.Scan(&username, &result, &ip, &created); err != nil {
				return nil, err
			}
			when, _, _ := asTime(created)
			place := s.geo.lookup(ip)
			item := map[string]any{
				"username": username, "result": result, "ip": ip, "createdAt": stamp(when),
				"country": "", "city": "", "lat": nil, "lon": nil,
			}
			if place != nil {
				item["country"] = place.Country
				item["city"] = place.City
				item["lat"] = place.Lat
				item["lon"] = place.Lon
			}
			logins = append(logins, item)
		}
	}
	names := []string{}
	all, err := s.openRooms(act.user.ID, allows(act.perms, "room:manage"))
	if err != nil {
		return nil, err
	}
	for _, room := range all {
		names = append(names, room.Name)
	}
	if cities == nil {
		cities = []cityBucket{}
	}
	if countries == nil {
		countries = []map[string]any{}
	}
	if logins == nil {
		logins = []map[string]any{}
	}
	return map[string]any{
		"server": server, "members": members, "links": links, "cities": cities, "countries": countries, "logins": logins, "rooms": names,
	}, nil
}

func (s *Server) worldMap(c *gin.Context) {
	act, err := s.need(c, "map:read")
	if err != nil {
		writeErr(c, err)
		return
	}
	payload, err := s.buildWorldMap(c.Request.Context(), act, c.Query("room"))
	if err != nil {
		writeErr(c, err)
		return
	}
	writeJSON(c, http.StatusOK, payload)
}

func (s *Server) loginLogs(c *gin.Context) {
	if _, err := s.need(c, "user:read"); err != nil {
		writeErr(c, err)
		return
	}
	q := strings.TrimSpace(c.Query("q"))
	page := queryInt(c, "page", 1)
	size := queryInt(c, "page_size", pageSize)
	query := `SELECT username, result, ip, created_at FROM login_logs`
	args := []any{}
	if q != "" {
		query += ` WHERE username LIKE ?`
		args = append(args, "%"+q+"%")
	}
	query += ` ORDER BY id DESC`
	rows, err := s.db.Query(s.q(query), args...)
	if err != nil {
		writeErr(c, err)
		return
	}
	defer rows.Close()
	type item struct {
		Username  string
		Result    string
		IP        string
		CreatedAt time.Time
	}
	var all []item
	for rows.Next() {
		var row item
		var created any
		if err := rows.Scan(&row.Username, &row.Result, &row.IP, &created); err != nil {
			writeErr(c, err)
			return
		}
		parsed, _, err := asTime(created)
		if err != nil {
			writeErr(c, err)
			return
		}
		row.CreatedAt = parsed
		all = append(all, row)
	}
	start := (page - 1) * size
	if start > len(all) {
		start = len(all)
	}
	end := start + size
	if end > len(all) {
		end = len(all)
	}
	items := []map[string]any{}
	for _, row := range all[start:end] {
		place := s.geo.lookup(row.IP)
		country, city := "", ""
		if place != nil {
			country = place.Country
			city = place.City
		}
		items = append(items, map[string]any{
			"username": row.Username, "result": row.Result, "ip": row.IP,
			"country": country, "city": city, "createdAt": stamp(row.CreatedAt),
		})
	}
	writeJSON(c, http.StatusOK, map[string]any{"total": len(all), "items": items})
}

func (s *Server) panelFile(c *gin.Context) {
	if strings.HasPrefix(c.Request.URL.Path, "/api/") || (c.Request.Method != http.MethodGet && c.Request.Method != http.MethodHead) {
		c.JSON(http.StatusNotFound, gin.H{"detail": "Not Found"})
		return
	}
	if s.panel == "" {
		c.Status(http.StatusNotFound)
		return
	}
	rel := strings.TrimPrefix(c.Request.URL.Path, "/")
	if rel != "" {
		target := filepath.Join(s.panel, filepath.Clean("/"+rel))
		if relPath, err := filepath.Rel(s.panel, target); err == nil && !strings.HasPrefix(relPath, "..") {
			if info, err := os.Stat(target); err == nil && !info.IsDir() {
				if strings.HasPrefix(rel, "assets/") {
					c.Header("Cache-Control", "public, max-age=31536000, immutable")
				}
				c.File(target)
				return
			}
		}
	}
	index := filepath.Join(s.panel, "index.html")
	if _, err := os.Stat(index); err != nil {
		c.Status(http.StatusNotFound)
		return
	}
	c.Header("Cache-Control", "no-cache")
	c.File(index)
}
