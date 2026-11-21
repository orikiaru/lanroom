package api

import (
	"encoding/json"
	"net"
	"os"
	"sync"

	"github.com/oschwald/maxminddb-golang"
)

type Place struct {
	Country string
	City    string
	Lat     float64
	Lon     float64
}

type geoDB struct {
	mu       sync.Mutex
	fixture  map[string]Place
	reader   *maxminddb.Reader
	disabled bool
	path     string
}

func newGeo(fixturePath, dbPath string) *geoDB {
	geo := &geoDB{fixture: map[string]Place{}, path: dbPath}
	if fixturePath == "" {
		return geo
	}
	raw, err := os.ReadFile(fixturePath)
	if err != nil {
		return geo
	}
	var decoded map[string]struct {
		Country string  `json:"country"`
		City    string  `json:"city"`
		Lat     float64 `json:"lat"`
		Lon     float64 `json:"lon"`
	}
	if json.Unmarshal(raw, &decoded) != nil {
		return geo
	}
	for key, value := range decoded {
		geo.fixture[key] = Place{Country: value.Country, City: value.City, Lat: value.Lat, Lon: value.Lon}
	}
	return geo
}

func (g *geoDB) lookup(ip string) *Place {
	if place, ok := g.fixture[ip]; ok {
		copy := place
		return &copy
	}
	if g.path == "" {
		return nil
	}
	g.mu.Lock()
	defer g.mu.Unlock()
	if g.disabled {
		return nil
	}
	if g.reader == nil {
		reader, err := maxminddb.Open(g.path)
		if err != nil {
			g.disabled = true
			return nil
		}
		g.reader = reader
	}
	parsed := net.ParseIP(ip)
	if parsed == nil {
		return nil
	}
	var row struct {
		Country struct {
			ISOCode string `maxminddb:"iso_code"`
		} `maxminddb:"country"`
		City struct {
			Names map[string]string `maxminddb:"names"`
		} `maxminddb:"city"`
		Location struct {
			Latitude  float64 `maxminddb:"latitude"`
			Longitude float64 `maxminddb:"longitude"`
		} `maxminddb:"location"`
	}
	if err := g.reader.Lookup(parsed, &row); err != nil {
		return nil
	}
	if row.Location.Latitude == 0 && row.Location.Longitude == 0 {
		return nil
	}
	return &Place{
		Country: row.Country.ISOCode,
		City:    row.City.Names["en"],
		Lat:     row.Location.Latitude,
		Lon:     row.Location.Longitude,
	}
}
