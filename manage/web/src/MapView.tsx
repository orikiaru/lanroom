import { useEffect, useRef } from "react";
import maplibregl, { type GeoJSONSource, type Map as WorldMap, type StyleSpecification } from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import { feature } from "topojson-client";
import type { FeatureCollection, Geometry } from "geojson";
import type { GeometryCollection, Topology } from "topojson-specification";
import world from "world-atlas/countries-50m.json";
import type { MapCity, MapCountry, MapLink, MapMember, ServerNode } from "./api";
import type { SelectedEntity } from "./Globe3D";
import { alphaFromId } from "./iso";

const darkLocalStyle: StyleSpecification = {
  version: 8,
  sources: {
    "esri-dark": {
      type: "raster",
      tiles: [`${location.origin}/api/tiles/dark/{z}/{x}/{y}`],
      tileSize: 256,
      maxzoom: 16,
    },
    "esri-places": {
      type: "raster",
      tiles: [`${location.origin}/api/tiles/places/{z}/{x}/{y}`],
      tileSize: 256,
      maxzoom: 16,
    },
  },
  layers: [
    {
      id: "background",
      type: "background",
      paint: { "background-color": "#050b18" },
    },
    {
      id: "esri-dark-tiles",
      type: "raster",
      source: "esri-dark",
      minzoom: 2,
      maxzoom: 18,
      paint: {
        "raster-opacity": ["interpolate", ["linear"], ["zoom"], 2, 0.35, 4, 0.92],
        "raster-brightness-max": 0.9,
      },
    },
    {
      id: "esri-places-tiles",
      type: "raster",
      source: "esri-places",
      minzoom: 3,
      maxzoom: 18,
      paint: {
        "raster-opacity": ["interpolate", ["linear"], ["zoom"], 3, 0.5, 5, 0.92],
      },
    },
  ],
};

function greatCircleCurve(from: [number, number], to: [number, number], steps = 36): [number, number][] {
  const [lon1, lat1] = from;
  const [lon2, lat2] = to;
  const dx = lon2 - lon1;
  const dy = lat2 - lat1;
  const dist = Math.hypot(dx, dy);
  const arch = Math.min(18, Math.max(2.5, dist * 0.18));
  const nx = dist > 0.001 ? -dy / dist : 0;
  const ny = dist > 0.001 ? dx / dist : 1;
  const midLon = (lon1 + lon2) / 2 + nx * arch;
  const midLat = Math.max(-82, Math.min(82, (lat1 + lat2) / 2 + ny * arch));

  const coords: [number, number][] = [];
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const inv = 1 - t;
    const lon = inv * inv * lon1 + 2 * inv * t * midLon + t * t * lon2;
    const lat = inv * inv * lat1 + 2 * inv * t * midLat + t * t * lat2;
    coords.push([lon, lat]);
  }
  return coords;
}

export function MapView({
  server,
  countries,
  cities,
  members,
  links,
  isLoginMode,
  showControlLinks,
  onSelect,
}: {
  server: ServerNode | null;
  countries: MapCountry[];
  cities: MapCity[];
  members: MapMember[];
  links: MapLink[];
  isLoginMode: boolean;
  showControlLinks: boolean;
  onSelect: (entity: SelectedEntity | null) => void;
}) {
  const node = useRef<HTMLDivElement>(null);
  const mapRef = useRef<WorldMap | null>(null);
  const selectRef = useRef(onSelect);
  selectRef.current = onSelect;
  const membersMapRef = useRef<Map<string, MapMember>>(new Map());
  const linksMapRef = useRef<Map<string, MapLink>>(new Map());
  const serverRef = useRef<ServerNode | null>(server);
  serverRef.current = server;

  useEffect(() => {
    if (!node.current || mapRef.current) return;
    const map = new maplibregl.Map({
      container: node.current,
      style: darkLocalStyle,
      center: [server?.lon ?? 108, server?.lat ?? 30],
      zoom: 2.2,
      maxZoom: 17,
      attributionControl: false,
    });
    map.addControl(new maplibregl.NavigationControl({ showCompass: false }), "bottom-right");

    map.on("load", () => {
      const topo = world as unknown as Topology<{ countries: GeometryCollection<{ name: string }> }>;
      const shapes = feature(topo, topo.objects.countries) as unknown as FeatureCollection<Geometry, { name: string }>;

      map.addSource("countries", { type: "geojson", data: shapes });
      map.addLayer({
        id: "country-fill",
        type: "fill",
        source: "countries",
        paint: {
          "fill-color": "#0e2240",
          "fill-opacity": ["interpolate", ["linear"], ["zoom"], 1.5, 0.55, 4, 0.14, 6.5, 0.0],
        },
      });
      map.addLayer({
        id: "country-border",
        type: "line",
        source: "countries",
        paint: {
          "line-color": "rgba(56, 189, 248, 0.34)",
          "line-width": 0.9,
          "line-opacity": ["interpolate", ["linear"], ["zoom"], 2, 0.7, 7, 0.25],
        },
      });

      map.addSource("links-control", { type: "geojson", data: { type: "FeatureCollection", features: [] } });
      map.addLayer({
        id: "links-control",
        type: "line",
        source: "links-control",
        paint: {
          "line-color": "#38bdf8",
          "line-width": 1.2,
          "line-opacity": 0.35,
          "line-dasharray": [3, 3],
        },
      });

      map.addSource("links-active", { type: "geojson", data: { type: "FeatureCollection", features: [] } });
      map.addLayer({
        id: "links-active-glow",
        type: "line",
        source: "links-active",
        paint: {
          "line-color": ["get", "color"],
          "line-width": 6,
          "line-opacity": 0.22,
          "line-blur": 4,
        },
      });
      map.addLayer({
        id: "links-active",
        type: "line",
        source: "links-active",
        paint: {
          "line-color": ["get", "color"],
          "line-width": 2.2,
          "line-opacity": 0.92,
        },
      });

      map.addSource("cities", { type: "geojson", data: { type: "FeatureCollection", features: [] } });
      map.addLayer({
        id: "cities",
        type: "circle",
        source: "cities",
        paint: {
          "circle-color": "#38bdf8",
          "circle-opacity": 0.22,
          "circle-radius": ["interpolate", ["linear"], ["get", "count"], 1, 10, 20, 28],
          "circle-blur": 0.6,
        },
      });

      map.addSource("members", { type: "geojson", data: { type: "FeatureCollection", features: [] } });
      map.addLayer({
        id: "members-halo",
        type: "circle",
        source: "members",
        paint: {
          "circle-color": ["get", "color"],
          "circle-radius": 11,
          "circle-opacity": 0.28,
        },
      });
      map.addLayer({
        id: "members",
        type: "circle",
        source: "members",
        paint: {
          "circle-color": ["get", "color"],
          "circle-radius": 5.5,
          "circle-stroke-width": 2,
          "circle-stroke-color": "#050a14",
        },
      });

      map.addSource("server", { type: "geojson", data: { type: "FeatureCollection", features: [] } });
      map.addLayer({
        id: "server-halo",
        type: "circle",
        source: "server",
        paint: {
          "circle-color": "#a855f7",
          "circle-radius": 16,
          "circle-opacity": 0.3,
        },
      });
      map.addLayer({
        id: "server-dot",
        type: "circle",
        source: "server",
        paint: {
          "circle-color": "#c084fc",
          "circle-radius": 7.5,
          "circle-stroke-width": 2.5,
          "circle-stroke-color": "#ffffff",
        },
      });
    });

    map.on("click", "members", (event) => {
      const id = event.features?.[0]?.properties?.memberId as string | undefined;
      if (id && membersMapRef.current.has(id)) {
        selectRef.current({ kind: "member", member: membersMapRef.current.get(id)! });
      }
    });
    map.on("click", "server-dot", () => {
      if (serverRef.current) selectRef.current({ kind: "server", server: serverRef.current });
    });
    map.on("click", "links-active", (event) => {
      const id = event.features?.[0]?.properties?.linkId as string | undefined;
      if (id && linksMapRef.current.has(id)) {
        selectRef.current({ kind: "link", link: linksMapRef.current.get(id)! });
      }
    });

    for (const layer of ["members", "server-dot", "links-active"]) {
      map.on("mouseenter", layer, () => {
        map.getCanvas().style.cursor = "pointer";
      });
      map.on("mouseleave", layer, () => {
        map.getCanvas().style.cursor = "";
      });
    }

    mapRef.current = map;
    return () => {
      map.remove();
      mapRef.current = null;
    };
  }, []);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    const apply = () => {
      const counts = new Map(countries.map((item) => [item.country, item.count]));
      const max = Math.max(1, ...countries.map((item) => item.count));
      const countrySource = map.getSource("countries") as GeoJSONSource | undefined;
      if (countrySource) {
        const topo = world as unknown as Topology<{ countries: GeometryCollection<{ name: string }> }>;
        const shapes = feature(topo, topo.objects.countries) as unknown as FeatureCollection<Geometry, { name: string }>;
        shapes.features.forEach((shape) => {
          const code = alphaFromId(String(shape.id ?? ""));
          const count = counts.get(code) || 0;
          (shape.properties as { fillColor?: string }).fillColor = count
            ? `rgba(14, 165, 233, ${0.26 + 0.44 * (count / max)})`
            : "#10213d";
        });
        countrySource.setData(shapes);
        map.setPaintProperty("country-fill", "fill-color", ["get", "fillColor"]);
      }

      membersMapRef.current.clear();
      const memberFeatures = members
        .filter((m) => m.lat != null && m.lon != null)
        .map((m, idx) => {
          const memberId = m.id || `${m.room}:${m.mac}:${idx}`;
          membersMapRef.current.set(memberId, m);
          const color = isLoginMode
            ? m.mac === "ok"
              ? "#22d3ee"
              : m.mac === "logout"
                ? "#94a3b8"
                : "#f43f5e"
            : m.mode === "direct" || m.direct
              ? "#10b981"
              : m.mode === "relay" || m.tcpRelay
                ? "#f59e0b"
                : "#38bdf8";
          return {
            type: "Feature" as const,
            properties: { memberId, color },
            geometry: { type: "Point" as const, coordinates: [m.lon as number, m.lat as number] },
          };
        });
      (map.getSource("members") as GeoJSONSource | undefined)?.setData({
        type: "FeatureCollection",
        features: memberFeatures,
      });

      const serverFeatures =
        server && server.lat != null && server.lon != null
          ? [
              {
                type: "Feature" as const,
                properties: { name: server.name },
                geometry: { type: "Point" as const, coordinates: [server.lon, server.lat] },
              },
            ]
          : [];
      (map.getSource("server") as GeoJSONSource | undefined)?.setData({
        type: "FeatureCollection",
        features: serverFeatures,
      });

      linksMapRef.current.clear();
      const activeLinkFeatures = isLoginMode
        ? []
        : links
            .filter((l) => l.kind !== "control")
            .map((l) => {
              linksMapRef.current.set(l.id, l);
              return {
                type: "Feature" as const,
                properties: { linkId: l.id, color: l.kind === "direct" ? "#10b981" : "#f59e0b" },
                geometry: {
                  type: "LineString" as const,
                  coordinates: greatCircleCurve([l.from.lon, l.from.lat], [l.to.lon, l.to.lat]),
                },
              };
            });
      (map.getSource("links-active") as GeoJSONSource | undefined)?.setData({
        type: "FeatureCollection",
        features: activeLinkFeatures,
      });

      const controlLinkFeatures =
        isLoginMode || !showControlLinks
          ? []
          : links
              .filter((l) => l.kind === "control")
              .map((l) => ({
                type: "Feature" as const,
                properties: { linkId: l.id },
                geometry: {
                  type: "LineString" as const,
                  coordinates: greatCircleCurve([l.from.lon, l.from.lat], [l.to.lon, l.to.lat], 24),
                },
              }));
      (map.getSource("links-control") as GeoJSONSource | undefined)?.setData({
        type: "FeatureCollection",
        features: controlLinkFeatures,
      });

      (map.getSource("cities") as GeoJSONSource | undefined)?.setData({
        type: "FeatureCollection",
        features: cities.map((item) => ({
          type: "Feature" as const,
          properties: item,
          geometry: { type: "Point" as const, coordinates: [item.lon, item.lat] },
        })),
      });
    };

    if (map.isStyleLoaded()) apply();
    else map.once("load", apply);
  }, [server, countries, cities, members, links, isLoginMode, showControlLinks]);

  return <div ref={node} className="h-full w-full bg-[#060d1c]" />;
}
