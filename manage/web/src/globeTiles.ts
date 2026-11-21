import * as THREE from "three";

const DEG2RAD = Math.PI / 180;
const MAX_CACHED_TILES = 160;
const MIN_TILE_ZOOM = 4;
const MAX_TILE_ZOOM = 12;

export type DetailMapStyle = "zh" | "satellite" | "dark" | "off";

function latLonToVec3(lat: number, lon: number, radius: number): THREE.Vector3 {
  const phi = (90 - lat) * DEG2RAD;
  const theta = (lon + 180) * DEG2RAD;
  return new THREE.Vector3(
    -radius * Math.sin(phi) * Math.cos(theta),
    radius * Math.cos(phi),
    radius * Math.sin(phi) * Math.sin(theta),
  );
}

function vec3ToLatLon(v: THREE.Vector3): { lat: number; lon: number } {
  const n = v.clone().normalize();
  const lat = 90 - Math.acos(Math.max(-1, Math.min(1, n.y))) / DEG2RAD;
  const rawLon = Math.atan2(n.z, -n.x) / DEG2RAD - 180;
  const lon = ((rawLon + 540) % 360) - 180;
  return { lat, lon };
}

function latLonToTileFloat(lat: number, lon: number, z: number): { fx: number; fy: number } {
  const n = Math.pow(2, z);
  const clampedLat = Math.max(-85.0511, Math.min(85.0511, lat));
  const latRad = clampedLat * DEG2RAD;
  const fx = ((lon + 180) / 360) * n;
  const fy = ((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2) * n;
  return { fx, fy };
}

function buildSphericalTileGeometry(z: number, x: number, y: number, radius: number): THREE.BufferGeometry {
  const n = Math.pow(2, z);
  const segs = z <= 5 ? 10 : z <= 7 ? 6 : 4;
  const cols = segs + 1;
  const positions = new Float32Array(cols * cols * 3);
  const uvs = new Float32Array(cols * cols * 2);
  const indices: number[] = [];

  let pIdx = 0;
  let uvIdx = 0;
  for (let row = 0; row <= segs; row++) {
    const v = row / segs; // 0 = north edge, 1 = south edge
    const mercY = Math.PI * (1 - (2 * (y + v)) / n);
    const lat = Math.atan(Math.sinh(mercY)) / DEG2RAD;
    for (let col = 0; col <= segs; col++) {
      const u = col / segs; // 0 = west edge, 1 = east edge
      const lon = ((x + u) / n) * 360 - 180;
      const p = latLonToVec3(lat, lon, radius);
      positions[pIdx++] = p.x;
      positions[pIdx++] = p.y;
      positions[pIdx++] = p.z;
      uvs[uvIdx++] = u;
      uvs[uvIdx++] = 1 - v;
    }
  }

  for (let row = 0; row < segs; row++) {
    for (let col = 0; col < segs; col++) {
      const a = row * cols + col;
      const b = a + 1;
      const c = a + cols;
      const d = c + 1;
      indices.push(a, c, b, c, d, b);
    }
  }

  const geo = new THREE.BufferGeometry();
  geo.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geo.setAttribute("uv", new THREE.BufferAttribute(uvs, 2));
  geo.setIndex(indices);
  return geo;
}

function loadTileImage(url: string): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.decoding = "async";
    img.onload = () => resolve(img);
    img.onerror = (err) => reject(err);
    img.src = url;
  });
}

type ActiveStyle = Exclude<DetailMapStyle, "off">;

type TileEntry = {
  key: string;
  style: ActiveStyle;
  z: number;
  x: number;
  y: number;
  mesh: THREE.Mesh;
  mat: THREE.MeshBasicMaterial;
  loaded: boolean;
  fade: number;
  lastUsed: number;
};

export type ZoomTelemetry = {
  zoom: number;
  scaleLabel: string;
  tilesActive: boolean;
};

export function zoomLabelFor(z: number, tilesActive: boolean): string {
  if (!tilesActive) return "全球视野";
  if (z <= 4) return "大洲区块";
  if (z <= 5) return "省区视野";
  if (z <= 7) return "城市群";
  if (z <= 9) return "城区细节";
  return "街道区块";
}

/**
 * Multi-zoom 3D spherical slippy-tile overlay engine served via same-origin cached `/api/tiles/:layer/:z/:x/:y`.
 * Supports dark-styled Chinese Retina detail maps (省/市/区县/路网), global Esri dark maps, and satellite imagery.
 */
export class SphericalTileLayer {
  readonly group = new THREE.Group();
  private readonly baseRadius: number;
  private readonly maxAnisotropy: number;
  private readonly cache = new Map<string, TileEntry>();
  private style: DetailMapStyle = "zh";
  private disposed = false;
  private lastCamPos = new THREE.Vector3(999, 999, 999);
  private lastTick = 0;
  private activeKeys = new Set<string>();
  private activeParentKeys = new Set<string>();
  private currentZoom = 3;
  private globalAlpha = 0;

  constructor(baseRadius: number, maxAnisotropy: number) {
    this.baseRadius = baseRadius;
    this.maxAnisotropy = Math.max(4, maxAnisotropy);
  }

  setStyle(next: DetailMapStyle) {
    if (this.style === next) return;
    this.style = next;
    this.group.visible = next !== "off";
    this.activeKeys.clear();
    this.activeParentKeys.clear();
    this.lastCamPos.set(999, 999, 999);
    this.lastTick = 0;
  }

  update(camera: THREE.PerspectiveCamera, nowMs: number): ZoomTelemetry {
    const camDist = camera.position.length();
    const altitude = Math.max(0.005, camDist - this.baseRadius);
    const desiredZ = Math.min(
      MAX_TILE_ZOOM,
      Math.max(MIN_TILE_ZOOM, Math.round(Math.log2(1.42 / altitude) + 3.15)),
    );

    const enabled = this.style !== "off";
    const targetAlpha = enabled ? THREE.MathUtils.clamp((2.22 - camDist) / 0.38, 0, 0.96) : 0;
    this.globalAlpha = targetAlpha;
    this.group.visible = enabled && targetAlpha > 0.01;

    if (!this.group.visible) {
      this.currentZoom = 3;
      return { zoom: 3, scaleLabel: "全球视野", tilesActive: false };
    }

    const moved = camera.position.distanceToSquared(this.lastCamPos) > 0.000018;
    if (moved && nowMs - this.lastTick > 90) {
      this.lastCamPos.copy(camera.position);
      this.lastTick = nowMs;
      this.currentZoom = desiredZ;
      this.refreshVisibleTiles(camera, desiredZ, altitude, nowMs);
    }

    for (const entry of this.cache.values()) {
      const isPrimary = this.activeKeys.has(entry.key);
      const isParent = this.activeParentKeys.has(entry.key);
      if (isPrimary && entry.loaded) {
        entry.fade = Math.min(1, entry.fade + 0.15);
        entry.mesh.visible = true;
        entry.mat.opacity = entry.fade * this.globalAlpha;
        entry.mesh.renderOrder = 3;
      } else if (isParent && entry.loaded) {
        entry.mesh.visible = true;
        entry.mat.opacity = this.globalAlpha * 0.92;
        entry.mesh.renderOrder = 2;
      } else {
        entry.mesh.visible = false;
      }
    }

    return {
      zoom: this.currentZoom,
      scaleLabel: zoomLabelFor(this.currentZoom, true),
      tilesActive: true,
    };
  }

  private refreshVisibleTiles(camera: THREE.PerspectiveCamera, z: number, altitude: number, nowMs: number) {
    if (this.style === "off") return;
    const style = this.style;
    const { lat, lon } = vec3ToLatLon(camera.position);
    const n = Math.pow(2, z);
    const { fx, fy } = latLonToTileFloat(lat, lon, z);
    const cx = Math.floor(fx);
    const cy = Math.floor(fy);

    const fovRad = camera.fov * DEG2RAD;
    const halfSpanRad = Math.min(0.62, Math.atan(Math.tan(fovRad * 0.5) * altitude) * 1.12);
    const latCos = Math.max(0.25, Math.cos(lat * DEG2RAD));
    const tilesLat = Math.min(3, Math.max(2, Math.ceil((halfSpanRad / Math.PI) * n) + 1));
    const tilesLon = Math.min(
      4,
      Math.max(2, Math.ceil(((halfSpanRad * camera.aspect) / (2 * Math.PI * latCos)) * n) + 1),
    );

    const nextActive = new Set<string>();
    const nextParents = new Set<string>();

    for (let dy = -tilesLat; dy <= tilesLat; dy++) {
      const ty = cy + dy;
      if (ty < 0 || ty >= n) continue;
      for (let dx = -tilesLon; dx <= tilesLon; dx++) {
        const tx = ((cx + dx) % n + n) % n;
        const key = `${style}:${z}/${tx}/${ty}`;
        nextActive.add(key);
        const entry = this.ensureTile(style, z, tx, ty, nowMs);

        if (!entry.loaded && z > MIN_TILE_ZOOM) {
          const pz = z - 1;
          const px = Math.floor(tx / 2);
          const py = Math.floor(ty / 2);
          const pKey = `${style}:${pz}/${px}/${py}`;
          nextParents.add(pKey);
          const pEntry = this.cache.get(pKey);
          if (pEntry) {
            pEntry.lastUsed = nowMs;
          } else {
            this.ensureTile(style, pz, px, py, nowMs);
          }
        }
      }
    }

    this.activeKeys = nextActive;
    this.activeParentKeys = nextParents;
    this.evictIfNeeded();
  }

  private ensureTile(style: ActiveStyle, z: number, x: number, y: number, nowMs: number): TileEntry {
    const key = `${style}:${z}/${x}/${y}`;
    const existing = this.cache.get(key);
    if (existing) {
      existing.lastUsed = nowMs;
      return existing;
    }

    const layerRadius = this.baseRadius + 0.001 + (z - MIN_TILE_ZOOM) * 0.00004;
    const geo = buildSphericalTileGeometry(z, x, y, layerRadius);
    const mat = new THREE.MeshBasicMaterial({
      transparent: true,
      opacity: 0,
      depthWrite: false,
      polygonOffset: true,
      polygonOffsetFactor: -z,
      polygonOffsetUnits: -1,
    });
    const mesh = new THREE.Mesh(geo, mat);
    mesh.visible = false;
    this.group.add(mesh);

    const entry: TileEntry = {
      key,
      style,
      z,
      x,
      y,
      mesh,
      mat,
      loaded: false,
      fade: 0,
      lastUsed: nowMs,
    };
    this.cache.set(key, entry);

    const commitCanvas = (canvas: HTMLCanvasElement) => {
      if (this.disposed || !this.cache.has(key)) return;
      const tex = new THREE.CanvasTexture(canvas);
      tex.colorSpace = THREE.SRGBColorSpace;
      tex.anisotropy = this.maxAnisotropy;
      tex.minFilter = THREE.LinearMipmapLinearFilter;
      tex.magFilter = THREE.LinearFilter;
      tex.wrapS = THREE.ClampToEdgeWrapping;
      tex.wrapT = THREE.ClampToEdgeWrapping;
      tex.generateMipmaps = true;
      tex.needsUpdate = true;
      const prev = entry.mat.map;
      entry.mat.map = tex;
      entry.mat.needsUpdate = true;
      prev?.dispose();
      entry.loaded = true;
    };

    if (style === "zh") {
      // Same-origin cached Chinese Retina map (512x512) transformed into deep obsidian-navy tactical dark mode
      loadTileImage(`/api/tiles/zh/${z}/${x}/${y}`)
        .then((img) => {
          const size = Math.min(512, Math.max(256, img.naturalWidth || 512));
          const canvas = document.createElement("canvas");
          canvas.width = size;
          canvas.height = size;
          const ctx = canvas.getContext("2d");
          if (!ctx) return;
          ctx.filter = "invert(0.92) hue-rotate(196deg) saturate(1.22) brightness(0.76) contrast(1.16)";
          ctx.drawImage(img, 0, 0, size, size);
          ctx.filter = "none";
          commitCanvas(canvas);
        })
        .catch(() => undefined);
    } else {
      const baseLayer = style === "satellite" ? "sat" : "dark";
      const paintComposite = (baseImg: HTMLImageElement, placesImg?: HTMLImageElement) => {
        const size = 256;
        const canvas = document.createElement("canvas");
        canvas.width = size;
        canvas.height = size;
        const ctx = canvas.getContext("2d");
        if (!ctx) return;
        ctx.drawImage(baseImg, 0, 0, size, size);
        if (style === "dark") {
          ctx.save();
          ctx.globalCompositeOperation = "multiply";
          ctx.fillStyle = "#5b89c8";
          ctx.fillRect(0, 0, size, size);
          ctx.restore();
        } else {
          ctx.fillStyle = "rgba(4, 11, 26, 0.22)";
          ctx.fillRect(0, 0, size, size);
        }
        if (placesImg) {
          ctx.save();
          ctx.globalAlpha = 0.94;
          ctx.drawImage(placesImg, 0, 0, size, size);
          ctx.restore();
        }
        commitCanvas(canvas);
      };

      loadTileImage(`/api/tiles/${baseLayer}/${z}/${x}/${y}`)
        .then((baseImg) => {
          paintComposite(baseImg);
          return loadTileImage(`/api/tiles/places/${z}/${x}/${y}`)
            .then((placesImg) => paintComposite(baseImg, placesImg))
            .catch(() => undefined);
        })
        .catch(() => undefined);
    }

    return entry;
  }

  private evictIfNeeded() {
    if (this.cache.size <= MAX_CACHED_TILES) return;
    const entries = Array.from(this.cache.values())
      .filter((e) => !this.activeKeys.has(e.key) && !this.activeParentKeys.has(e.key))
      .sort((a, b) => a.lastUsed - b.lastUsed);
    const toRemove = this.cache.size - MAX_CACHED_TILES;
    for (let i = 0; i < Math.min(toRemove, entries.length); i++) {
      this.removeEntry(entries[i]);
    }
  }

  private removeEntry(entry: TileEntry) {
    this.cache.delete(entry.key);
    this.group.remove(entry.mesh);
    entry.mesh.geometry.dispose();
    entry.mat.map?.dispose();
    entry.mat.dispose();
  }

  dispose() {
    this.disposed = true;
    for (const entry of Array.from(this.cache.values())) {
      this.removeEntry(entry);
    }
    this.activeKeys.clear();
    this.activeParentKeys.clear();
  }
}
