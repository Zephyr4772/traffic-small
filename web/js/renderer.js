// Draws the city top-down on a 2D canvas.
// Geometry is turned into cached Path2D objects once per city, so redrawing on pan/zoom is cheap.

import { Control, RoadClass } from './city.js';
import { signalKey } from './live.js';
import { SIGNAL } from './palette.js';

const POI = {
  school:        { glyph: 'S', color: '#c99212', minZoom: 0.10, major: true },
  hospital:      { glyph: 'H', color: '#e5484d', minZoom: 0.08, major: true },
  fireStation:   { glyph: 'F', color: '#d24a3f', minZoom: 0.22, major: false },
  policeStation: { glyph: 'P', color: '#3f66a0', minZoom: 0.22, major: false },
  busDepot:      { glyph: 'B', color: '#6b8636', minZoom: 0.12, major: true },
  mall:          { glyph: 'M', color: '#9a5797', minZoom: 0.12, major: true },
  stadium:       { glyph: '★', color: '#7b6d4f', minZoom: 0.10, major: true },
};

const MARKING_ZOOM = 0.9;      // lane markings appear from this zoom (px per metre)
const SIGNAL_HEAD_ZOOM = 1.8;  // individual signal heads per approach from this zoom
const SIGNAL_DOT_ZOOM = 0.3;   // live coloured dots per approach from this zoom; a junction icon below it

// Colours of vehicles that are not cars; cars get one of CAR_COLORS by id.
const KIND_COLORS = ['#4f7cff', '#2f3640', '#e8c14a', '#eef0f2', '#d9822b', '#2f7fc1', '#ffb300', '#ff2d2d'];
const CAR_COLORS = ['#4f7cff', '#e5484d', '#f2f2ee', '#7b8794', '#2b2f36', '#3fa46a', '#c9a227', '#8e5bd6'];
const FLOW_COLORS = ['#d83a3a', '#f07a2c', '#e3b341', '#2fb36b'];   // jammed, slow, moderate, free

export class CityRenderer {
  constructor() {
    this.city = null;
    this.palette = null;
    this.highlight = null;
  }

  setPalette(p) { this.palette = p; }

  setCity(city) {
    this.city = city;
    this.highlight = null;
    const driveLeft = city.config.driveLeft !== false;

    // ---- land ----
    this.groundPath = new Path2D();
    this.zonePaths = {};
    for (const b of city.blocks) {
      if (b.zone === 'water') continue;
      addPoly(this.groundPath, b.poly);
      (this.zonePaths[b.zone] ??= new Path2D());
      addPoly(this.zonePaths[b.zone], b.poly);
    }

    this.riverPath = null;
    if (city.river.path.length > 1) {
      this.riverPath = new Path2D();
      city.river.path.forEach((p, i) => (i ? this.riverPath.lineTo(p.x, p.y) : this.riverPath.moveTo(p.x, p.y)));
    }

    this.featurePaths = {};
    this.overPaths = {};
    this.treePath = new Path2D();
    let treeIndex = 0;
    for (const f of city.features) {
      if (f.type === 'trees') {
        for (const p of f.poly) {
          const r = 2.6 + ((treeIndex++ * 2654435761) % 100) / 100 * 1.8;
          this.treePath.moveTo(p.x + r, p.y);
          this.treePath.arc(p.x, p.y, r, 0, Math.PI * 2);
        }
        continue;
      }
      const target = f.over ? this.overPaths : this.featurePaths;
      (target[f.type] ??= new Path2D());
      addPoly(target[f.type], f.poly);
    }

    // ---- roads ----
    this.roadPaths = { 1: new Path2D(), 2: new Path2D(), 3: new Path2D() };
    this.classHalfWidth = { 1: 3, 2: 3.75, 3: 8 };
    this.bridgePath = new Path2D();
    this.bridgeHalfWidth = 0;
    for (const e of city.edges) {
      const A = city.nodes[e.a], B = city.nodes[e.b];
      this.roadPaths[e.cls].moveTo(A.x, A.y);
      this.roadPaths[e.cls].lineTo(B.x, B.y);
      this.classHalfWidth[e.cls] = e.halfWidth;
      if (e.bridge) {
        this.bridgePath.moveTo(A.x, A.y);
        this.bridgePath.lineTo(B.x, B.y);
        this.bridgeHalfWidth = Math.max(this.bridgeHalfWidth, e.halfWidth);
      }
    }

    // Size of each junction box: the widest road meeting there.
    const junctionRadius = city.nodes.map(n => {
      if (n.edges.length < 3) return 0;
      return Math.max(...n.edges.map(id => city.edges[id].halfWidth)) + 1.5;
    });

    // ---- markings, stop lines, zebra crossings, signal heads ----
    this.medianPath = new Path2D();
    this.lanePath = new Path2D();
    this.stopPath = new Path2D();
    this.zebraPath = new Path2D();
    this.signalHeads = [];
    this.stopBars = [];
    this.medianWidth = 2;

    // node.stop comes from the engine (City::stopDistance), so vehicles stop exactly on these lines.
    const approachOffset = (node) => {
      if (node.control === Control.Signal) return { zebra: node.stop - 2.4, stop: node.stop, start: node.stop + 0.6 };
      if (node.control === Control.AllWayStop) return { zebra: null, stop: node.stop, start: node.stop + 0.6 };
      return { zebra: null, stop: null, start: node.edges.length >= 3 ? node.stop : 0 };
    };

    for (const e of city.edges) {
      const A = city.nodes[e.a], B = city.nodes[e.b];
      const dx = (B.x - A.x) / e.length, dy = (B.y - A.y) / e.length;
      const Lx = dy, Ly = -dx;   // left of travel a -> b (screen coordinates)
      const offA = approachOffset(A), offB = approachOffset(B);
      const s0 = offA.start, s1 = e.length - offB.start;
      const at = (s, lat) => [A.x + dx * s + Lx * lat, A.y + dy * s + Ly * lat];
      const line = (path, sA, sB, lat) => {
        const [x0, y0] = at(sA, lat), [x1, y1] = at(sB, lat);
        path.moveTo(x0, y0);
        path.lineTo(x1, y1);
      };

      if (s1 - s0 > 4) {
        if (e.median > 0) {
          this.medianWidth = e.median;
          line(this.medianPath, s0, s1, 0);
        } else {
          line(this.lanePath, s0, s1, 0);   // dashed centre line on undivided roads
        }
        for (let k = 1; k < e.lanes; k++) {
          const lat = e.median / 2 + k * e.laneWidth;
          line(this.lanePath, s0, s1, lat);
          line(this.lanePath, s0, s1, -lat);
        }
      }

      // Incoming lanes are on the driver's left in India (right elsewhere).
      // Arriving at B we travel a->b, so incoming is +left; arriving at A we travel b->a, so incoming is -left.
      for (const [node, off, sign, s] of [[B, offB, 1, (d) => e.length - d], [A, offA, -1, (d) => d]]) {
        const side = driveLeft ? sign : -sign;
        if (off.stop !== null) {
          // The stop line only spans the incoming half of the road.
          const [x0, y0] = at(s(off.stop), side * (e.median / 2 + 0.2));
          const [x1, y1] = at(s(off.stop), side * (e.halfWidth - 0.2));
          this.stopPath.moveTo(x0, y0);
          this.stopPath.lineTo(x1, y1);
          if (node.control === Control.Signal) this.stopBars.push({ node: node.id, edge: e.id, x0, y0, x1, y1 });
        }
        if (off.zebra !== null) {
          const [x0, y0] = at(s(off.zebra), -e.halfWidth + 0.3);
          const [x1, y1] = at(s(off.zebra), e.halfWidth - 0.3);
          this.zebraPath.moveTo(x0, y0);
          this.zebraPath.lineTo(x1, y1);
        }
        if (node.control === Control.Signal) {
          const [hx, hy] = at(s(off.stop), side * (e.halfWidth + 1.4));
          this.signalHeads.push({ x: hx, y: hy, node: node.id, edge: e.id });
        }
      }
    }

    this.signalNodes = city.nodes.filter(n => n.control === Control.Signal);
    this.stopNodes = city.nodes.filter(n => n.control === Control.AllWayStop);

    // ---- buildings ----
    this.buildingPaths = {};
    this.shadowPath = new Path2D();
    this.outlinePath = new Path2D();
    for (const b of city.buildings) {
      (this.buildingPaths[b.type] ??= new Path2D());
      addPoly(this.buildingPaths[b.type], b.poly);
      addPoly(this.outlinePath, b.poly);
      const h = Math.min(4.5, 0.6 + b.floors * 0.25);
      addPoly(this.shadowPath, b.poly.map(p => ({ x: p.x + h * 0.6, y: p.y + h })));
    }

    // ---- labels ----
    this.poiLabels = city.buildings.filter(b => b.name && POI[b.type]).map(b => ({ x: b.cx, y: b.cy, name: b.name, type: b.type }));
    this.roadLabels = [];
    const seen = new Map();
    for (const e of city.edges) {
      if (e.cls === RoadClass.Local || e.length < 110) continue;
      const k = seen.get(e.name) ?? 0;
      seen.set(e.name, k + 1);
      if (k % 3 !== 1) continue;
      const A = city.nodes[e.a], B = city.nodes[e.b];
      let angle = Math.atan2(B.y - A.y, B.x - A.x);
      if (angle > Math.PI / 2) angle -= Math.PI;
      if (angle < -Math.PI / 2) angle += Math.PI;
      this.roadLabels.push({ x: (A.x + B.x) / 2, y: (A.y + B.y) / 2, angle, name: e.name, length: e.length });
    }

    // ---- spatial index for hover ----
    this.cell = 60;
    this.grid = new Map();
    for (const b of city.buildings) {
      let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
      for (const p of b.poly) { minX = Math.min(minX, p.x); minY = Math.min(minY, p.y); maxX = Math.max(maxX, p.x); maxY = Math.max(maxY, p.y); }
      for (let gx = Math.floor(minX / this.cell); gx <= Math.floor(maxX / this.cell); gx++)
        for (let gy = Math.floor(minY / this.cell); gy <= Math.floor(maxY / this.cell); gy++) {
          const key = gx + ',' + gy;
          if (!this.grid.has(key)) this.grid.set(key, []);
          this.grid.get(key).push(b.id);
        }
    }
    this.junctionRadius = junctionRadius;
  }

  // ---- static layer: the city itself, cached until the camera or layers change ----
  drawStatic(ctx, cam, layers) {
    const P = this.palette;
    const dpr = cam.dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = P.outside;
    ctx.fillRect(0, 0, cam.w, cam.h);
    if (!this.city) return;
    const z = cam.zoom;
    const px = 1 / z;   // one screen pixel in metres

    cam.apply(ctx);
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.fillStyle = P.land;
    ctx.fillRect(0, 0, this.city.width, this.city.height);

    // Land and land use
    ctx.fillStyle = P.ground;
    ctx.fill(this.groundPath);
    if (layers.zones) {
      for (const [zone, path] of Object.entries(this.zonePaths)) {
        ctx.fillStyle = P.zone[zone];
        ctx.fill(path);
      }
    }

    // River
    if (this.riverPath) {
      const w = this.city.river.width;
      ctx.save();
      ctx.beginPath();
      ctx.rect(0, 0, this.city.width, this.city.height);   // keep the river inside the city
      ctx.clip();
      ctx.strokeStyle = P.waterEdge; ctx.lineWidth = w + 7; ctx.stroke(this.riverPath);
      ctx.strokeStyle = P.water; ctx.lineWidth = w; ctx.stroke(this.riverPath);
      ctx.restore();
    }

    // Parks, fields, parking
    for (const type of ['park', 'plaza', 'parking', 'sportsField']) {
      if (!this.featurePaths[type]) continue;
      ctx.fillStyle = P.feature[type];
      ctx.fill(this.featurePaths[type]);
    }
    if (z > 0.8 && this.featurePaths.sportsField) {
      ctx.strokeStyle = P.fieldLine; ctx.lineWidth = Math.max(0.3, px);
      ctx.stroke(this.featurePaths.sportsField);
    }

    // Roads: bridge decks, then kerbs (casing), then asphalt
    if (this.bridgeHalfWidth > 0) {
      ctx.strokeStyle = P.bridge; ctx.lineWidth = this.bridgeHalfWidth * 2 + 7;
      ctx.lineCap = 'butt';
      ctx.stroke(this.bridgePath);
      ctx.lineCap = 'round';
    }
    for (const cls of [1, 2, 3]) {
      ctx.strokeStyle = P.roadCasing;
      ctx.lineWidth = this.classHalfWidth[cls] * 2 + Math.max(1.2, 1.5 * px);
      ctx.stroke(this.roadPaths[cls]);
    }
    for (const cls of [1, 2, 3]) {
      ctx.strokeStyle = P.road[cls];
      ctx.lineWidth = this.classHalfWidth[cls] * 2;
      ctx.stroke(this.roadPaths[cls]);
    }

    // Lane markings
    if (layers.markings && z >= MARKING_ZOOM) {
      ctx.lineCap = 'butt';
      ctx.strokeStyle = P.median; ctx.lineWidth = this.medianWidth;
      ctx.stroke(this.medianPath);
      ctx.strokeStyle = P.marking; ctx.lineWidth = Math.max(0.15, px);
      ctx.setLineDash([3, 5]);
      ctx.stroke(this.lanePath);
      ctx.setLineDash([]);
      ctx.lineWidth = Math.max(0.45, px * 1.5);
      ctx.stroke(this.stopPath);
      ctx.strokeStyle = P.zebra; ctx.lineWidth = 3;
      ctx.setLineDash([0.55, 0.55]);
      ctx.stroke(this.zebraPath);
      ctx.setLineDash([]);
      ctx.lineCap = 'round';
    }

    // Trees
    if (z > 0.5) {
      ctx.fillStyle = P.tree;
      ctx.fill(this.treePath);
    }

    // Buildings with a soft shadow for height
    if (layers.buildings) {
      if (z > 0.3) {
        ctx.fillStyle = P.shadow;
        ctx.fill(this.shadowPath);
      }
      for (const [type, path] of Object.entries(this.buildingPaths)) {
        ctx.fillStyle = P.building[type];
        ctx.fill(path);
      }
      if (z > 0.55) {
        ctx.strokeStyle = P.outline; ctx.lineWidth = px;
        ctx.stroke(this.outlinePath);
      }
      for (const [type, path] of Object.entries(this.overPaths)) {
        ctx.fillStyle = P.feature[type];
        ctx.fill(path);
        if (z > 0.8) { ctx.strokeStyle = P.fieldLine; ctx.lineWidth = Math.max(0.3, px); ctx.stroke(path); }
      }
    }

    // ---- screen-space overlays ----
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    if (layers.control) this.drawControlGlyphs(ctx, cam);
    if (layers.labels) this.drawLabels(ctx, cam);
  }

  // ---- live layer: drawn every animation frame on top of the cached city ----

  // Congestion overlay from the engine's flow message.
  setFlow(flow) {
    this.flowPaths = FLOW_COLORS.map(() => new Path2D());
    if (!this.city || !flow) return;
    const side = this.city.config.driveLeft !== false ? 1 : -1;
    for (const f of flow.links) {
      const e = this.city.edges[f.edge];
      if (!e) continue;
      const A = this.city.nodes[f.reverse ? e.b : e.a], B = this.city.nodes[f.reverse ? e.a : e.b];
      const dx = (B.x - A.x) / e.length, dy = (B.y - A.y) / e.length;
      const off = side * (e.median / 2 + e.lanes * e.laneWidth / 2);
      const s0 = A.stop ?? 0, s1 = e.length - (B.stop ?? 0);
      if (s1 <= s0) continue;
      const bucket = f.ratio < 0.2 ? 0 : f.ratio < 0.4 ? 1 : f.ratio < 0.65 ? 2 : 3;
      const path = this.flowPaths[bucket];
      path.moveTo(A.x + dx * s0 + dy * off, A.y + dy * s0 - dx * off);
      path.lineTo(A.x + dx * s1 + dy * off, A.y + dy * s1 - dx * off);
      this.flowLaneWidth = e.laneWidth;
    }
  }

  drawLive(ctx, cam, layers, live, now) {
    if (!this.city) return;
    const z = cam.zoom, px = 1 / z;
    cam.apply(ctx);

    // Congestion: strongest when zoomed out, where individual vehicles are too small to read.
    if (layers.flow && this.flowPaths && z < 1.1) {   // at street level the vehicles themselves show the queues
      ctx.lineCap = 'butt';
      ctx.globalAlpha = z < 0.6 ? 0.95 : 0.55;
      ctx.lineWidth = Math.max(z < 0.6 ? 3 * px : 1.5 * px, 2.5);
      FLOW_COLORS.forEach((color, i) => {
        ctx.strokeStyle = color;
        ctx.stroke(this.flowPaths[i]);
      });
      ctx.globalAlpha = 1;
    }

    if (layers.vehicles && live.cur) this.drawVehicles(ctx, cam, live, now);
    if (layers.control) this.drawSignals(ctx, z, live.cur?.signals);
    if (live.tracked?.route?.length >= 4) this.drawRoute(ctx, live.tracked.route, px);
    this.drawHighlight(ctx, px);
  }

  drawSignals(ctx, z, signals) {
    if (z < SIGNAL_DOT_ZOOM) return;
    const color = s => s === 2 ? SIGNAL.green : s === 1 ? SIGNAL.amber : s === 0 ? SIGNAL.red : SIGNAL.off;
    // A thick coloured bar across each approach's stop line: easy to read at any zoom.
    ctx.lineCap = 'round';
    const w = Math.max(1.4, 5 / z);
    for (const b of this.stopBars) {
      const c = color(signals?.get(signalKey(b.node, b.edge)));
      ctx.strokeStyle = 'rgba(0,0,0,0.55)';
      ctx.lineWidth = w + 2.4 / z;
      ctx.beginPath(); ctx.moveTo(b.x0, b.y0); ctx.lineTo(b.x1, b.y1); ctx.stroke();
      ctx.strokeStyle = c;
      ctx.lineWidth = w;
      ctx.stroke();
    }
    if (z < SIGNAL_HEAD_ZOOM) return;
    for (const h of this.signalHeads) {
      const s = signals?.get(signalKey(h.node, h.edge));
      ctx.fillStyle = SIGNAL.body;
      roundRect(ctx, h.x - 0.8, h.y - 1.9, 1.6, 3.8, 0.5);
      ctx.fill();
      [[0, SIGNAL.red], [1, SIGNAL.amber], [2, SIGNAL.green]].forEach(([state, c], i) => {
        ctx.beginPath();
        ctx.arc(h.x, h.y - 1.15 + i * 1.15, 0.45, 0, Math.PI * 2);
        ctx.fillStyle = s === state ? c : SIGNAL.off;
        ctx.fill();
      });
    }
  }

  // Vehicles, interpolated between the last two frames from the engine.
  drawVehicles(ctx, cam, live, now) {
    const { prev, cur, prevIndex, kinds } = live;
    const z = cam.zoom;
    const interval = prev ? Math.min(250, Math.max(30, cur.received - prev.received)) : 66;
    const t = prev ? Math.min(1, Math.max(0, (now - cur.received) / interval)) : 1;
    const minSize = 6 / z;                               // never smaller than a few pixels
    const boost = Math.min(3, Math.max(1, 2.2 / z));     // draw vehicles larger than life when zoomed out
    const flash = Math.floor(now / 250) % 2 === 0;
    const [vx0, vy0] = cam.screenToWorld(-20, -20), [vx1, vy1] = cam.screenToWorld(cam.w + 20, cam.h + 20);

    const bodies = new Map();
    const brakes = new Path2D();
    const positions = live.positions;
    positions.length = 0;
    for (let i = 0; i < cur.n; i++) {
      let x = cur.x[i], y = cur.y[i], a = cur.angle[i];
      const j = prevIndex?.get(cur.ids[i]);
      if (j !== undefined) {
        x = prev.x[j] + (x - prev.x[j]) * t;
        y = prev.y[j] + (y - prev.y[j]) * t;
        let d = a - prev.angle[j];
        if (d > Math.PI) d -= 2 * Math.PI;
        if (d < -Math.PI) d += 2 * Math.PI;
        a = prev.angle[j] + d * t;
      }
      positions.push(cur.ids[i], x, y);
      if (x < vx0 || x > vx1 || y < vy0 || y > vy1) continue;

      const k = cur.kind[i];
      const spec = kinds[k] ?? { length: 4.4, width: 1.8 };
      const L = Math.max(spec.length * boost, minSize), W = Math.max(spec.width * boost, minSize * 0.6);
      const color = k === 7 ? (flash ? '#ff2d2d' : '#2d6bff')
        : k === 0 ? CAR_COLORS[cur.ids[i] % CAR_COLORS.length] : KIND_COLORS[k];
      if (!bodies.has(color)) bodies.set(color, new Path2D());
      const c = Math.cos(a), s = Math.sin(a);
      const hl = L / 2, hw = W / 2;
      const path = bodies.get(color);
      path.moveTo(x + c * hl - s * hw, y + s * hl + c * hw);
      path.lineTo(x + c * hl + s * hw, y + s * hl - c * hw);
      path.lineTo(x - c * hl + s * hw, y - s * hl - c * hw);
      path.lineTo(x - c * hl - s * hw, y - s * hl + c * hw);
      path.closePath();
      if ((cur.flags[i] & 1) && z >= 2.2) {   // brake lights
        const bx = x - c * (hl - 0.15), by = y - s * (hl - 0.15);
        brakes.moveTo(bx - s * hw * 0.7 + 0.25, by + c * hw * 0.7);
        brakes.arc(bx - s * hw * 0.7, by + c * hw * 0.7, 0.25, 0, Math.PI * 2);
        brakes.moveTo(bx + s * hw * 0.7 + 0.25, by - c * hw * 0.7);
        brakes.arc(bx + s * hw * 0.7, by - c * hw * 0.7, 0.25, 0, Math.PI * 2);
      }
      if (cur.ids[i] === live.trackedId) live.trackedPos = [x, y];
    }

    ctx.lineJoin = 'round';
    for (const [color, path] of bodies) {
      ctx.fillStyle = color;
      ctx.fill(path);
      ctx.strokeStyle = 'rgba(0,0,0,0.6)';   // dark outline so light vehicles stand out from the road
      ctx.lineWidth = 1 / z;
      ctx.stroke(path);
    }
    ctx.fillStyle = '#ff3030';
    ctx.fill(brakes);

    if (live.trackedPos) {
      const [x, y] = live.trackedPos;
      ctx.strokeStyle = '#2563eb';
      ctx.lineWidth = 2.5 / z;
      ctx.beginPath();
      ctx.arc(x, y, Math.max(5, 9 / z), 0, Math.PI * 2);
      ctx.stroke();
    }
  }

  drawRoute(ctx, route, px) {
    ctx.save();
    ctx.strokeStyle = '#2563eb';
    ctx.globalAlpha = 0.75;
    ctx.lineWidth = Math.max(1.2, 3 * px);
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.setLineDash([6 * px, 5 * px]);
    ctx.beginPath();
    ctx.moveTo(route[0], route[1]);
    for (let i = 2; i + 1 < route.length; i += 2) ctx.lineTo(route[i], route[i + 1]);
    ctx.stroke();
    ctx.setLineDash([]);
    const n = route.length;
    ctx.globalAlpha = 1;
    ctx.fillStyle = '#2563eb';
    ctx.beginPath();
    ctx.arc(route[n - 2], route[n - 1], Math.max(3, 6 * px), 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  // Nearest vehicle to a screen point, from the positions drawn last frame.
  vehicleAt(cam, live, sx, sy) {
    const [wx, wy] = cam.screenToWorld(sx, sy);
    const max = Math.max(4, 10 / cam.zoom);
    let best = null, bestD = max;
    const p = live.positions;
    for (let i = 0; i < p.length; i += 3) {
      const d = Math.hypot(p[i + 1] - wx, p[i + 2] - wy);
      if (d < bestD) { bestD = d; best = p[i]; }
    }
    return best;
  }

  drawControlGlyphs(ctx, cam) {
    const z = cam.zoom;
    if (z >= 0.12 && z < SIGNAL_DOT_ZOOM) {   // closer in, live dots per approach take over
      const s = Math.min(1.2, 0.75 + z * 0.4);
      for (const n of this.signalNodes) {
        const [x, y] = cam.worldToScreen(n.x, n.y);
        if (x < -20 || y < -20 || x > cam.w + 20 || y > cam.h + 20) continue;
        ctx.fillStyle = SIGNAL.body;
        roundRect(ctx, x - 4.5 * s, y - 10 * s, 9 * s, 20 * s, 3 * s);
        ctx.fill();
        [SIGNAL.red, SIGNAL.amber, SIGNAL.green].forEach((c, i) => {
          ctx.beginPath();
          ctx.arc(x, y - 6 * s + i * 6 * s, 2.2 * s, 0, Math.PI * 2);
          ctx.fillStyle = c;
          ctx.fill();
        });
      }
    }
    if (z >= 0.3) {
      for (const n of this.stopNodes) {
        const [x, y] = cam.worldToScreen(n.x, n.y);
        if (x < -20 || y < -20 || x > cam.w + 20 || y > cam.h + 20) continue;
        octagon(ctx, x, y, 6.5);
        ctx.fillStyle = '#ffffff';
        ctx.fill();
        octagon(ctx, x, y, 5.2);
        ctx.fillStyle = '#d93636';
        ctx.fill();
      }
    }
  }

  drawLabels(ctx, cam) {
    const P = this.palette;
    const z = cam.zoom;
    const placed = [];
    const free = (x, y, w, h) => {
      for (const r of placed) if (x < r.x + r.w && x + w > r.x && y < r.y + r.h && y + h > r.y) return false;
      placed.push({ x, y, w, h });
      return true;
    };

    // Points of interest first (they matter most).
    ctx.textBaseline = 'middle';
    for (const l of this.poiLabels) {
      const poi = POI[l.type];
      if (z < poi.minZoom) continue;
      const [x, y] = cam.worldToScreen(l.x, l.y);
      if (x < -50 || y < -20 || x > cam.w + 50 || y > cam.h + 20) continue;
      const showName = z >= (poi.major ? 0.38 : 0.6);
      ctx.font = '600 12px system-ui, sans-serif';
      const tw = showName ? ctx.measureText(l.name).width + 8 : 0;
      if (!free(x - 10, y - 10, 20 + tw, 20)) continue;
      ctx.beginPath();
      ctx.arc(x, y, 9, 0, Math.PI * 2);
      ctx.fillStyle = '#ffffff';
      ctx.fill();
      ctx.beginPath();
      ctx.arc(x, y, 7.5, 0, Math.PI * 2);
      ctx.fillStyle = poi.color;
      ctx.fill();
      ctx.fillStyle = '#ffffff';
      ctx.font = '700 10px system-ui, sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(poi.glyph, x, y + 0.5);
      if (showName) {
        ctx.font = '600 12px system-ui, sans-serif';
        ctx.textAlign = 'left';
        ctx.lineWidth = 3.5;
        ctx.strokeStyle = P.labelHalo;
        ctx.strokeText(l.name, x + 13, y);
        ctx.fillStyle = P.label;
        ctx.fillText(l.name, x + 13, y);
      }
    }

    // Road names along arterials and collectors.
    if (z >= 0.75) {
      ctx.font = '600 11px system-ui, sans-serif';
      ctx.textAlign = 'center';
      for (const r of this.roadLabels) {
        const tw = ctx.measureText(r.name).width;
        if (r.length * z < tw + 30) continue;
        const [x, y] = cam.worldToScreen(r.x, r.y);
        if (x < -100 || y < -20 || x > cam.w + 100 || y > cam.h + 20) continue;
        if (!free(x - tw / 2, y - 8, tw, 16)) continue;
        ctx.save();
        ctx.translate(x, y);
        ctx.rotate(r.angle);
        ctx.lineWidth = 3;
        ctx.strokeStyle = P.roadLabelHalo;
        ctx.strokeText(r.name, 0, 0);
        ctx.fillStyle = P.roadLabel;
        ctx.fillText(r.name, 0, 0);
        ctx.restore();
      }
    }

    // District names when zoomed out.
    if (z < 0.55) {
      ctx.font = '700 13px system-ui, sans-serif';
      ctx.textAlign = 'center';
      if ('letterSpacing' in ctx) ctx.letterSpacing = '2px';
      for (const d of this.city.districts) {
        const [x, y] = cam.worldToScreen(d.x, d.y);
        const text = d.name.toUpperCase();
        const tw = ctx.measureText(text).width;
        if (!free(x - tw / 2, y - 9, tw, 18)) continue;
        ctx.lineWidth = 4;
        ctx.strokeStyle = P.labelHalo;
        ctx.strokeText(text, x, y);
        ctx.fillStyle = P.district;
        ctx.fillText(text, x, y);
      }
      if ('letterSpacing' in ctx) ctx.letterSpacing = '0px';
    }
  }

  drawHighlight(ctx, px) {
    const h = this.highlight;
    if (!h) return;
    ctx.strokeStyle = '#2563eb';
    ctx.lineWidth = 2.5 * px;
    if (h.kind === 'building') {
      const path = new Path2D();
      addPoly(path, h.building.poly);
      ctx.stroke(path);
    } else if (h.kind === 'junction') {
      ctx.beginPath();
      ctx.arc(h.node.x, h.node.y, Math.max(this.junctionRadius[h.node.id], 6) + 3, 0, Math.PI * 2);
      ctx.stroke();
    } else if (h.kind === 'road') {
      const A = this.city.nodes[h.edge.a], B = this.city.nodes[h.edge.b];
      ctx.globalAlpha = 0.35;
      ctx.lineWidth = h.edge.halfWidth * 2 + 2;
      ctx.beginPath();
      ctx.moveTo(A.x, A.y);
      ctx.lineTo(B.x, B.y);
      ctx.stroke();
      ctx.globalAlpha = 1;
    }
  }

  // What is under the mouse? Junctions win over buildings, buildings over roads.
  hitTest(cam, sx, sy) {
    if (!this.city) return null;
    const [wx, wy] = cam.screenToWorld(sx, sy);
    let best = null, bestD = Infinity;
    for (const n of this.city.nodes) {
      if (n.edges.length < 3) continue;
      const d = Math.hypot(n.x - wx, n.y - wy);
      const r = Math.max(this.junctionRadius[n.id], 10 / cam.zoom);
      if (d < r && d < bestD) { bestD = d; best = n; }
    }
    if (best) return { kind: 'junction', node: best };

    const key = Math.floor(wx / this.cell) + ',' + Math.floor(wy / this.cell);
    for (const id of this.grid.get(key) ?? []) {
      const b = this.city.buildings[id];
      if (pointInPoly(wx, wy, b.poly)) return { kind: 'building', building: b };
    }

    let edge = null, edgeD = Infinity;
    for (const e of this.city.edges) {
      const A = this.city.nodes[e.a], B = this.city.nodes[e.b];
      const d = distToSegment(wx, wy, A.x, A.y, B.x, B.y);
      if (d < e.halfWidth + 1 && d < edgeD) { edgeD = d; edge = e; }
    }
    return edge ? { kind: 'road', edge } : null;
  }
}

// ---- helpers ----

function addPoly(path, poly) {
  if (!poly.length) return;
  path.moveTo(poly[0].x, poly[0].y);
  for (let i = 1; i < poly.length; i++) path.lineTo(poly[i].x, poly[i].y);
  path.closePath();
}

function roundRect(ctx, x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}

function octagon(ctx, x, y, r) {
  ctx.beginPath();
  for (let i = 0; i < 8; i++) {
    const a = Math.PI / 8 + (i * Math.PI) / 4;
    const px = x + Math.cos(a) * r, py = y + Math.sin(a) * r;
    i ? ctx.lineTo(px, py) : ctx.moveTo(px, py);
  }
  ctx.closePath();
}

function pointInPoly(x, y, poly) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[i], b = poly[j];
    if ((a.y > y) !== (b.y > y) && x < ((b.x - a.x) * (y - a.y)) / (b.y - a.y) + a.x) inside = !inside;
  }
  return inside;
}

function distToSegment(px, py, ax, ay, bx, by) {
  const vx = bx - ax, vy = by - ay;
  const l2 = vx * vx + vy * vy;
  const t = l2 ? Math.max(0, Math.min(1, ((px - ax) * vx + (py - ay) * vy) / l2)) : 0;
  return Math.hypot(px - (ax + vx * t), py - (ay + vy * t));
}
