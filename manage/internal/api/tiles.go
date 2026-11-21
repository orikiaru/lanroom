package api

import (
	"fmt"
	"io"
	"net/http"
	"strconv"
	"sync"
	"time"

	"github.com/gin-gonic/gin"
)

const (
	tileCacheMaxEntries = 1024
	tileMaxBytes        = 1024 * 1024
)

type cachedTile struct {
	data        []byte
	contentType string
	storedAt    time.Time
}

type tileCache struct {
	mu      sync.Mutex
	entries map[string]*cachedTile
	order   []string
	client  *http.Client
}

func newTileCache() *tileCache {
	return &tileCache{
		entries: make(map[string]*cachedTile, 256),
		order:   make([]string, 0, 256),
		client: &http.Client{
			Timeout: 8 * time.Second,
		},
	}
}

func (tc *tileCache) get(key string) (*cachedTile, bool) {
	tc.mu.Lock()
	defer tc.mu.Unlock()
	item, ok := tc.entries[key]
	if !ok {
		return nil, false
	}
	if time.Since(item.storedAt) > 12*time.Hour {
		delete(tc.entries, key)
		return nil, false
	}
	return item, true
}

func (tc *tileCache) put(key string, item *cachedTile) {
	tc.mu.Lock()
	defer tc.mu.Unlock()
	if _, exists := tc.entries[key]; !exists {
		tc.order = append(tc.order, key)
	}
	tc.entries[key] = item
	for len(tc.entries) > tileCacheMaxEntries && len(tc.order) > 0 {
		oldest := tc.order[0]
		tc.order = tc.order[1:]
		delete(tc.entries, oldest)
	}
}

func tileUpstreamURL(layer string, z, x, y int) (string, bool) {
	switch layer {
	case "zh":
		sub := ((x + y) & 3) + 1
		return fmt.Sprintf("https://webrd0%d.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=2&style=8&x=%d&y=%d&z=%d", sub, x, y, z), true
	case "dark":
		return fmt.Sprintf("https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/%d/%d/%d", z, y, x), true
	case "places":
		return fmt.Sprintf("https://server.arcgisonline.com/ArcGIS/rest/services/Reference/World_Boundaries_and_Places/MapServer/tile/%d/%d/%d", z, y, x), true
	case "sat":
		return fmt.Sprintf("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%d/%d/%d", z, y, x), true
	default:
		return "", false
	}
}

func (s *Server) proxyTile(c *gin.Context) {
	layer := c.Param("layer")
	z, errZ := strconv.Atoi(c.Param("z"))
	x, errX := strconv.Atoi(c.Param("x"))
	y, errY := strconv.Atoi(c.Param("y"))
	if errZ != nil || errX != nil || errY != nil || z < 0 || z > 16 {
		c.Status(http.StatusBadRequest)
		return
	}
	maxCoord := 1 << z
	if x < 0 || x >= maxCoord || y < 0 || y >= maxCoord {
		c.Status(http.StatusBadRequest)
		return
	}
	upstream, ok := tileUpstreamURL(layer, z, x, y)
	if !ok {
		c.Status(http.StatusNotFound)
		return
	}

	cacheKey := fmt.Sprintf("%s:%d/%d/%d", layer, z, x, y)
	if hit, found := s.tiles.get(cacheKey); found {
		c.Header("Cache-Control", "public, max-age=86400")
		c.Header("X-Tile-Cache", "HIT")
		c.Data(http.StatusOK, hit.contentType, hit.data)
		return
	}

	req, err := http.NewRequestWithContext(c.Request.Context(), http.MethodGet, upstream, nil)
	if err != nil {
		c.Status(http.StatusBadGateway)
		return
	}
	req.Header.Set("User-Agent", "Mozilla/5.0 (compatible; LanroomPanel/1.0)")
	resp, err := s.tiles.client.Do(req)
	if err != nil {
		c.Status(http.StatusBadGateway)
		return
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		c.Status(http.StatusBadGateway)
		return
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, tileMaxBytes))
	if err != nil || len(body) == 0 {
		c.Status(http.StatusBadGateway)
		return
	}
	ct := resp.Header.Get("Content-Type")
	if ct == "" {
		ct = "image/png"
	}
	s.tiles.put(cacheKey, &cachedTile{
		data:        body,
		contentType: ct,
		storedAt:    time.Now(),
	})
	c.Header("Cache-Control", "public, max-age=86400")
	c.Header("X-Tile-Cache", "MISS")
	c.Data(http.StatusOK, ct, body)
}
