// Front-end entry point: asks the C++ engine for a city, draws it, and animates the live traffic.

import { Camera } from './camera.js';
import { decodeCity } from './city.js';
import { LiveClient } from './live.js';
import { getPalette } from './palette.js';
import { CityRenderer } from './renderer.js';
import { hideTip, renderLegend, renderLiveStats, renderStats, renderTracked, showTip, tooltipHtml } from './ui.js';

const $ = id => document.getElementById(id);

// Mirrors CityConfig::preset in engine/src/world/City.cpp.
const PRESETS = {
  demo:   { size: 0.7, block: 230, spacing: 3, irregular: 0, jitter: 0.08, river: false },
  small:  { size: 1.6, block: 150, spacing: 5 },
  medium: { size: 2.4, block: 160, spacing: 5 },
  large:  { size: 3.6, block: 170, spacing: 5 },
};
const DEFAULTS = { irregular: 0.10, jitter: 0.18, river: true };

const canvas = $('map');
const ctx = canvas.getContext('2d');
const staticCanvas = document.createElement('canvas');   // the city, redrawn only when the view changes
const staticCtx = staticCanvas.getContext('2d');
const camera = new Camera(canvas);
const renderer = new CityRenderer();
const layers = { vehicles: true, flow: false, zones: true, buildings: true, markings: true, control: true, labels: true };
let city = null;
let staticDirty = true;

// Live simulation state
const live = {
  prev: null, cur: null, prevIndex: null, kinds: [], positions: [],
  minVersion: 0, trackedId: 0, tracked: null, trackedPos: null,
  running: true, speed: 1, follow: true, volumeTouched: 0,
};

renderer.setPalette(getPalette());
renderLegend(getPalette());

camera.onChange = () => { staticDirty = true; };
camera.attach();

function resize() {
  const dpr = window.devicePixelRatio || 1;
  for (const c of [canvas, staticCanvas]) {
    c.width = Math.round(innerWidth * dpr);
    c.height = Math.round(innerHeight * dpr);
  }
  camera.resize(innerWidth, innerHeight, dpr);
  staticDirty = true;
}
addEventListener('resize', resize);
resize();

matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
  renderer.setPalette(getPalette());
  renderLegend(getPalette());
  staticDirty = true;
});

// ---- animation loop ----

function frame(now) {
  if (live.follow && live.trackedPos) {
    camera.x += (live.trackedPos[0] - camera.x) * 0.15;
    camera.y += (live.trackedPos[1] - camera.y) * 0.15;
    staticDirty = true;
  }
  if (staticDirty) {
    renderer.drawStatic(staticCtx, camera, layers);
    staticDirty = false;
  }
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.drawImage(staticCanvas, 0, 0);
  live.trackedPos = null;
  renderer.drawLive(ctx, camera, layers, live, now);
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

// ---- connection to the engine ----

const client = new LiveClient({
  connected() { setStatus('Connected to engine'); },
  disconnected() { setStatus('Engine not reachable. Is traffic.exe running?', true); },
  hello(m) {
    live.kinds = m.kinds;
    live.running = m.running;
    live.speed = m.speed;
    showControls();
  },
  frame(f) {
    if (f.cityVersion < live.minVersion) return;            // still the previous city
    if (live.cur && live.cur.cityVersion === f.cityVersion) {
      live.prev = live.cur;
      live.prevIndex = new Map();
      for (let i = 0; i < live.prev.n; i++) live.prevIndex.set(live.prev.ids[i], i);
    } else {
      live.prev = null;
      live.prevIndex = null;
    }
    live.cur = f;
    $('clock').textContent = clockText(f.clock);
  },
  stats(s) {
    if (s.cityVersion < live.minVersion) return;
    renderLiveStats(s);
    live.running = s.running;
    live.speed = s.speed;
    showControls();
    // Keep the volume slider in step with the engine, unless the user is dragging it.
    if (performance.now() - live.volumeTouched > 1500) {
      const v = $('volume');
      v.max = Math.max(4000, Math.round(s.defaultVolume * 4 / 100) * 100);
      v.value = s.volume;
      $('volumeOut').textContent = fmtVolume(s.volume);
    }
    live.tracked = s.tracked ?? null;
    if (live.trackedId && !s.tracked) untrack();   // the vehicle arrived or left
    renderTracked(live.tracked);
    // Tell the user if the engine can't keep up with the requested speed (ignore the first seconds).
    live.slowCount = s.running && s.simTime > 5 && s.effectiveSpeed < s.speed * 0.8 ? (live.slowCount ?? 0) + 1 : 0;
    if (live.slowCount >= 4) setStatus(`Engine running at ${s.effectiveSpeed.toFixed(1)}× (asked for ${s.speed}×)`);
  },
  flow(f) {
    if (f.cityVersion < live.minVersion) return;
    renderer.setFlow(f);
  },
});

const clockText = secs => {
  const s = Math.floor(secs) % 86400;
  return String(Math.floor(s / 3600)).padStart(2, '0') + ':' + String(Math.floor(s / 60) % 60).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
};
const fmtVolume = v => Number(v).toLocaleString('en-IN') + ' / h';

function showControls() {
  $('pauseIcon').style.display = live.running ? '' : 'none';   // SVG elements ignore the hidden property
  $('playIcon').style.display = live.running ? 'none' : '';
  $('playPause').title = live.running ? 'Pause (Space)' : 'Play (Space)';
  for (const b of $('speeds').children) b.classList.toggle('on', parseFloat(b.dataset.speed) === live.speed);
}

function togglePlay() {
  live.running = !live.running;
  client.send({ cmd: live.running ? 'play' : 'pause' });
  showControls();
}
$('playPause').addEventListener('click', togglePlay);
for (const b of $('speeds').children) {
  b.addEventListener('click', () => {
    live.speed = parseFloat(b.dataset.speed);
    client.send({ cmd: 'speed', value: live.speed });
    showControls();
  });
}
$('volume').addEventListener('input', () => {
  live.volumeTouched = performance.now();
  $('volumeOut').textContent = fmtVolume($('volume').value);
  client.send({ cmd: 'volume', value: parseFloat($('volume').value) });
});
$('controller').addEventListener('change', () => client.send({ cmd: 'controller', value: parseInt($('controller').value, 10) }));
$('resetTraffic').addEventListener('click', () => {
  live.minVersion = (live.cur?.cityVersion ?? 0) + 1;
  live.cur = live.prev = null;
  untrack();
  client.send({ cmd: 'reset' });
});

// ---- following a vehicle ----

function track(id) {
  live.trackedId = id;
  live.follow = $('followCam').checked;
  client.send({ cmd: 'track', value: id });
}
function untrack() {
  live.trackedId = 0;
  live.tracked = null;
  renderTracked(null);
  client.send({ cmd: 'track', value: 0 });
}
$('untrack').addEventListener('click', untrack);
$('followCam').addEventListener('change', () => { live.follow = $('followCam').checked; });

// A click (not a drag) on a vehicle follows it.
let down = null;
canvas.addEventListener('pointerdown', e => { down = { x: e.clientX, y: e.clientY }; });
canvas.addEventListener('pointerup', e => {
  if (!down) return;
  const moved = Math.hypot(e.clientX - down.x, e.clientY - down.y);
  down = null;
  if (moved > 4) { live.follow = false; $('followCam').checked = false; return; }   // dragging stops following
  const id = layers.vehicles ? renderer.vehicleAt(camera, live, e.clientX, e.clientY) : null;
  if (id) track(id);
});

// ---- city controls ----

const sliders = {
  size: v => v.toFixed(1) + ' km',
  block: v => v + ' m',
  spacing: v => v + ' blocks',
  irregular: v => Math.round(v * 100) + '%',
  jitter: v => Math.round(v * 100) + '%',
};
for (const [id, format] of Object.entries(sliders)) {
  const input = $(id);
  input.addEventListener('input', () => {
    $(id + 'Out').textContent = format(parseFloat(input.value));
    if (['size', 'block', 'spacing'].includes(id)) $('scale').value = 'custom';
  });
}

function setControls(p) {
  for (const [id, format] of Object.entries(sliders)) {
    if (p[id] === undefined) continue;
    $(id).value = p[id];
    $(id + 'Out').textContent = format(parseFloat(p[id]));
  }
  if (p.river !== undefined) $('river').checked = p.river;
}

$('scale').addEventListener('change', () => {
  const preset = PRESETS[$('scale').value];
  if (preset) setControls(preset);
});

function readParams() {
  return {
    seed: Math.max(1, parseInt($('seed').value, 10) || 1),
    scale: $('scale').value,
    size: Math.round(parseFloat($('size').value) * 1000),
    block: $('block').value,
    spacing: $('spacing').value,
    irregular: $('irregular').value,
    jitter: $('jitter').value,
    river: $('river').checked ? 1 : 0,
  };
}

const randomSeed = () => 1 + Math.floor(Math.random() * 999999);

$('dice').addEventListener('click', () => { $('seed').value = randomSeed(); generate(); });
$('generate').addEventListener('click', () => generate());
$('seed').addEventListener('keydown', e => { if (e.key === 'Enter') generate(); });

for (const box of document.querySelectorAll('[data-layer]')) {
  box.addEventListener('change', () => { layers[box.dataset.layer] = box.checked; staticDirty = true; });
}

addEventListener('keydown', e => {
  if (e.target.tagName === 'INPUT' && e.target.type !== 'checkbox' && e.target.type !== 'range') return;
  if (e.target.tagName === 'SELECT') return;
  if (e.key === 'f' || e.key === 'F') fitCity();
  if (e.key === 'r' || e.key === 'R') { $('seed').value = randomSeed(); generate(); }
  if (e.key === ' ') { e.preventDefault(); togglePlay(); }
  if (e.key === 'h' || e.key === 'H') document.body.classList.toggle('clean');
});

// ---- hover ----

canvas.addEventListener('pointermove', e => {
  if (e.buttons) { hideTip(); return; }
  const hit = renderer.hitTest(camera, e.clientX, e.clientY);
  renderer.highlight = hit;
  if (hit) showTip(tooltipHtml(hit, city), e.clientX, e.clientY);
  else hideTip();
});
canvas.addEventListener('pointerleave', () => { hideTip(); renderer.highlight = null; });

// ---- generation ----

function fitCity() {
  live.follow = false;
  if (city) camera.fit(0, 0, city.width, city.height);
}

function setStatus(text, isError = false) {
  const s = $('status');
  s.textContent = text;
  s.classList.toggle('error', isError);
}

async function generate() {
  const params = readParams();
  $('generate').disabled = true;
  setStatus('Generating city…');
  const t0 = performance.now();
  try {
    const res = await fetch('/api/city?' + new URLSearchParams(params));
    if (!res.ok) throw new Error(await res.text());
    const raw = await res.json();
    city = decodeCity(raw);
    hideTip();
    untrack();
    // Ignore frames from the old city still in flight.
    live.minVersion = raw.cityVersion ?? 0;
    live.cur = live.prev = null;
    renderer.setCity(city);
    renderer.setFlow(null);
    renderStats(city.stats);
    fitCity();
    staticDirty = true;
    const ms = Math.round(performance.now() - t0);
    setStatus(`Seed ${params.seed} · ${city.stats.signals} signals · ${city.stats.population.toLocaleString('en-IN')} residents · ${ms} ms`);
    history.replaceState(null, '', '?' + new URLSearchParams(params));
  } catch (err) {
    setStatus('Could not load the city. Is traffic.exe running? (' + err.message + ')', true);
  } finally {
    $('generate').disabled = false;
  }
}

// ---- start: restore settings from the address bar, so a city can be shared by link ----

const q = new URLSearchParams(location.search);
const scale = q.get('scale') ?? 'demo';
$('scale').value = PRESETS[scale] || scale === 'custom' ? scale : 'demo';
setControls({ ...DEFAULTS, ...(PRESETS[scale] ?? PRESETS.demo) });
if (q.has('size')) setControls({ size: parseFloat(q.get('size')) / 1000 });
for (const k of ['block', 'spacing', 'irregular', 'jitter']) if (q.has(k)) setControls({ [k]: parseFloat(q.get(k)) });
if (q.has('river')) setControls({ river: q.get('river') === '1' });
$('seed').value = q.get('seed') ?? randomSeed();
generate();
