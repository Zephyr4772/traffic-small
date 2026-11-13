// WebSocket client for the live simulation stream from the C++ engine.
//
// Messages from the engine:
//   text   {"type":"hello", kinds:[...]}                      once, on connect
//   text   {"type":"stats", ...}                              twice a second
//   binary type 1: vehicle frame + signal states              ~15 per second
//   binary type 2: congestion per link                        twice a second
// Binary layouts are written by Simulation::writeFrame / writeFlow (little-endian).

export class LiveClient {
  constructor(handlers) {
    this.handlers = handlers;
    this.ws = null;
    this.connect();
  }

  connect() {
    const ws = new WebSocket(`ws://${location.host}/ws`);
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => this.handlers.connected?.();
    ws.onclose = () => {
      this.handlers.disconnected?.();
      setTimeout(() => this.connect(), 1500);
    };
    ws.onmessage = e => {
      if (typeof e.data === 'string') {
        const msg = JSON.parse(e.data);
        this.handlers[msg.type]?.(msg);
        return;
      }
      const dv = new DataView(e.data);
      const type = dv.getUint8(0);
      if (type === 1) this.handlers.frame?.(decodeFrame(dv));
      else if (type === 2) this.handlers.flow?.(decodeFlow(dv));
    };
    this.ws = ws;
  }

  send(obj) {
    if (this.ws?.readyState === WebSocket.OPEN) this.ws.send(JSON.stringify(obj));
  }
}

export const signalKey = (node, edge) => node * 100000 + edge;

function decodeFrame(dv) {
  let o = 4;
  const cityVersion = dv.getUint32(o, true); o += 4;
  const clock = dv.getFloat64(o, true); o += 8;
  const n = dv.getUint32(o, true); o += 4;
  const ids = new Uint32Array(n), x = new Float32Array(n), y = new Float32Array(n), angle = new Float32Array(n);
  const kind = new Uint8Array(n), flags = new Uint8Array(n), speed = new Uint16Array(n);
  for (let i = 0; i < n; i++, o += 20) {
    ids[i] = dv.getUint32(o, true);
    x[i] = dv.getFloat32(o + 4, true);
    y[i] = dv.getFloat32(o + 8, true);
    angle[i] = dv.getFloat32(o + 12, true);
    kind[i] = dv.getUint8(o + 16);
    flags[i] = dv.getUint8(o + 17);
    speed[i] = dv.getUint16(o + 18, true);
  }
  const ns = dv.getUint32(o, true); o += 4;
  const signals = new Map();
  for (let i = 0; i < ns; i++, o += 9) signals.set(signalKey(dv.getUint32(o, true), dv.getUint32(o + 4, true)), dv.getUint8(o + 8));
  return { cityVersion, clock, n, ids, x, y, angle, kind, flags, speed, signals, received: performance.now() };
}

function decodeFlow(dv) {
  let o = 4;
  const cityVersion = dv.getUint32(o, true); o += 4;
  const n = dv.getUint32(o, true); o += 4;
  const links = [];
  for (let i = 0; i < n; i++, o += 6) {
    const value = dv.getUint8(o + 5);
    if (value !== 255) links.push({ edge: dv.getUint32(o, true), reverse: dv.getUint8(o + 4) === 1, ratio: value / 200 });
  }
  return { cityVersion, links };
}
