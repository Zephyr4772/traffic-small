// Pan/zoom camera. World units are metres; zoom is screen pixels per metre.

export class Camera {
  constructor(canvas) {
    this.canvas = canvas;
    this.x = 0;
    this.y = 0;
    this.zoom = 0.3;
    this.minZoom = 0.05;
    this.maxZoom = 14;
    this.w = 1;
    this.h = 1;
    this.dpr = 1;
    this.onChange = () => {};
  }

  resize(w, h, dpr) {
    this.w = w;
    this.h = h;
    this.dpr = dpr;
  }

  worldToScreen(wx, wy) {
    return [(wx - this.x) * this.zoom + this.w / 2, (wy - this.y) * this.zoom + this.h / 2];
  }

  screenToWorld(sx, sy) {
    return [(sx - this.w / 2) / this.zoom + this.x, (sy - this.h / 2) / this.zoom + this.y];
  }

  // Canvas transform so drawing commands can use world coordinates.
  apply(ctx) {
    const z = this.zoom * this.dpr;
    ctx.setTransform(z, 0, 0, z, (this.w / 2 - this.x * this.zoom) * this.dpr, (this.h / 2 - this.y * this.zoom) * this.dpr);
  }

  fit(minX, minY, maxX, maxY, padding = { left: 290, right: 300, top: 80, bottom: 50 }) {
    const pad = this.w < 900 ? { left: 20, right: 20, top: 70, bottom: 20 } : padding;
    const availW = Math.max(100, this.w - pad.left - pad.right);
    const availH = Math.max(100, this.h - pad.top - pad.bottom);
    this.zoom = Math.min(availW / (maxX - minX), availH / (maxY - minY));
    this.zoom = Math.min(this.maxZoom, Math.max(this.minZoom, this.zoom));
    const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
    // Centre the city inside the free area between the panels.
    this.x = cx - ((pad.left - pad.right) / 2) / this.zoom;
    this.y = cy - ((pad.top - pad.bottom) / 2) / this.zoom;
    this.onChange();
  }

  zoomAt(sx, sy, factor) {
    const [wx, wy] = this.screenToWorld(sx, sy);
    this.zoom = Math.min(this.maxZoom, Math.max(this.minZoom, this.zoom * factor));
    this.x = wx - (sx - this.w / 2) / this.zoom;
    this.y = wy - (sy - this.h / 2) / this.zoom;
    this.onChange();
  }

  attach() {
    const c = this.canvas;
    let drag = null;
    c.addEventListener('pointerdown', e => {
      drag = { x: e.clientX, y: e.clientY, cx: this.x, cy: this.y };
      c.setPointerCapture(e.pointerId);
      c.classList.add('dragging');
    });
    c.addEventListener('pointermove', e => {
      if (!drag) return;
      this.x = drag.cx - (e.clientX - drag.x) / this.zoom;
      this.y = drag.cy - (e.clientY - drag.y) / this.zoom;
      this.onChange();
    });
    const end = () => { drag = null; c.classList.remove('dragging'); };
    c.addEventListener('pointerup', end);
    c.addEventListener('pointercancel', end);
    c.addEventListener('wheel', e => {
      e.preventDefault();
      const factor = Math.exp(-e.deltaY * (e.deltaMode === 1 ? 0.05 : 0.0015));
      this.zoomAt(e.clientX, e.clientY, factor);
    }, { passive: false });
  }
}
