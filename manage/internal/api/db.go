package api

import (
	"database/sql"
	"fmt"
	"strconv"
	"strings"
	"time"

	_ "github.com/jackc/pgx/v5/stdlib"
	_ "modernc.org/sqlite"
)

func openDB(raw string) (*sql.DB, bool, error) {
	if raw == "" {
		raw = "sqlite://./lanroom-manage.db"
	}
	if strings.HasPrefix(raw, "postgres") {
		dsn := strings.Replace(raw, "postgresql+psycopg://", "postgres://", 1)
		dsn = strings.Replace(dsn, "postgresql://", "postgres://", 1)
		db, err := sql.Open("pgx", dsn)
		return db, true, err
	}
	path := strings.TrimPrefix(raw, "sqlite://")
	if path == "" {
		path = "./lanroom-manage.db"
	}
	if !strings.Contains(path, "?") {
		path += "?_time_format=sqlite"
	}
	db, err := sql.Open("sqlite", path)
	if err == nil {
		db.SetMaxOpenConns(1)
	}
	return db, false, err
}

func (s *Server) q(query string) string {
	if !s.postgres {
		return query
	}
	var b strings.Builder
	n := 1
	for _, r := range query {
		if r == '?' {
			b.WriteByte('$')
			b.WriteString(strconv.Itoa(n))
			n++
		} else {
			b.WriteRune(r)
		}
	}
	return b.String()
}

func (s *Server) migrate() error {
	stmts := sqliteSchema
	if s.postgres {
		stmts = postgresSchema
	}
	for _, stmt := range stmts {
		if _, err := s.db.Exec(stmt); err != nil {
			return fmt.Errorf("migrate: %w", err)
		}
	}
	return nil
}

var sqliteSchema = []string{
	`CREATE TABLE IF NOT EXISTS roles (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		key TEXT NOT NULL UNIQUE,
		name TEXT NOT NULL,
		is_system INTEGER NOT NULL DEFAULT 1
	)`,
	`CREATE TABLE IF NOT EXISTS role_permissions (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		role_id INTEGER NOT NULL,
		permission TEXT NOT NULL,
		UNIQUE (role_id, permission)
	)`,
	`CREATE TABLE IF NOT EXISTS users (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		username TEXT NOT NULL UNIQUE,
		display_name TEXT NOT NULL DEFAULT '',
		password_hash TEXT NOT NULL,
		role_key TEXT NOT NULL,
		is_active INTEGER NOT NULL DEFAULT 1,
		last_login_at TEXT,
		last_login_ip TEXT NOT NULL DEFAULT '',
		created_at TEXT NOT NULL
	)`,
	`CREATE TABLE IF NOT EXISTS rooms (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		name TEXT NOT NULL UNIQUE,
		owner_id INTEGER NOT NULL,
		created_at TEXT NOT NULL,
		closed_at TEXT
	)`,
	`CREATE TABLE IF NOT EXISTS sessions (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		token_hash TEXT NOT NULL UNIQUE,
		user_id INTEGER NOT NULL,
		expires_at TEXT NOT NULL,
		created_at TEXT NOT NULL
	)`,
	`CREATE TABLE IF NOT EXISTS login_logs (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		username TEXT NOT NULL,
		result TEXT NOT NULL,
		ip TEXT NOT NULL DEFAULT '',
		created_at TEXT NOT NULL
	)`,
	`CREATE TABLE IF NOT EXISTS presence_points (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		room_name TEXT NOT NULL,
		member_mac TEXT NOT NULL,
		source_ip TEXT NOT NULL,
		country TEXT NOT NULL DEFAULT '',
		city TEXT NOT NULL DEFAULT '',
		latitude REAL NOT NULL,
		longitude REAL NOT NULL,
		seen_at TEXT NOT NULL
	)`,
}

var postgresSchema = []string{
	`CREATE TABLE IF NOT EXISTS roles (
		id SERIAL PRIMARY KEY,
		key VARCHAR(64) NOT NULL UNIQUE,
		name VARCHAR(128) NOT NULL,
		is_system BOOLEAN NOT NULL DEFAULT TRUE
	)`,
	`CREATE TABLE IF NOT EXISTS role_permissions (
		id SERIAL PRIMARY KEY,
		role_id INTEGER NOT NULL,
		permission VARCHAR(64) NOT NULL,
		UNIQUE (role_id, permission)
	)`,
	`CREATE TABLE IF NOT EXISTS users (
		id SERIAL PRIMARY KEY,
		username VARCHAR(64) NOT NULL UNIQUE,
		display_name VARCHAR(128) NOT NULL DEFAULT '',
		password_hash VARCHAR(255) NOT NULL,
		role_key VARCHAR(64) NOT NULL,
		is_active BOOLEAN NOT NULL DEFAULT TRUE,
		last_login_at TIMESTAMP NULL,
		last_login_ip VARCHAR(64) NOT NULL DEFAULT '',
		created_at TIMESTAMP NOT NULL
	)`,
	`CREATE TABLE IF NOT EXISTS rooms (
		id SERIAL PRIMARY KEY,
		name VARCHAR(64) NOT NULL UNIQUE,
		owner_id INTEGER NOT NULL,
		created_at TIMESTAMP NOT NULL,
		closed_at TIMESTAMP NULL
	)`,
	`CREATE TABLE IF NOT EXISTS sessions (
		id SERIAL PRIMARY KEY,
		token_hash VARCHAR(64) NOT NULL UNIQUE,
		user_id INTEGER NOT NULL,
		expires_at TIMESTAMP NOT NULL,
		created_at TIMESTAMP NOT NULL
	)`,
	`CREATE TABLE IF NOT EXISTS login_logs (
		id SERIAL PRIMARY KEY,
		username VARCHAR(128) NOT NULL,
		result VARCHAR(16) NOT NULL,
		ip VARCHAR(64) NOT NULL DEFAULT '',
		created_at TIMESTAMP NOT NULL
	)`,
	`CREATE TABLE IF NOT EXISTS presence_points (
		id SERIAL PRIMARY KEY,
		room_name VARCHAR(64) NOT NULL,
		member_mac VARCHAR(32) NOT NULL,
		source_ip VARCHAR(64) NOT NULL,
		country VARCHAR(8) NOT NULL DEFAULT '',
		city VARCHAR(128) NOT NULL DEFAULT '',
		latitude DOUBLE PRECISION NOT NULL,
		longitude DOUBLE PRECISION NOT NULL,
		seen_at TIMESTAMP NOT NULL
	)`,
}

func asBool(v any) (bool, error) {
	switch t := v.(type) {
	case bool:
		return t, nil
	case int64:
		return t != 0, nil
	case int32:
		return t != 0, nil
	case []byte:
		s := string(t)
		return s == "1" || s == "t" || s == "true", nil
	case string:
		return t == "1" || t == "t" || t == "true", nil
	default:
		return false, fmt.Errorf("bool value %T", v)
	}
}

func asTime(v any) (time.Time, bool, error) {
	if v == nil {
		return time.Time{}, false, nil
	}
	switch t := v.(type) {
	case time.Time:
		return t, true, nil
	case string:
		parsed, err := parseTime(t)
		return parsed, err == nil, err
	case []byte:
		parsed, err := parseTime(string(t))
		return parsed, err == nil, err
	default:
		return time.Time{}, false, fmt.Errorf("time value %T", v)
	}
}

func parseTime(value string) (time.Time, error) {
	if i := strings.Index(value, " m="); i >= 0 {
		value = value[:i]
	}
	for _, layout := range []string{
		time.RFC3339Nano,
		time.RFC3339,
		"2006-01-02 15:04:05.999999999-07:00",
		"2006-01-02T15:04:05.999999999-07:00",
		"2006-01-02 15:04:05.999999999 -0700 MST",
		"2006-01-02 15:04:05.999999999",
		"2006-01-02T15:04:05.999999999",
		"2006-01-02 15:04:05",
		"2006-01-02T15:04:05",
	} {
		if parsed, err := time.Parse(layout, value); err == nil {
			return parsed, nil
		}
	}
	return time.Time{}, fmt.Errorf("time %q", value)
}

func stamp(t time.Time) string {
	return t.UTC().Format("2006-01-02T15:04:05")
}
