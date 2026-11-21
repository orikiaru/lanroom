import { forwardRef, useEffect, useImperativeHandle, useRef, useState } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { feature } from "topojson-client";
import type { FeatureCollection, Geometry, Position } from "geojson";
import type { GeometryCollection, Topology } from "topojson-specification";
import world from "world-atlas/countries-50m.json";
import type { MapCountry, MapLink, MapMember, ServerNode } from "./api";
import { SphericalTileLayer, type DetailMapStyle, type ZoomTelemetry } from "./globeTiles";
import { alphaFromId } from "./iso";

const GLOBE_RADIUS = 1;
const DEG2RAD = Math.PI / 180;

export type SelectedEntity =
  | { kind: "server"; server: ServerNode }
  | { kind: "member"; member: MapMember }
  | { kind: "link"; link: MapLink };

export type Globe3DHandle = {
  focusLatLon: (lat: number, lon: number, distance?: number) => void;
  focusServer: () => void;
  fitAll: () => void;
  zoomIn: () => void;
  zoomOut: () => void;
};

type Globe3DProps = {
  server: ServerNode | null;
  members: MapMember[];
  links: MapLink[];
  countries: MapCountry[];
  isLoginMode: boolean;
  autoRotate: boolean;
  showControlLinks: boolean;
  detailMapStyle?: DetailMapStyle;
  selectedId: string | null;
  recentJoinIds?: string[];
  onZoomChange?: (info: ZoomTelemetry) => void;
  onSelect: (entity: SelectedEntity | null) => void;
};

type PositionedMember = {
  member: MapMember;
  lat: number;
  lon: number;
  id: string;
};

type HoverState = {
  x: number;
  y: number;
  title: string;
  subtitle: string;
  badge?: string;
  badgeColor?: string;
};

type AnimatedComet = {
  segments: THREE.Mesh[];
  curve: THREE.QuadraticBezierCurve3;
  speed: number;
  offset: number;
  reverse: boolean;
};

type AnimatedRing = {
  mesh: THREE.Mesh;
  phase: number;
  speed: number;
  maxScale: number;
  baseOpacity: number;
};

type TrackedLabel = {
  id: string;
  text: string;
  kind: "server" | "node";
  color: string;
  baseSurface: THREE.Vector3;
  baseOffset: number;
  priority: number;
};

type PickMeta =
  | { kind: "server"; server: ServerNode; title: string; subtitle: string; badge: string; badgeColor: string }
  | { kind: "member"; member: MapMember; title: string; subtitle: string; badge: string; badgeColor: string }
  | { kind: "link"; link: MapLink; title: string; subtitle: string; badge: string; badgeColor: string };

const worldFeatures = (() => {
  const topo = world as unknown as Topology<{ countries: GeometryCollection<{ name: string }> }>;
  return feature(topo, topo.objects.countries) as unknown as FeatureCollection<Geometry, { name: string }>;
})();

function latLonToVec3(lat: number, lon: number, radius: number): THREE.Vector3 {
  const phi = (90 - lat) * DEG2RAD;
  const theta = (lon + 180) * DEG2RAD;
  return new THREE.Vector3(
    -radius * Math.sin(phi) * Math.cos(theta),
    radius * Math.cos(phi),
    radius * Math.sin(phi) * Math.sin(theta),
  );
}

/**
 * Append subdivided 3D great-circle segments along a GeoJSON ring so vector borders
 * hug the curved sphere surface tightly with zero clipping and infinite zoom sharpness.
 */
function appendRingSegments(ring: Position[], radius: number, out: number[]) {
  if (ring.length < 2) return;
  for (let i = 0; i < ring.length - 1; i++) {
    const [lon1, lat1] = ring[i];
    const [lon2, lat2] = ring[i + 1];
    const dLon = lon2 - lon1;
    // Skip anti-meridian wrap jump
    if (Math.abs(dLon) > 180) continue;
    const dLat = lat2 - lat1;
    const distDeg = Math.hypot(dLon, dLat);
    const steps = Math.max(1, Math.ceil(distDeg / 2.2));
    let prev = latLonToVec3(lat1, lon1, radius);
    for (let s = 1; s <= steps; s++) {
      const f = s / steps;
      const cur = latLonToVec3(lat1 + dLat * f, lon1 + dLon * f, radius);
      out.push(prev.x, prev.y, prev.z, cur.x, cur.y, cur.z);
      prev = cur;
    }
  }
}

/**
 * Build crisp 3D vector coastlines & country borders directly on the sphere (never blurs at any zoom).
 */
function buildVectorBorders(activeCodes: Set<string> | null, radius: number, color: string, opacity: number): THREE.LineSegments {
  const verts: number[] = [];
  for (const feat of worldFeatures.features) {
    if (activeCodes) {
      const code = alphaFromId(String(feat.id ?? ""));
      if (!activeCodes.has(code)) continue;
    }
    const geom = feat.geometry;
    if (!geom) continue;
    if (geom.type === "Polygon") {
      geom.coordinates.forEach((ring) => appendRingSegments(ring, radius, verts));
    } else if (geom.type === "MultiPolygon") {
      geom.coordinates.forEach((poly) => poly.forEach((ring) => appendRingSegments(ring, radius, verts)));
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute("position", new THREE.Float32BufferAttribute(verts, 3));
  const mat = new THREE.LineBasicMaterial({
    color,
    transparent: true,
    opacity,
    depthWrite: false,
  });
  return new THREE.LineSegments(geo, mat);
}

/**
 * Build crisp 3D vector lat/lon graticule grid + highlighted equator directly on the sphere.
 */
function createVectorGraticule(): THREE.Group {
  const group = new THREE.Group();
  const r = GLOBE_RADIUS + 0.0009;
  const gridVerts: number[] = [];
  const eqVerts: number[] = [];

  // Meridians every 15°
  for (let lon = -180; lon < 180; lon += 15) {
    for (let lat = -80; lat < 80; lat += 4) {
      const a = latLonToVec3(lat, lon, r);
      const b = latLonToVec3(lat + 4, lon, r);
      gridVerts.push(a.x, a.y, a.z, b.x, b.y, b.z);
    }
  }
  // Parallels every 15°
  for (let lat = -75; lat <= 75; lat += 15) {
    const target = lat === 0 ? eqVerts : gridVerts;
    for (let lon = -180; lon < 180; lon += 3) {
      const a = latLonToVec3(lat, lon, r);
      const b = latLonToVec3(lat, lon + 3, r);
      target.push(a.x, a.y, a.z, b.x, b.y, b.z);
    }
  }

  const gridGeo = new THREE.BufferGeometry();
  gridGeo.setAttribute("position", new THREE.Float32BufferAttribute(gridVerts, 3));
  group.add(
    new THREE.LineSegments(
      gridGeo,
      new THREE.LineBasicMaterial({
        color: "#38bdf8",
        transparent: true,
        opacity: 0.08,
        depthWrite: false,
      }),
    ),
  );

  const eqGeo = new THREE.BufferGeometry();
  eqGeo.setAttribute("position", new THREE.Float32BufferAttribute(eqVerts, 3));
  group.add(
    new THREE.LineSegments(
      eqGeo,
      new THREE.LineBasicMaterial({
        color: "#38bdf8",
        transparent: true,
        opacity: 0.2,
        depthWrite: false,
      }),
    ),
  );

  return group;
}

/**
 * High-resolution 4096x2048 base surface texture for smooth ocean & land fills.
 * All coastlines, borders, and graticule lines are rendered as true 3D vectors above this sphere
 * so zooming in stays mathematically sharp with zero bitmap pixelation.
 */
function buildEarthTexture(countries: MapCountry[], maxAnisotropy: number): THREE.CanvasTexture {
  const width = 4096;
  const height = 2048;
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext("2d")!;

  // Deep obsidian-space ocean gradient
  const ocean = ctx.createLinearGradient(0, 0, 0, height);
  ocean.addColorStop(0, "#020612");
  ocean.addColorStop(0.24, "#040d20");
  ocean.addColorStop(0.5, "#06152f");
  ocean.addColorStop(0.76, "#040d20");
  ocean.addColorStop(1, "#020612");
  ctx.fillStyle = ocean;
  ctx.fillRect(0, 0, width, height);

  const countMap = new Map(countries.map((c) => [c.country, c.count]));
  const maxCount = Math.max(1, ...countries.map((c) => c.count));

  const project = ([lon, lat]: Position): [number, number] => [
    ((lon + 180) / 360) * width,
    ((90 - lat) / 180) * height,
  ];

  const traceRing = (ring: Position[]) => {
    if (ring.length < 2) return;
    let [prevX, prevY] = project(ring[0]);
    ctx.moveTo(prevX, prevY);
    for (let i = 1; i < ring.length; i++) {
      const [x, y] = project(ring[i]);
      if (Math.abs(x - prevX) > width * 0.5) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
      prevX = x;
      prevY = y;
    }
    ctx.closePath();
  };

  for (const feat of worldFeatures.features) {
    const code = alphaFromId(String(feat.id ?? ""));
    const active = countMap.get(code) || 0;
    const geom = feat.geometry;
    if (!geom) continue;

    ctx.beginPath();
    if (geom.type === "Polygon") {
      geom.coordinates.forEach(traceRing);
    } else if (geom.type === "MultiPolygon") {
      geom.coordinates.forEach((poly) => poly.forEach(traceRing));
    }

    if (active > 0) {
      const ratio = active / maxCount;
      ctx.fillStyle = `rgba(14, 165, 233, ${0.24 + ratio * 0.3})`;
      ctx.fill();
    } else {
      ctx.fillStyle = "#0f2342";
      ctx.fill();
    }
  }

  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = Math.max(4, maxAnisotropy);
  texture.minFilter = THREE.LinearMipmapLinearFilter;
  texture.magFilter = THREE.LinearFilter;
  texture.generateMipmaps = true;
  texture.needsUpdate = true;
  return texture;
}

function createOuterAtmosphere(): THREE.Mesh {
  const geo = new THREE.SphereGeometry(GLOBE_RADIUS * 1.14, 72, 72);
  const mat = new THREE.ShaderMaterial({
    vertexShader: `
      varying vec3 vNormal;
      void main() {
        vNormal = normalize(normalMatrix * normal);
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: `
      varying vec3 vNormal;
      void main() {
        float intensity = pow(0.66 - dot(vNormal, vec3(0.0, 0.0, 1.0)), 3.4);
        vec3 glow = mix(vec3(0.11, 0.45, 0.96), vec3(0.22, 0.84, 0.99), clamp(intensity * 1.5, 0.0, 1.0));
        gl_FragColor = vec4(glow, clamp(intensity * 0.95, 0.0, 0.78));
      }
    `,
    blending: THREE.AdditiveBlending,
    side: THREE.BackSide,
    transparent: true,
    depthWrite: false,
  });
  return new THREE.Mesh(geo, mat);
}

function createInnerLimbRim(): THREE.Mesh {
  const geo = new THREE.SphereGeometry(GLOBE_RADIUS * 1.0035, 96, 72);
  const mat = new THREE.ShaderMaterial({
    vertexShader: `
      varying vec3 vNormal;
      varying vec3 vViewDir;
      void main() {
        vec4 mvPos = modelViewMatrix * vec4(position, 1.0);
        vNormal = normalize(normalMatrix * normal);
        vViewDir = normalize(-mvPos.xyz);
        gl_Position = projectionMatrix * mvPos;
      }
    `,
    fragmentShader: `
      varying vec3 vNormal;
      varying vec3 vViewDir;
      void main() {
        float fresnel = pow(1.0 - max(0.0, dot(vNormal, vViewDir)), 3.8);
        vec3 rim = mix(vec3(0.12, 0.68, 0.98), vec3(0.55, 0.36, 0.96), 0.25);
        gl_FragColor = vec4(rim, fresnel * 0.5);
      }
    `,
    blending: THREE.AdditiveBlending,
    transparent: true,
    depthWrite: false,
  });
  return new THREE.Mesh(geo, mat);
}

function createOrbitalRings(): THREE.Group {
  const group = new THREE.Group();

  const makeRing = (radius: number, color: string, opacity: number, rotX: number, rotZ: number) => {
    const pts: THREE.Vector3[] = [];
    const segs = 160;
    for (let i = 0; i <= segs; i++) {
      const a = (i / segs) * Math.PI * 2;
      pts.push(new THREE.Vector3(Math.cos(a) * radius, 0, Math.sin(a) * radius));
    }
    const geo = new THREE.BufferGeometry().setFromPoints(pts);
    const mat = new THREE.LineDashedMaterial({
      color,
      dashSize: 0.055,
      gapSize: 0.04,
      transparent: true,
      opacity,
      depthWrite: false,
    });
    const line = new THREE.Line(geo, mat);
    line.computeLineDistances();
    line.rotation.x = rotX;
    line.rotation.z = rotZ;
    return line;
  };

  group.add(makeRing(GLOBE_RADIUS * 1.24, "#38bdf8", 0.15, 0.42, 0.18));
  group.add(makeRing(GLOBE_RADIUS * 1.36, "#8b5cf6", 0.1, -0.52, -0.26));
  return group;
}

function createStarfield(): THREE.Points {
  const count = 1400;
  const positions = new Float32Array(count * 3);
  const colors = new Float32Array(count * 3);
  for (let i = 0; i < count; i++) {
    const r = 10 + Math.random() * 20;
    const u = Math.random();
    const v = Math.random();
    const theta = 2 * Math.PI * u;
    const phi = Math.acos(2 * v - 1);
    positions[i * 3] = r * Math.sin(phi) * Math.cos(theta);
    positions[i * 3 + 1] = r * Math.sin(phi) * Math.sin(theta);
    positions[i * 3 + 2] = r * Math.cos(phi);

    const tint = Math.random();
    const c =
      tint > 0.72 ? new THREE.Color("#7dd3fc") : tint > 0.44 ? new THREE.Color("#c4b5fd") : new THREE.Color("#cbd5e1");
    colors[i * 3] = c.r;
    colors[i * 3 + 1] = c.g;
    colors[i * 3 + 2] = c.b;
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geo.setAttribute("color", new THREE.BufferAttribute(colors, 3));
  const mat = new THREE.PointsMaterial({
    size: 0.044,
    vertexColors: true,
    transparent: true,
    opacity: 0.58,
    depthWrite: false,
  });
  return new THREE.Points(geo, mat);
}

function spreadMembers(
  members: MapMember[],
  server: ServerNode | null,
): { list: PositionedMember[]; coordById: Map<string, [number, number]> } {
  const coordById = new Map<string, [number, number]>();
  const buckets = new Map<string, MapMember[]>();
  const serverKey =
    server && server.lat != null && server.lon != null ? `${server.lat.toFixed(2)},${server.lon.toFixed(2)}` : "";

  for (const member of members) {
    if (member.lat == null || member.lon == null) continue;
    const key = `${member.lat.toFixed(2)},${member.lon.toFixed(2)}`;
    const group = buckets.get(key) ?? [];
    group.push(member);
    buckets.set(key, group);
  }

  const list: PositionedMember[] = [];
  for (const [key, group] of buckets.entries()) {
    const sharesServer = key === serverKey;
    const total = group.length;
    group.forEach((member, idx) => {
      const id = member.id || `${member.room}:${member.mac}:${idx}`;
      let lat = member.lat as number;
      let lon = member.lon as number;
      if (total > 1 || sharesServer) {
        const angle = (idx / Math.max(total, 1)) * Math.PI * 2 + (sharesServer ? 0.55 : 0.25);
        const radiusDeg = sharesServer ? 0.14 : Math.min(0.24, 0.08 + total * 0.03);
        lat = Math.max(-84, Math.min(84, lat + Math.sin(angle) * radiusDeg));
        lon = lon + Math.cos(angle) * radiusDeg;
      }
      coordById.set(id, [lat, lon]);
      list.push({ member, lat, lon, id });
    });
  }
  return { list, coordById };
}

function orientToSurface(obj: THREE.Object3D, surfacePos: THREE.Vector3) {
  const normal = surfacePos.clone().normalize();
  obj.position.copy(surfacePos);
  obj.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), normal);
}

function modeColor(member: MapMember, isLoginMode: boolean): string {
  if (isLoginMode) {
    if (member.mac === "ok") return "#22d3ee";
    if (member.mac === "logout") return "#94a3b8";
    return "#f43f5e";
  }
  if (member.mode === "direct" || member.direct) return "#10b981";
  if (member.mode === "relay" || member.tcpRelay) return "#f59e0b";
  return "#38bdf8";
}

function modeLabel(member: MapMember, isLoginMode: boolean): string {
  if (isLoginMode) {
    if (member.mac === "ok") return "登录成功";
    if (member.mac === "logout") return "主动退出";
    return "登录失败";
  }
  if (member.mode === "direct" || member.direct) return "P2P 直连";
  if (member.mode === "relay" || member.tcpRelay) return "服务器中转";
  return "单节点待机";
}

function disposeGroup(group: THREE.Group) {
  while (group.children.length > 0) {
    const child = group.children[0];
    group.remove(child);
    child.traverse((node) => {
      if ((node as THREE.Mesh).geometry) {
        (node as THREE.Mesh).geometry.dispose();
      }
      const mat = (node as THREE.Mesh).material;
      if (Array.isArray(mat)) {
        mat.forEach((m) => m.dispose());
      } else if (mat) {
        mat.dispose();
      }
    });
  }
}

export const Globe3D = forwardRef<Globe3DHandle, Globe3DProps>(function Globe3D(
  {
    server,
    members,
    links,
    countries,
    isLoginMode,
    autoRotate,
    showControlLinks,
    detailMapStyle = "zh",
    selectedId,
    recentJoinIds = [],
    onZoomChange,
    onSelect,
  },
  ref,
) {
  const mountRef = useRef<HTMLDivElement>(null);
  const labelLayerRef = useRef<HTMLDivElement>(null);
  const [hover, setHover] = useState<HoverState | null>(null);
  const [webglError, setWebglError] = useState("");

  const onSelectRef = useRef(onSelect);
  onSelectRef.current = onSelect;
  const onZoomChangeRef = useRef(onZoomChange);
  onZoomChangeRef.current = onZoomChange;
  const autoRotateRef = useRef(autoRotate);
  autoRotateRef.current = autoRotate;
  const detailMapStyleRef = useRef<DetailMapStyle>(detailMapStyle);
  detailMapStyleRef.current = detailMapStyle;
  const serverRef = useRef(server);
  serverRef.current = server;
  const membersRef = useRef(members);
  membersRef.current = members;

  const countriesSigRef = useRef<string>("");
  const sceneSigRef = useRef<string>("");

  const sceneCtxRef = useRef<{
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera;
    controls: OrbitControls;
    maxAnisotropy: number;
    earthMat: THREE.MeshPhongMaterial;
    tileLayer: SphericalTileLayer;
    activeBorderGroup: THREE.Group;
    dynamicGroup: THREE.Group;
    scalableMarkers: THREE.Group[];
    arcTubes: { mat: THREE.MeshBasicMaterial; baseOpacity: number }[];
    pickables: THREE.Object3D[];
    comets: AnimatedComet[];
    rings: AnimatedRing[];
    serverCrystal: THREE.Mesh | null;
    serverOrbit: THREE.Mesh | null;
    labels: TrackedLabel[];
    flyTarget: { startPos: THREE.Vector3; endPos: THREE.Vector3; startTime: number; duration: number } | null;
    isDragging: boolean;
  } | null>(null);

  const flyToLatLon = (lat: number, lon: number, distance = 2.35) => {
    const ctx = sceneCtxRef.current;
    if (!ctx) return;
    const clampedDist = THREE.MathUtils.clamp(distance, 1.009, 4.5);
    const targetDir = latLonToVec3(lat, lon, 1).normalize();
    const endPos = targetDir.multiplyScalar(clampedDist);
    ctx.flyTarget = {
      startPos: ctx.camera.position.clone(),
      endPos,
      startTime: performance.now(),
      duration: 820,
    };
  };

  const smoothZoomFactor = (factor: number) => {
    const ctx = sceneCtxRef.current;
    if (!ctx) return;
    const curPos = ctx.camera.position.clone();
    const curDist = curPos.length();
    const curAlt = Math.max(0.008, curDist - GLOBE_RADIUS);
    const nextAlt = THREE.MathUtils.clamp(curAlt * factor, 0.009, 3.5);
    const nextPos = curPos.normalize().multiplyScalar(GLOBE_RADIUS + nextAlt);
    ctx.flyTarget = {
      startPos: ctx.camera.position.clone(),
      endPos: nextPos,
      startTime: performance.now(),
      duration: 420,
    };
  };

  useImperativeHandle(ref, () => ({
    focusLatLon: (lat, lon, distance) => flyToLatLon(lat, lon, distance ?? 1.48),
    focusServer: () => {
      const s = serverRef.current;
      if (s && s.lat != null && s.lon != null) {
        flyToLatLon(s.lat, s.lon, 1.42);
      } else {
        flyToLatLon(34, 108, 2.2);
      }
    },
    fitAll: () => {
      const pts: [number, number][] = [];
      const s = serverRef.current;
      if (s && s.lat != null && s.lon != null) pts.push([s.lat, s.lon]);
      for (const m of membersRef.current) {
        if (m.lat != null && m.lon != null) pts.push([m.lat, m.lon]);
      }
      if (pts.length === 0) {
        flyToLatLon(26, 108, 2.75);
        return;
      }
      const sumVec = new THREE.Vector3();
      for (const [la, lo] of pts) {
        sumVec.add(latLonToVec3(la, lo, 1));
      }
      if (sumVec.lengthSq() < 0.001) {
        flyToLatLon(pts[0][0], pts[0][1], 3.0);
        return;
      }
      sumVec.normalize();
      const lat = 90 - Math.acos(Math.max(-1, Math.min(1, sumVec.y))) / DEG2RAD;
      const lon = Math.atan2(sumVec.z, -sumVec.x) / DEG2RAD - 180;
      const normLon = ((lon + 540) % 360) - 180;
      let maxAngle = 0;
      for (const [la, lo] of pts) {
        const v = latLonToVec3(la, lo, 1);
        maxAngle = Math.max(maxAngle, sumVec.angleTo(v));
      }
      const dist = Math.min(3.55, Math.max(2.15, 2.0 + maxAngle * 1.15));
      flyToLatLon(lat, normLon, dist);
    },
    zoomIn: () => smoothZoomFactor(0.52),
    zoomOut: () => smoothZoomFactor(1.9),
  }));

  // 1. Initialize Three.js scene once
  useEffect(() => {
    const container = mountRef.current;
    if (!container) return;

    let renderer: THREE.WebGLRenderer;
    try {
      renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: "high-performance" });
    } catch {
      setWebglError("当前浏览器不支持 WebGL，请切换至 2D 平面视图。");
      return;
    }

    const maxAnisotropy = renderer.capabilities.getMaxAnisotropy();
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2.5));
    renderer.setSize(container.clientWidth, container.clientHeight);
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.12;
    container.innerHTML = "";
    container.appendChild(renderer.domElement);

    const scene = new THREE.Scene();

    const camera = new THREE.PerspectiveCamera(
      40,
      Math.max(1, container.clientWidth) / Math.max(1, container.clientHeight),
      0.01,
      100,
    );
    camera.position.copy(latLonToVec3(26, 108, 2.55));

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enablePan = false;
    controls.enableDamping = true;
    controls.dampingFactor = 0.065;
    controls.rotateSpeed = 0.52;
    controls.zoomSpeed = 0.85;
    controls.minDistance = 1.008; // Zoom all the way down to city districts & blocks (Z11)
    controls.maxDistance = 4.6;
    controls.autoRotate = autoRotateRef.current;
    controls.autoRotateSpeed = 0.25;

    const ambient = new THREE.AmbientLight("#b9d6f8", 1.38);
    scene.add(ambient);
    const keyLight = new THREE.DirectionalLight("#e0f2fe", 1.42);
    keyLight.position.set(5, 3, 5);
    scene.add(keyLight);
    const rimLight = new THREE.DirectionalLight("#7c3aed", 0.58);
    rimLight.position.set(-5, -2, -4);
    scene.add(rimLight);

    const stars = createStarfield();
    scene.add(stars);

    const orbitalRings = createOrbitalRings();
    scene.add(orbitalRings);

    // High-segment Earth sphere (128x96) so horizon stays ultra-smooth even when zoomed in
    const earthGeo = new THREE.SphereGeometry(GLOBE_RADIUS, 128, 96);
    const earthTexture = buildEarthTexture([], maxAnisotropy);
    const earthMat = new THREE.MeshPhongMaterial({
      map: earthTexture,
      specular: new THREE.Color("#0b244e"),
      shininess: 16,
      emissive: new THREE.Color("#040b18"),
      emissiveIntensity: 0.45,
    });
    const earthMesh = new THREE.Mesh(earthGeo, earthMat);
    scene.add(earthMesh);

    // Multi-zoom 3D spherical slippy-tile engine (streams detailed provinces/cities/districts on zoom!)
    const tileLayer = new SphericalTileLayer(GLOBE_RADIUS, maxAnisotropy);
    tileLayer.setStyle(detailMapStyleRef.current);
    scene.add(tileLayer.group);

    // True 3D vector graticule & base 50m coastlines (razor-sharp at ANY magnification!)
    const graticule = createVectorGraticule();
    scene.add(graticule);

    const baseBorders = buildVectorBorders(null, GLOBE_RADIUS + 0.0018, "#5aa5f8", 0.48);
    const baseBorderMat = baseBorders.material as THREE.LineBasicMaterial;
    scene.add(baseBorders);

    // Dynamic active-country glowing 3D vector border group
    const activeBorderGroup = new THREE.Group();
    scene.add(activeBorderGroup);

    const innerRim = createInnerLimbRim();
    scene.add(innerRim);

    const outerAtmos = createOuterAtmosphere();
    scene.add(outerAtmos);

    const dynamicGroup = new THREE.Group();
    scene.add(dynamicGroup);

    const ctx = {
      scene,
      camera,
      controls,
      maxAnisotropy,
      earthMat,
      tileLayer,
      activeBorderGroup,
      dynamicGroup,
      scalableMarkers: [] as THREE.Group[],
      arcTubes: [] as { mat: THREE.MeshBasicMaterial; baseOpacity: number }[],
      pickables: [] as THREE.Object3D[],
      comets: [] as AnimatedComet[],
      rings: [] as AnimatedRing[],
      serverCrystal: null as THREE.Mesh | null,
      serverOrbit: null as THREE.Mesh | null,
      labels: [] as TrackedLabel[],
      flyTarget: null as { startPos: THREE.Vector3; endPos: THREE.Vector3; startTime: number; duration: number } | null,
      isDragging: false,
    };
    sceneCtxRef.current = ctx;

    const raycaster = new THREE.Raycaster();
    const pointer = new THREE.Vector2();
    let pointerDownPos = { x: 0, y: 0 };

    const onControlStart = () => {
      ctx.isDragging = true;
      ctx.flyTarget = null;
    };
    const onControlEnd = () => {
      ctx.isDragging = false;
    };
    controls.addEventListener("start", onControlStart);
    controls.addEventListener("end", onControlEnd);

    const pickAt = (clientX: number, clientY: number): PickMeta | null => {
      const rect = renderer.domElement.getBoundingClientRect();
      pointer.x = ((clientX - rect.left) / rect.width) * 2 - 1;
      pointer.y = -((clientY - rect.top) / rect.height) * 2 + 1;
      raycaster.setFromCamera(pointer, camera);
      const hits = raycaster.intersectObjects(ctx.pickables, false);
      if (hits.length === 0) return null;
      return (hits[0].object.userData as PickMeta) || null;
    };

    const onPointerMove = (e: PointerEvent) => {
      if (ctx.isDragging) {
        setHover(null);
        return;
      }
      const meta = pickAt(e.clientX, e.clientY);
      const rect = renderer.domElement.getBoundingClientRect();
      if (meta) {
        renderer.domElement.style.cursor = "pointer";
        setHover({
          x: e.clientX - rect.left,
          y: e.clientY - rect.top,
          title: meta.title,
          subtitle: meta.subtitle,
          badge: meta.badge,
          badgeColor: meta.badgeColor,
        });
      } else {
        renderer.domElement.style.cursor = "grab";
        setHover(null);
      }
    };

    const onPointerDown = (e: PointerEvent) => {
      pointerDownPos = { x: e.clientX, y: e.clientY };
    };

    const onPointerUp = (e: PointerEvent) => {
      const dist = Math.hypot(e.clientX - pointerDownPos.x, e.clientY - pointerDownPos.y);
      if (dist > 6) return;
      const meta = pickAt(e.clientX, e.clientY);
      if (!meta) return;
      if (meta.kind === "server") {
        onSelectRef.current({ kind: "server", server: meta.server });
        if (meta.server.lat != null && meta.server.lon != null) {
          flyToLatLon(meta.server.lat, meta.server.lon, 1.38);
        }
      } else if (meta.kind === "member") {
        onSelectRef.current({ kind: "member", member: meta.member });
        if (meta.member.lat != null && meta.member.lon != null) {
          flyToLatLon(meta.member.lat, meta.member.lon, 1.24);
        }
      } else if (meta.kind === "link") {
        onSelectRef.current({ kind: "link", link: meta.link });
      }
    };

    const onPointerLeave = () => setHover(null);

    renderer.domElement.addEventListener("pointermove", onPointerMove);
    renderer.domElement.addEventListener("pointerdown", onPointerDown);
    renderer.domElement.addEventListener("pointerup", onPointerUp);
    renderer.domElement.addEventListener("pointerleave", onPointerLeave);

    const ro = new ResizeObserver(() => {
      const w = Math.max(1, container.clientWidth);
      const h = Math.max(1, container.clientHeight);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h);
    });
    ro.observe(container);

    let rafId = 0;
    const tempVec = new THREE.Vector3();
    const labelWorldPos = new THREE.Vector3();
    const camDir = new THREE.Vector3();
    const labelNodes = new Map<string, HTMLDivElement>();
    let lastReportedZoomKey = "";

    const animate = (now: number) => {
      rafId = requestAnimationFrame(animate);
      const t = now * 0.001;

      const camDistBefore = camera.position.length();
      // Pause auto-rotation automatically when zoomed in to inspect fine regions
      controls.autoRotate = autoRotateRef.current && !ctx.isDragging && !ctx.flyTarget && camDistBefore > 1.85;

      if (ctx.flyTarget) {
        const progress = Math.min(1, (now - ctx.flyTarget.startTime) / ctx.flyTarget.duration);
        const eased = 1 - Math.pow(1 - progress, 3);
        const startLen = ctx.flyTarget.startPos.length();
        const endLen = ctx.flyTarget.endPos.length();
        const dirA = ctx.flyTarget.startPos.clone().normalize();
        const dirB = ctx.flyTarget.endPos.clone().normalize();
        const q = new THREE.Quaternion().setFromUnitVectors(dirA, dirB);
        const stepQ = new THREE.Quaternion().slerpQuaternions(new THREE.Quaternion(), q, eased);
        const curDir = dirA.applyQuaternion(stepQ).normalize();
        const curLen = startLen + (endLen - startLen) * eased;
        camera.position.copy(curDir.multiplyScalar(curLen));
        camera.lookAt(0, 0, 0);
        if (progress >= 1) {
          ctx.flyTarget = null;
        }
      }

      // Dynamic near plane + zoom-adaptive LOD all the way down to city/district level (Z11)
      const camDist = camera.position.length();
      const altitude = Math.max(0.008, camDist - GLOBE_RADIUS);
      const desiredNear = Math.max(0.001, altitude * 0.14);
      if (Math.abs(camera.near - desiredNear) / camera.near > 0.14) {
        camera.near = desiredNear;
        camera.updateProjectionMatrix();
      }

      controls.rotateSpeed = THREE.MathUtils.clamp(altitude * 0.36 + 0.012, 0.014, 0.56);
      const markerScale = THREE.MathUtils.clamp(altitude * 0.62 + 0.035, 0.045, 1.08);
      const cometScale = THREE.MathUtils.clamp(altitude * 0.6 + 0.08, 0.1, 1.05);

      for (const mGroup of ctx.scalableMarkers) {
        mGroup.scale.setScalar(markerScale);
      }
      // Fade out wide glowing 3D tubes at close regional/city zoom so only crisp 1px filaments remain
      const tubeZoomFade = THREE.MathUtils.clamp((altitude - 0.08) / 0.34, 0, 1);
      for (const tube of ctx.arcTubes) {
        tube.mat.opacity = tube.baseOpacity * tubeZoomFade;
      }

      controls.update();

      // Update multi-zoom 3D spherical slippy tiles & subtly dial back coarse outlines at deep zoom
      const zoomInfo = tileLayer.update(camera, now);
      const zoomKey = `${zoomInfo.zoom}:${zoomInfo.tilesActive}`;
      if (zoomKey !== lastReportedZoomKey) {
        lastReportedZoomKey = zoomKey;
        onZoomChangeRef.current?.(zoomInfo);
      }
      const deepFade = zoomInfo.tilesActive ? THREE.MathUtils.clamp((altitude - 0.06) / 0.55, 0.18, 1) : 1;
      baseBorderMat.opacity = 0.48 * deepFade;
      graticule.visible = !zoomInfo.tilesActive || altitude > 0.25;

      if (ctx.serverCrystal) {
        ctx.serverCrystal.rotation.y = t * 1.3;
        ctx.serverCrystal.rotation.x = Math.sin(t * 0.9) * 0.25;
      }
      if (ctx.serverOrbit) {
        ctx.serverOrbit.rotation.z = t * 1.1;
      }

      for (const ring of ctx.rings) {
        const cycle = (t * ring.speed + ring.phase) % 1;
        const scale = 1 + cycle * ring.maxScale;
        ring.mesh.scale.set(scale, scale, 1);
        const mat = ring.mesh.material as THREE.MeshBasicMaterial;
        mat.opacity = (1 - cycle) * (1 - cycle) * ring.baseOpacity;
      }

      for (const comet of ctx.comets) {
        const raw = (t * comet.speed + comet.offset) % 1;
        const headU = comet.reverse ? 1 - raw : raw;
        const dir = comet.reverse ? 1 : -1;
        const segCount = comet.segments.length;
        for (let i = 0; i < segCount; i++) {
          const segU = headU + dir * i * 0.02;
          const segMesh = comet.segments[i];
          if (segU < 0.01 || segU > 0.99) {
            segMesh.visible = false;
            continue;
          }
          segMesh.visible = true;
          comet.curve.getPoint(segU, segMesh.position);
          const edgeFade = Math.sin(segU * Math.PI);
          const decay = 1 - i / segCount;
          segMesh.scale.setScalar((0.55 + edgeFade * 0.55) * decay * cometScale);
        }
      }

      stars.rotation.y = t * 0.005;
      orbitalRings.rotation.y = t * 0.025;

      renderer.render(scene, camera);

      // Projected 2D HTML labels synced with zoom-adaptive marker height & collision avoidance
      const labelRoot = labelLayerRef.current;
      if (labelRoot) {
        const width = container.clientWidth;
        const height = container.clientHeight;
        camDir.copy(camera.position).normalize();
        const activeIds = new Set<string>();
        const placedBoxes: { x1: number; y1: number; x2: number; y2: number }[] = [];

        const sortedLabels = [...ctx.labels].sort((a, b) => b.priority - a.priority);
        for (const item of sortedLabels) {
          activeIds.add(item.id);
          let el = labelNodes.get(item.id);
          if (!el) {
            el = document.createElement("div");
            el.className =
              item.kind === "server"
                ? "pointer-events-none absolute left-0 top-0 rounded-full border border-violet-400/55 bg-[#0b0f26]/92 px-2.5 py-0.5 text-[11px] font-medium tracking-wide text-violet-100 shadow-[0_0_22px_rgba(139,92,246,0.48)] backdrop-blur-md whitespace-nowrap"
                : "pointer-events-none absolute left-0 top-0 rounded-full border border-slate-400/25 bg-[#061022]/86 px-2 py-0.5 text-[10px] font-medium text-slate-100 shadow-lg backdrop-blur-sm whitespace-nowrap";
            el.innerHTML =
              item.kind === "server"
                ? `<span style="display:inline-block;width:6px;height:6px;border-radius:99px;background:${item.color};margin-right:6px;box-shadow:0 0 8px ${item.color}"></span>${item.text}`
                : `<span style="display:inline-block;width:5px;height:5px;border-radius:99px;background:${item.color};margin-right:5px;box-shadow:0 0 6px ${item.color}"></span>${item.text}`;
            labelRoot.appendChild(el);
            labelNodes.set(item.id, el);
          }

          const dot = item.baseSurface.dot(camDir);
          if (dot < 0.12) {
            el.style.opacity = "0";
            continue;
          }
          labelWorldPos
            .copy(item.baseSurface)
            .multiplyScalar(GLOBE_RADIUS + 0.003 + item.baseOffset * markerScale);
          tempVec.copy(labelWorldPos).project(camera);
          // Hide labels projected outside the viewport
          if (tempVec.z > 1 || tempVec.x < -1.1 || tempVec.x > 1.1 || tempVec.y < -1.1 || tempVec.y > 1.1) {
            el.style.opacity = "0";
            continue;
          }
          const x = (tempVec.x * 0.5 + 0.5) * width;
          const y = (-tempVec.y * 0.5 + 0.5) * height - (item.kind === "server" ? 12 : 8);

          const estW = Math.min(200, Math.max(88, item.text.length * 7.2 + 28));
          const estH = 24;
          const box = { x1: x - estW / 2 - 4, y1: y - estH - 4, x2: x + estW / 2 + 4, y2: y + 4 };
          const collides =
            item.priority < 90 &&
            placedBoxes.some((b) => !(box.x2 < b.x1 || box.x1 > b.x2 || box.y2 < b.y1 || box.y1 > b.y2));

          if (collides) {
            el.style.opacity = "0";
            continue;
          }
          placedBoxes.push(box);
          el.style.opacity = String(Math.min(1, (dot - 0.12) * 3.6));
          el.style.transform = `translate3d(${x.toFixed(1)}px, ${y.toFixed(1)}px, 0) translate(-50%, -100%)`;
        }

        for (const [id, el] of labelNodes.entries()) {
          if (!activeIds.has(id)) {
            el.remove();
            labelNodes.delete(id);
          }
        }
      }
    };

    rafId = requestAnimationFrame(animate);

    return () => {
      cancelAnimationFrame(rafId);
      ro.disconnect();
      controls.removeEventListener("start", onControlStart);
      controls.removeEventListener("end", onControlEnd);
      controls.dispose();
      renderer.domElement.removeEventListener("pointermove", onPointerMove);
      renderer.domElement.removeEventListener("pointerdown", onPointerDown);
      renderer.domElement.removeEventListener("pointerup", onPointerUp);
      renderer.domElement.removeEventListener("pointerleave", onPointerLeave);
      tileLayer.dispose();
      disposeGroup(dynamicGroup);
      disposeGroup(activeBorderGroup);
      disposeGroup(graticule);
      disposeGroup(orbitalRings);
      (baseBorders.geometry as THREE.BufferGeometry).dispose();
      (baseBorders.material as THREE.Material).dispose();
      earthGeo.dispose();
      earthMat.map?.dispose();
      earthMat.dispose();
      (innerRim.geometry as THREE.BufferGeometry).dispose();
      (innerRim.material as THREE.Material).dispose();
      (outerAtmos.geometry as THREE.BufferGeometry).dispose();
      (outerAtmos.material as THREE.Material).dispose();
      (stars.geometry as THREE.BufferGeometry).dispose();
      (stars.material as THREE.Material).dispose();
      renderer.dispose();
      if (labelLayerRef.current) labelLayerRef.current.innerHTML = "";
      sceneCtxRef.current = null;
      countriesSigRef.current = "";
      sceneSigRef.current = "";
    };
  }, []);

  useEffect(() => {
    sceneCtxRef.current?.tileLayer.setStyle(detailMapStyle);
  }, [detailMapStyle]);

  // 2. Refresh 4096px Earth fill texture + luminous 3D vector borders ONLY when active countries change
  useEffect(() => {
    const ctx = sceneCtxRef.current;
    if (!ctx) return;
    const sig = countries
      .map((c) => `${c.country}:${c.count}`)
      .sort()
      .join("|");
    if (sig === countriesSigRef.current) return;
    countriesSigRef.current = sig;

    const nextTex = buildEarthTexture(countries, ctx.maxAnisotropy);
    const prevTex = ctx.earthMat.map;
    ctx.earthMat.map = nextTex;
    ctx.earthMat.needsUpdate = true;
    prevTex?.dispose();

    disposeGroup(ctx.activeBorderGroup);
    if (countries.length > 0) {
      const activeSet = new Set(countries.map((c) => c.country));
      const activeBorders = buildVectorBorders(activeSet, GLOBE_RADIUS + 0.0025, "#7dd3fc", 0.95);
      ctx.activeBorderGroup.add(activeBorders);
    }
  }, [countries]);

  // 3. Rebuild 3D markers & 3D communication arcs ONLY when topology actually changes (zero flicker)
  useEffect(() => {
    const ctx = sceneCtxRef.current;
    if (!ctx) return;

    const sig = JSON.stringify({
      s: server ? `${server.ip}:${server.lat}:${server.lon}:${server.name}` : "",
      m: members.map((x) => `${x.id}:${x.mode}:${x.lat}:${x.lon}:${x.virtualIp}:${x.relayCount ?? 0}`),
      l: links.map((x) => `${x.id}:${x.kind}:${x.relayCount ?? 0}`),
      im: isLoginMode,
      sc: showControlLinks,
      sel: selectedId,
      rj: recentJoinIds,
    });
    if (sig === sceneSigRef.current) return;
    sceneSigRef.current = sig;

    disposeGroup(ctx.dynamicGroup);
    ctx.scalableMarkers = [];
    ctx.arcTubes = [];
    ctx.pickables = [];
    ctx.comets = [];
    ctx.rings = [];
    ctx.serverCrystal = null;
    ctx.serverOrbit = null;
    ctx.labels = [];

    const recentSet = new Set(recentJoinIds);
    const { list: positionedMembers, coordById } = spreadMembers(members, server);

    // A. Render Server 3D Spire
    if (server && server.lat != null && server.lon != null) {
      const surfaceUnit = latLonToVec3(server.lat, server.lon, 1);
      const basePos = surfaceUnit.clone().multiplyScalar(GLOBE_RADIUS + 0.003);
      const serverGroup = new THREE.Group();
      orientToSurface(serverGroup, basePos);

      const baseHex = new THREE.Mesh(
        new THREE.CircleGeometry(0.024, 6),
        new THREE.MeshBasicMaterial({ color: "#a855f7", transparent: true, opacity: 0.92, side: THREE.DoubleSide }),
      );
      baseHex.rotation.x = -Math.PI / 2;
      serverGroup.add(baseHex);

      const spireHeight = 0.13;
      const spire = new THREE.Mesh(
        new THREE.CylinderGeometry(0.002, 0.0085, spireHeight, 12),
        new THREE.MeshBasicMaterial({
          color: "#8b5cf6",
          transparent: true,
          opacity: 0.85,
          blending: THREE.AdditiveBlending,
          depthWrite: false,
        }),
      );
      spire.position.y = spireHeight / 2;
      serverGroup.add(spire);

      const crystal = new THREE.Mesh(
        new THREE.OctahedronGeometry(0.02, 0),
        new THREE.MeshBasicMaterial({ color: "#f3e8ff" }),
      );
      crystal.position.y = spireHeight + 0.02;
      serverGroup.add(crystal);
      ctx.serverCrystal = crystal;

      const orbitRing = new THREE.Mesh(
        new THREE.TorusGeometry(0.034, 0.002, 8, 36),
        new THREE.MeshBasicMaterial({
          color: "#c084fc",
          transparent: true,
          opacity: 0.85,
          blending: THREE.AdditiveBlending,
          depthWrite: false,
        }),
      );
      orbitRing.position.y = spireHeight + 0.02;
      orbitRing.rotation.x = Math.PI / 2.6;
      serverGroup.add(orbitRing);
      ctx.serverOrbit = orbitRing;

      for (let i = 0; i < 2; i++) {
        const ring = new THREE.Mesh(
          new THREE.RingGeometry(0.02, 0.027, 40),
          new THREE.MeshBasicMaterial({
            color: "#c084fc",
            transparent: true,
            opacity: 0.82,
            side: THREE.DoubleSide,
            blending: THREE.AdditiveBlending,
            depthWrite: false,
          }),
        );
        ring.rotation.x = -Math.PI / 2;
        ring.position.y = 0.002;
        serverGroup.add(ring);
        ctx.rings.push({ mesh: ring, phase: i * 0.5, speed: 0.42, maxScale: 3.2, baseOpacity: 0.82 });
      }

      const hitMesh = new THREE.Mesh(
        new THREE.SphereGeometry(0.065, 12, 12),
        new THREE.MeshBasicMaterial({ visible: false }),
      );
      hitMesh.position.y = spireHeight * 0.6;
      hitMesh.userData = {
        kind: "server",
        server,
        title: server.name || "Lanroom 主节点",
        subtitle: `${server.city || "机房"} ${server.country || ""} · ${server.ip}`,
        badge: "SERVER",
        badgeColor: "#a855f7",
      } satisfies PickMeta;
      serverGroup.add(hitMesh);
      ctx.pickables.push(hitMesh);

      ctx.dynamicGroup.add(serverGroup);
      ctx.scalableMarkers.push(serverGroup);

      ctx.labels.push({
        id: "server-label",
        text: `${server.name || "SERVER"}${server.city ? ` · ${server.city}` : ""}`,
        kind: "server",
        color: "#c084fc",
        baseSurface: surfaceUnit,
        baseOffset: spireHeight + 0.048,
        priority: 100,
      });
    }

    // B. Render Client Node 3D Beacons
    positionedMembers.forEach(({ member, lat, lon, id }, index) => {
      const color = modeColor(member, isLoginMode);
      const isSelected = selectedId === id;
      const isFreshJoin = recentSet.has(id);
      const surfaceUnit = latLonToVec3(lat, lon, 1);
      const basePos = surfaceUnit.clone().multiplyScalar(GLOBE_RADIUS + 0.003);
      const nodeGroup = new THREE.Group();
      orientToSurface(nodeGroup, basePos);

      const pinHeight = isSelected ? 0.068 : isFreshJoin ? 0.06 : 0.042;

      const pin = new THREE.Mesh(
        new THREE.CylinderGeometry(0.0015, 0.0048, pinHeight, 10),
        new THREE.MeshBasicMaterial({
          color,
          transparent: true,
          opacity: 0.9,
          blending: THREE.AdditiveBlending,
          depthWrite: false,
        }),
      );
      pin.position.y = pinHeight / 2;
      nodeGroup.add(pin);

      const head = new THREE.Mesh(
        new THREE.SphereGeometry(isSelected ? 0.016 : 0.0105, 16, 16),
        new THREE.MeshBasicMaterial({ color }),
      );
      head.position.y = pinHeight;
      nodeGroup.add(head);

      const headGlow = new THREE.Mesh(
        new THREE.SphereGeometry(isSelected ? 0.025 : 0.017, 12, 12),
        new THREE.MeshBasicMaterial({
          color,
          transparent: true,
          opacity: 0.34,
          blending: THREE.AdditiveBlending,
          depthWrite: false,
        }),
      );
      headGlow.position.y = pinHeight;
      nodeGroup.add(headGlow);

      const ring = new THREE.Mesh(
        new THREE.RingGeometry(0.011, 0.0165, 32),
        new THREE.MeshBasicMaterial({
          color,
          transparent: true,
          opacity: 0.78,
          side: THREE.DoubleSide,
          blending: THREE.AdditiveBlending,
          depthWrite: false,
        }),
      );
      ring.rotation.x = -Math.PI / 2;
      ring.position.y = 0.002;
      nodeGroup.add(ring);
      ctx.rings.push({
        mesh: ring,
        phase: (index * 0.27) % 1,
        speed: isSelected ? 0.85 : 0.5,
        maxScale: isSelected ? 3.4 : 2.4,
        baseOpacity: 0.78,
      });

      if (isFreshJoin) {
        const burstRing = new THREE.Mesh(
          new THREE.RingGeometry(0.014, 0.025, 36),
          new THREE.MeshBasicMaterial({
            color: "#6ee7b7",
            transparent: true,
            opacity: 0.95,
            side: THREE.DoubleSide,
            blending: THREE.AdditiveBlending,
            depthWrite: false,
          }),
        );
        burstRing.rotation.x = -Math.PI / 2;
        burstRing.position.y = 0.003;
        nodeGroup.add(burstRing);
        ctx.rings.push({
          mesh: burstRing,
          phase: 0,
          speed: 1.25,
          maxScale: 5.2,
          baseOpacity: 0.95,
        });
      }

      const hitMesh = new THREE.Mesh(
        new THREE.SphereGeometry(0.045, 10, 10),
        new THREE.MeshBasicMaterial({ visible: false }),
      );
      hitMesh.position.y = pinHeight * 0.6;
      hitMesh.userData = {
        kind: "member",
        member,
        title: isLoginMode ? `${member.owner} (${member.source})` : `${member.room} · ${member.virtualIp}`,
        subtitle: `${member.city || "未定位"} ${member.country || ""} · ${member.source}`,
        badge: modeLabel(member, isLoginMode),
        badgeColor: color,
      } satisfies PickMeta;
      nodeGroup.add(hitMesh);
      ctx.pickables.push(hitMesh);

      ctx.dynamicGroup.add(nodeGroup);
      ctx.scalableMarkers.push(nodeGroup);

      if (!isLoginMode && (positionedMembers.length <= 18 || isSelected || isFreshJoin)) {
        ctx.labels.push({
          id: `node-${id}`,
          text: `${member.virtualIp}${member.city ? ` · ${member.city}` : ""}`,
          kind: "node",
          color,
          baseSurface: surfaceUnit,
          baseOffset: pinHeight + 0.022,
          priority: isSelected ? 92 : isFreshJoin ? 85 : 50,
        });
      }
    });

    // C. Render 3D Elevated Communication Arcs + Sleek Comet-Trail Pulses
    if (!isLoginMode) {
      const visibleLinks = links.filter((link) => showControlLinks || link.kind !== "control");
      visibleLinks.forEach((link, idx) => {
        const fromSpread = coordById.get(link.fromId);
        const toSpread = link.toId === "server" ? null : coordById.get(link.toId);
        const fromLat = fromSpread ? fromSpread[0] : link.from.lat;
        const fromLon = fromSpread ? fromSpread[1] : link.from.lon;
        const toLat = toSpread ? toSpread[0] : link.to.lat;
        const toLon = toSpread ? toSpread[1] : link.to.lon;

        const start = latLonToVec3(fromLat, fromLon, GLOBE_RADIUS + 0.01);
        const end = latLonToVec3(toLat, toLon, GLOBE_RADIUS + (link.toId === "server" ? 0.075 : 0.01));

        const angle = start.clone().normalize().angleTo(end.clone().normalize());
        const baseLift = link.kind === "direct" ? 0.16 : link.kind === "relay" ? 0.14 : 0.09;
        const lift = Math.min(0.58, Math.max(baseLift, angle * 0.4));

        const mid = start.clone().add(end);
        if (mid.lengthSq() < 1e-4) {
          mid.set(0, 1, 0);
        }
        mid.normalize().multiplyScalar(GLOBE_RADIUS + lift);

        const curve = new THREE.QuadraticBezierCurve3(start, mid, end);
        const isSelected = selectedId === link.id;
        const arcColor =
          link.kind === "direct" ? "#10b981" : link.kind === "relay" ? "#f59e0b" : "#38bdf8";
        const pulseColor =
          link.kind === "direct" ? "#6ee7b7" : link.kind === "relay" ? "#fde68a" : "#7dd3fc";

        if (link.kind === "control") {
          const pts = curve.getPoints(56);
          const lineGeo = new THREE.BufferGeometry().setFromPoints(pts);
          const lineMat = new THREE.LineDashedMaterial({
            color: arcColor,
            dashSize: 0.018,
            gapSize: 0.02,
            transparent: true,
            opacity: isSelected ? 0.85 : 0.24,
          });
          const line = new THREE.Line(lineGeo, lineMat);
          line.computeLineDistances();
          ctx.dynamicGroup.add(line);
        } else {
          // Fine glowing 3D tube (fades cleanly at city zoom so thin 1px coreLine stays razor-sharp)
          const tubeRadius = isSelected ? 0.004 : 0.002;
          const tubeBaseOpacity = isSelected ? 0.88 : 0.48;
          const tubeMat = new THREE.MeshBasicMaterial({
            color: arcColor,
            transparent: true,
            opacity: tubeBaseOpacity,
            blending: THREE.AdditiveBlending,
            depthWrite: false,
          });
          const tube = new THREE.Mesh(new THREE.TubeGeometry(curve, 72, tubeRadius, 8, false), tubeMat);
          ctx.dynamicGroup.add(tube);
          ctx.arcTubes.push({ mat: tubeMat, baseOpacity: tubeBaseOpacity });

          // Razor-sharp inner filament
          const pts = curve.getPoints(72);
          const coreLine = new THREE.Line(
            new THREE.BufferGeometry().setFromPoints(pts),
            new THREE.LineBasicMaterial({
              color: pulseColor,
              transparent: true,
              opacity: isSelected ? 1 : 0.78,
              blending: THREE.AdditiveBlending,
            }),
          );
          ctx.dynamicGroup.add(coreLine);

          // Bidirectional comet-trail pulses
          for (let p = 0; p < 2; p++) {
            const segments: THREE.Mesh[] = [];
            const tailSteps = 5;
            for (let s = 0; s < tailSteps; s++) {
              const r = s === 0 ? 0.0062 : 0.0048 * (1 - s * 0.14);
              const segMesh = new THREE.Mesh(
                new THREE.SphereGeometry(r, 8, 8),
                new THREE.MeshBasicMaterial({
                  color: s === 0 ? "#ffffff" : pulseColor,
                  transparent: true,
                  opacity: s === 0 ? 0.96 : 0.65 * (1 - s / tailSteps),
                  blending: THREE.AdditiveBlending,
                  depthWrite: false,
                }),
              );
              ctx.dynamicGroup.add(segMesh);
              segments.push(segMesh);
            }
            ctx.comets.push({
              segments,
              curve,
              speed: link.kind === "direct" ? 0.4 : 0.3,
              offset: (idx * 0.19 + p * 0.5) % 1,
              reverse: p % 2 === 1,
            });
          }

          const hitTube = new THREE.Mesh(
            new THREE.TubeGeometry(curve, 24, 0.015, 6, false),
            new THREE.MeshBasicMaterial({ visible: false }),
          );
          hitTube.userData = {
            kind: "link",
            link,
            title: `${link.from.label} ↔ ${link.to.label}`,
            subtitle: `房间 ${link.room}${link.relayCount ? ` · 累计中转 ${link.relayCount} 帧` : ""}`,
            badge: link.kind === "direct" ? "P2P 3D 直连" : "服务器 3D 中转",
            badgeColor: arcColor,
          } satisfies PickMeta;
          ctx.dynamicGroup.add(hitTube);
          ctx.pickables.push(hitTube);
        }
      });
    }
  }, [server, members, links, isLoginMode, showControlLinks, selectedId, recentJoinIds]);

  if (webglError) {
    return (
      <div className="grid h-full min-h-[540px] place-items-center rounded-2xl border border-slate-700/40 bg-[#060d1c] p-8 text-center">
        <div className="max-w-sm space-y-2">
          <p className="text-sm font-medium text-slate-200">{webglError}</p>
          <p className="text-xs text-slate-400">可点击右上角切换到 2D 平面视图查看节点与链路。</p>
        </div>
      </div>
    );
  }

  return (
    <div className="globe-stage-bg relative h-full w-full overflow-hidden">
      <div ref={mountRef} className="h-full w-full cursor-grab active:cursor-grabbing" />
      <div ref={labelLayerRef} className="pointer-events-none absolute inset-0 overflow-hidden" />
      {hover && (
        <div
          className="hud-glass pointer-events-none absolute z-30 max-w-xs rounded-xl px-3 py-2 text-xs text-slate-100 transition-opacity duration-100"
          style={{
            left: Math.min(hover.x + 14, (mountRef.current?.clientWidth ?? 800) - 240),
            top: Math.max(12, hover.y - 12),
          }}
        >
          <div className="flex items-center gap-2">
            <span className="font-semibold text-white">{hover.title}</span>
            {hover.badge && (
              <span
                className="rounded-full px-1.5 py-0.5 text-[10px] font-medium"
                style={{
                  backgroundColor: `${hover.badgeColor}24`,
                  color: hover.badgeColor,
                  border: `1px solid ${hover.badgeColor}55`,
                }}
              >
                {hover.badge}
              </span>
            )}
          </div>
          <p className="mt-0.5 text-[11px] text-slate-300">{hover.subtitle}</p>
        </div>
      )}
    </div>
  );
});
