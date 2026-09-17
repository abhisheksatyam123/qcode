// Infinite Canvas Engine for QCode WebUI
// Supports infinite pan, zoom (0.1x to 10x), stylus/touch/mouse, vector shapes, undo/redo, and PNG export

export class InfiniteCanvas {
  constructor(canvasElement, options = {}) {
    this.canvas = canvasElement;
    this.ctx = canvasElement.getContext('2d');
    this.options = options;

    // Viewport transformation
    this.scale = 1.0;
    this.panX = 0;
    this.panY = 0;

    // Drawing state
    this.tool = 'pen'; // 'pen' | 'line' | 'arrow' | 'rect' | 'circle' | 'text' | 'eraser' | 'pan'
    this.color = '#38bdf8'; // Default sky-blue
    this.lineWidth = 3;
    this.elements = [];
    this.history = [];
    this.redoList = [];

    // Interaction tracking
    this.isInteracting = false;
    this.isPanning = false;
    this.lastPointer = { x: 0, y: 0 };
    this.currentElement = null;

    this._bindEvents();
    this.resize();
  }

  setTool(tool) {
    this.tool = tool;
  }

  setColor(color) {
    this.color = color;
  }

  setLineWidth(width) {
    this.lineWidth = width;
  }

  resize() {
    if (!this.canvas || !this.canvas.parentElement) return;
    const rect = this.canvas.parentElement.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;

    this.width = rect.width;
    this.height = rect.height;

    this.canvas.width = Math.max(100, Math.floor(rect.width * dpr));
    this.canvas.height = Math.max(100, Math.floor(rect.height * dpr));
    this.canvas.style.width = `${rect.width}px`;
    this.canvas.style.height = `${rect.height}px`;

    this.render();
  }

  screenToWorld(clientX, clientY) {
    const rect = this.canvas.getBoundingClientRect();
    const sx = clientX - rect.left;
    const sy = clientY - rect.top;
    return {
      x: (sx - this.panX) / this.scale,
      y: (sy - this.panY) / this.scale
    };
  }

  worldToScreen(worldX, worldY) {
    return {
      x: worldX * this.scale + this.panX,
      y: worldY * this.scale + this.panY
    };
  }

  _bindEvents() {
    this.canvas.addEventListener('mousedown', (e) => this._onPointerDown(e));
    window.addEventListener('mousemove', (e) => this._onPointerMove(e));
    window.addEventListener('mouseup', (e) => this._onPointerUp(e));

    this.canvas.addEventListener('wheel', (e) => this._onWheel(e), { passive: false });

    // Touch support for tablets and mobile
    this.canvas.addEventListener('touchstart', (e) => {
      e.preventDefault();
      if (e.touches.length === 1) {
        const t = e.touches[0];
        this._onPointerDown({ clientX: t.clientX, clientY: t.clientY, button: 0 });
      } else if (e.touches.length === 2) {
        // Two finger pinch/pan
        this.isPanning = true;
        this.lastTouchDistance = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY
        );
        this.lastPointer = {
          x: (e.touches[0].clientX + e.touches[1].clientX) / 2,
          y: (e.touches[0].clientY + e.touches[1].clientY) / 2
        };
      }
    }, { passive: false });

    this.canvas.addEventListener('touchmove', (e) => {
      e.preventDefault();
      if (e.touches.length === 1 && !this.isPanning) {
        const t = e.touches[0];
        this._onPointerMove({ clientX: t.clientX, clientY: t.clientY });
      } else if (e.touches.length === 2) {
        const p1 = e.touches[0];
        const p2 = e.touches[1];
        const currentDistance = Math.hypot(p1.clientX - p2.clientX, p1.clientY - p2.clientY);
        const midX = (p1.clientX + p2.clientX) / 2;
        const midY = (p1.clientY + p2.clientY) / 2;

        if (this.lastTouchDistance && this.lastTouchDistance > 0) {
          const factor = currentDistance / this.lastTouchDistance;
          this.zoomAt(midX, midY, factor);
        }
        this.panX += midX - this.lastPointer.x;
        this.panY += midY - this.lastPointer.y;

        this.lastTouchDistance = currentDistance;
        this.lastPointer = { x: midX, y: midY };
        this.render();
      }
    }, { passive: false });

    this.canvas.addEventListener('touchend', (e) => {
      e.preventDefault();
      this.lastTouchDistance = null;
      this._onPointerUp(e);
    }, { passive: false });
  }

  _onWheel(e) {
    e.preventDefault();
    const rect = this.canvas.getBoundingClientRect();
    const clientX = e.clientX - rect.left;
    const clientY = e.clientY - rect.top;

    if (e.ctrlKey || e.metaKey) {
      // Zoom
      const zoomFactor = e.deltaY < 0 ? 1.08 : 0.92;
      this.zoomAt(clientX, clientY, zoomFactor);
    } else {
      // Pan
      this.panX -= e.deltaX;
      this.panY -= e.deltaY;
      this.render();
    }
  }

  zoomAt(screenX, screenY, factor) {
    const oldScale = this.scale;
    let newScale = this.scale * factor;
    newScale = Math.max(0.1, Math.min(10.0, newScale));

    const worldPoint = {
      x: (screenX - this.panX) / oldScale,
      y: (screenY - this.panY) / oldScale
    };

    this.scale = newScale;
    this.panX = screenX - worldPoint.x * newScale;
    this.panY = screenY - worldPoint.y * newScale;
    this.render();
  }

  resetView() {
    this.scale = 1.0;
    this.panX = 0;
    this.panY = 0;
    this.render();
  }

  _onPointerDown(e) {
    // Space or middle mouse or pan tool triggers pan
    if (e.button === 1 || e.spaceKey || this.tool === 'pan') {
      this.isPanning = true;
      this.lastPointer = { x: e.clientX, y: e.clientY };
      return;
    }

    if (e.button !== 0) return;

    const pt = this.screenToWorld(e.clientX, e.clientY);

    if (this.tool === 'text') {
      const text = window.prompt('Enter diagram label or note text:');
      if (text) {
        this.saveHistory();
        this.elements.push({
          id: Date.now().toString(),
          type: 'text',
          points: [pt],
          color: this.color,
          lineWidth: this.lineWidth,
          text
        });
        this.render();
      }
      return;
    }

    this.isInteracting = true;
    this.currentElement = {
      id: Date.now().toString(),
      type: this.tool,
      points: [pt],
      color: this.color,
      lineWidth: this.lineWidth
    };
    this.render();
  }

  _onPointerMove(e) {
    if (this.isPanning) {
      const dx = e.clientX - this.lastPointer.x;
      const dy = e.clientY - this.lastPointer.y;
      this.panX += dx;
      this.panY += dy;
      this.lastPointer = { x: e.clientX, y: e.clientY };
      this.render();
      return;
    }

    if (!this.isInteracting || !this.currentElement) return;

    const pt = this.screenToWorld(e.clientX, e.clientY);

    if (this.tool === 'pen' || this.tool === 'eraser') {
      this.currentElement.points.push(pt);
    } else {
      this.currentElement.points = [this.currentElement.points[0], pt];
    }

    this.render();
  }

  _onPointerUp(e) {
    if (this.isPanning) {
      this.isPanning = false;
    }

    if (!this.isInteracting || !this.currentElement) return;
    this.isInteracting = false;

    this.saveHistory();
    this.elements.push(this.currentElement);
    this.currentElement = null;
    this.redoList = [];
    this.render();
  }

  saveHistory() {
    this.history.push(JSON.parse(JSON.stringify(this.elements)));
    if (this.history.length > 50) this.history.shift();
  }

  undo() {
    if (this.elements.length === 0 && this.history.length === 0) return;
    if (this.history.length > 0) {
      this.redoList.push(JSON.parse(JSON.stringify(this.elements)));
      this.elements = this.history.pop();
      this.render();
    } else {
      this.redoList.push(JSON.parse(JSON.stringify(this.elements)));
      this.elements = [];
      this.render();
    }
  }

  redo() {
    if (this.redoList.length === 0) return;
    this.saveHistory();
    this.elements = this.redoList.pop();
    this.render();
  }

  clear() {
    if (this.elements.length === 0) return;
    this.saveHistory();
    this.elements = [];
    this.redoList = [];
    this.render();
  }

  render() {
    const ctx = this.ctx;
    const canvas = this.canvas;
    if (!ctx || !canvas) return;

    const dpr = window.devicePixelRatio || 1;

    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // Apply viewport transformation
    ctx.scale(dpr, dpr);
    ctx.translate(this.panX, this.panY);
    ctx.scale(this.scale, this.scale);

    // Infinite grid background
    this._renderGrid(ctx);

    // Render committed elements
    for (let i = 0; i < this.elements.length; i++) {
      this._drawElement(ctx, this.elements[i]);
    }

    // Render in-progress active stroke/shape
    if (this.currentElement) {
      this._drawElement(ctx, this.currentElement);
    }

    ctx.restore();
  }

  _renderGrid(ctx) {
    const gridSize = 32;
    const dpr = window.devicePixelRatio || 1;
    const viewW = this.canvas.width / (dpr * this.scale);
    const viewH = this.canvas.height / (dpr * this.scale);

    const startX = -this.panX / this.scale;
    const startY = -this.panY / this.scale;
    const endX = startX + viewW;
    const endY = startY + viewH;

    const firstX = Math.floor(startX / gridSize) * gridSize;
    const firstY = Math.floor(startY / gridSize) * gridSize;

    ctx.save();
    ctx.strokeStyle = '#1e293b';
    ctx.lineWidth = 0.5 / this.scale;

    ctx.beginPath();
    for (let x = firstX; x <= endX; x += gridSize) {
      ctx.moveTo(x, startY);
      ctx.lineTo(x, endY);
    }
    for (let y = firstY; y <= endY; y += gridSize) {
      ctx.moveTo(startX, y);
      ctx.lineTo(endX, y);
    }
    ctx.stroke();

    // Subtle origin indicator
    ctx.strokeStyle = '#334155';
    ctx.lineWidth = 1.0 / this.scale;
    ctx.beginPath();
    ctx.moveTo(-12, 0); ctx.lineTo(12, 0);
    ctx.moveTo(0, -12); ctx.lineTo(0, 12);
    ctx.stroke();

    ctx.restore();
  }

  _drawElement(ctx, el) {
    if (!el.points || el.points.length === 0) return;

    ctx.save();
    ctx.strokeStyle = el.type === 'eraser' ? '#0f172a' : el.color;
    ctx.fillStyle = el.color;
    ctx.lineWidth = el.type === 'eraser' ? el.lineWidth * 5 : el.lineWidth;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';

    const start = el.points[0];

    switch (el.type) {
      case 'pen':
      case 'eraser': {
        ctx.beginPath();
        ctx.moveTo(start.x, start.y);
        for (let i = 1; i < el.points.length; i++) {
          ctx.lineTo(el.points[i].x, el.points[i].y);
        }
        ctx.stroke();
        break;
      }
      case 'line': {
        const end = el.points[el.points.length - 1];
        ctx.beginPath();
        ctx.moveTo(start.x, start.y);
        ctx.lineTo(end.x, end.y);
        ctx.stroke();
        break;
      }
      case 'arrow': {
        const end = el.points[el.points.length - 1];
        const angle = Math.atan2(end.y - start.y, end.x - start.x);
        const headlen = Math.max(12, el.lineWidth * 3.5);

        ctx.beginPath();
        ctx.moveTo(start.x, start.y);
        ctx.lineTo(end.x, end.y);
        ctx.stroke();

        ctx.beginPath();
        ctx.moveTo(end.x, end.y);
        ctx.lineTo(
          end.x - headlen * Math.cos(angle - Math.PI / 6),
          end.y - headlen * Math.sin(angle - Math.PI / 6)
        );
        ctx.lineTo(
          end.x - headlen * Math.cos(angle + Math.PI / 6),
          end.y - headlen * Math.sin(angle + Math.PI / 6)
        );
        ctx.closePath();
        ctx.fill();
        break;
      }
      case 'rect': {
        const end = el.points[el.points.length - 1];
        const rx = Math.min(start.x, end.x);
        const ry = Math.min(start.y, end.y);
        const rw = Math.abs(end.x - start.x);
        const rh = Math.abs(end.y - start.y);
        ctx.strokeRect(rx, ry, rw, rh);
        break;
      }
      case 'circle': {
        const end = el.points[el.points.length - 1];
        const radius = Math.hypot(end.x - start.x, end.y - start.y);
        ctx.beginPath();
        ctx.arc(start.x, start.y, radius, 0, 2 * Math.PI);
        ctx.stroke();
        break;
      }
      case 'text': {
        if (el.text) {
          ctx.font = `${Math.max(14, el.lineWidth * 5)}px sans-serif`;
          ctx.fillText(el.text, start.x, start.y);
        }
        break;
      }
    }
    ctx.restore();
  }

  // Export bounding box of all strokes or current viewport to PNG base64
  exportImageBase64() {
    if (this.elements.length === 0) return null;

    // Calculate bounding box of all elements in world coordinates
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (const el of this.elements) {
      for (const pt of el.points) {
        if (pt.x < minX) minX = pt.x;
        if (pt.y < minY) minY = pt.y;
        if (pt.x > maxX) maxX = pt.x;
        if (pt.y > maxY) maxY = pt.y;
      }
    }

    const padding = 40;
    minX -= padding;
    minY -= padding;
    maxX += padding;
    maxY += padding;

    const w = Math.max(200, Math.ceil(maxX - minX));
    const h = Math.max(200, Math.ceil(maxY - minY));

    // Render to offscreen canvas
    const offCanvas = document.createElement('canvas');
    offCanvas.width = w;
    offCanvas.height = h;
    const offCtx = offCanvas.getContext('2d');

    // Fill background with dark slate matching UI
    offCtx.fillStyle = '#0f172a';
    offCtx.fillRect(0, 0, w, h);

    offCtx.save();
    offCtx.translate(-minX, -minY);

    for (let i = 0; i < this.elements.length; i++) {
      this._drawElement(offCtx, this.elements[i]);
    }
    offCtx.restore();

    return offCanvas.toDataURL('image/png');
  }
}
