package api

import (
	"database/sql"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
)

type Config struct {
	DatabaseURL    string
	RedisURL       string
	AuthSecret     string
	AdminUser      string
	AdminPassword  string
	PanelDist      string
	GeoIPDB        string
	GeoIPFixture   string
	ServerIP       string
	ServerName     string
	ServerCountry  string
	ServerCity     string
	ServerLat      float64
	ServerLon      float64
	HasServerCoord bool
}

func ConfigFromEnv() Config {
	admin := os.Getenv("ADMIN_EMAIL")
	if admin == "" {
		admin = os.Getenv("ADMIN_USERNAME")
	}
	if admin == "" {
		admin = "admin"
	}
	secret := os.Getenv("AUTH_SECRET")
	if secret == "" {
		secret = "lanroom-dev-secret"
	}
	redisURL := os.Getenv("REDIS_URL")
	if redisURL == "" {
		redisURL = "redis://127.0.0.1:6379/0"
	}
	serverName := strings.TrimSpace(os.Getenv("SERVER_NAME"))
	if serverName == "" {
		serverName = "Lanroom 主节点"
	}
	var lat, lon float64
	hasCoord := false
	if rawLat, rawLon := strings.TrimSpace(os.Getenv("SERVER_LAT")), strings.TrimSpace(os.Getenv("SERVER_LON")); rawLat != "" && rawLon != "" {
		if pLat, err1 := strconv.ParseFloat(rawLat, 64); err1 == nil {
			if pLon, err2 := strconv.ParseFloat(rawLon, 64); err2 == nil {
				lat, lon, hasCoord = pLat, pLon, true
			}
		}
	}
	return Config{
		DatabaseURL:    os.Getenv("DATABASE_URL"),
		RedisURL:       redisURL,
		AuthSecret:     secret,
		AdminUser:      admin,
		AdminPassword:  os.Getenv("ADMIN_PASSWORD"),
		PanelDist:      os.Getenv("PANEL_DIST"),
		GeoIPDB:        os.Getenv("GEOIP_DB"),
		GeoIPFixture:   os.Getenv("GEOIP_FIXTURE"),
		ServerIP:       strings.TrimSpace(os.Getenv("SERVER_IP")),
		ServerName:     serverName,
		ServerCountry:  strings.TrimSpace(os.Getenv("SERVER_COUNTRY")),
		ServerCity:     strings.TrimSpace(os.Getenv("SERVER_CITY")),
		ServerLat:      lat,
		ServerLon:      lon,
		HasServerCoord: hasCoord,
	}
}

type Server struct {
	db             *sql.DB
	postgres       bool
	store          Store
	geo            *geoDB
	tiles          *tileCache
	secret         string
	panel          string
	adminUser      string
	adminPass      string
	serverIP       string
	serverName     string
	serverCountry  string
	serverCity     string
	serverLat      float64
	serverLon      float64
	hasServerCoord bool
	now            func() time.Time
}

func New(cfg Config) (*Server, error) {
	db, postgres, err := openDB(cfg.DatabaseURL)
	if err != nil {
		return nil, err
	}
	var store Store
	if cfg.RedisURL == "memory://" {
		store = newMemoryStore()
	} else {
		store, err = newRedisStore(cfg.RedisURL)
		if err != nil {
			return nil, err
		}
	}
	serverName := strings.TrimSpace(cfg.ServerName)
	if serverName == "" {
		serverName = "Lanroom 主节点"
	}
	server := &Server{
		db:             db,
		postgres:       postgres,
		store:          store,
		geo:            newGeo(cfg.GeoIPFixture, cfg.GeoIPDB),
		tiles:          newTileCache(),
		secret:         cfg.AuthSecret,
		panel:          cfg.PanelDist,
		adminUser:      cfg.AdminUser,
		adminPass:      cfg.AdminPassword,
		serverIP:       strings.TrimSpace(cfg.ServerIP),
		serverName:     serverName,
		serverCountry:  strings.TrimSpace(cfg.ServerCountry),
		serverCity:     strings.TrimSpace(cfg.ServerCity),
		serverLat:      cfg.ServerLat,
		serverLon:      cfg.ServerLon,
		hasServerCoord: cfg.HasServerCoord,
		now:            func() time.Time { return time.Now().UTC() },
	}
	if err := server.migrate(); err != nil {
		return nil, err
	}
	if err := server.seed(); err != nil {
		return nil, err
	}
	return server, nil
}

func (s *Server) Handler() http.Handler {
	gin.SetMode(gin.ReleaseMode)
	engine := gin.New()
	engine.RedirectTrailingSlash = false
	engine.RedirectFixedPath = false
	engine.Use(gin.Recovery(), cors())
	engine.POST("/api/register", s.register)
	engine.POST("/api/login", s.login)
	engine.POST("/api/logout", s.logout)
	engine.GET("/api/me", s.me)
	engine.GET("/api/permissions", s.permissions)
	engine.GET("/api/roles", s.roles)
	engine.PUT("/api/roles/:key", s.updateRole)
	engine.GET("/api/users", s.users)
	engine.POST("/api/users", s.createUser)
	engine.PATCH("/api/users/:id", s.patchUser)
	engine.GET("/api/rooms", s.rooms)
	engine.POST("/api/rooms", s.createRoom)
	engine.POST("/api/rooms/:name/close", s.closeRoom)
	engine.POST("/api/rooms/:name/ticket", s.ticket)
	engine.GET("/api/rooms/:name/members", s.members)
	engine.POST("/api/rooms/:name/kick", s.kick)
	engine.GET("/api/map", s.worldMap)
	engine.GET("/api/ws", s.panelWS)
	engine.GET("/api/tiles/:layer/:z/:x/:y", s.proxyTile)
	engine.GET("/api/login-logs", s.loginLogs)
	engine.NoRoute(s.panelFile)
	return engine
}

func cors() gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Header("Access-Control-Allow-Origin", "*")
		c.Header("Access-Control-Allow-Headers", "*")
		c.Header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS")
		if c.Request.Method == http.MethodOptions {
			c.AbortWithStatus(http.StatusNoContent)
			return
		}
		c.Next()
	}
}
