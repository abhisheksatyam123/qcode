// Excalidraw-like Infinite Canvas Engine for QCode
// Implements:
// - Organic hand-drawn Rough.js shapes (rect, diamond, ellipse, arrow, line)
// - Continuous smooth Bezier freedraw (pen stays active across multiple strokes)
// - Excalidraw auto-expanding inline text editing & in-shape container text
// - 8-point resize handles, dragging, marquee selection, duplicate, undo/redo
// - High-resolution bounding-box export for Multimodal AI OCR

import rough from './vendor-rough.esm.js';

export class InfiniteCanvas {
  constructor(canvasElement, options = {}) {
    this.canvas = canvasElement;
    this.ctx = canvasElement.getContext('2d');
    this.options = options;
    this.rc = rough.canvas(canvasElement);

    // Viewport transform
    this.scale = 1.0;
    this.panX = 0;
    this.panY = 0;

    // Active tool: 'select' | 'rect' | 'diamond' | 'circle' | 'arrow' | 'line' | 'pen' | 'text' | 'eraser' | 'pan'
    this.tool = 'select';

    // Styling defaults
    this.strokeColor = '#f8fafc';
    this.fillColor = 'transparent';
    this.fillStyle = 'hachure'; // 'hachure' | 'solid' | 'none'
    this.lineWidth = 2.5; // 1.5 (thin) | 2.5 (medium) | 4.5 (bold)
    this.roughness = 1.2; // 0.5 (clean) | 1.2 (artist) | 2.2 (cartoon)
    this.fontSize = 20; // 14 (S) | 20 (M) | 28 (L) | 36 (XL)

    // Elements & selection
    this.elements = [];
    this.selectedIds = new Set();
    this.history = [];
    this.redoList = [];

    // Interaction state
    this.isInteracting = false;
    this.isPanning = false;
    this.isDraggingSelected = false;
    this.isResizing = false;
    this.activeResizeHandle = null;
    this.lastPointer = { x: 0, y: 0 };
    this.startPointer = { x: 0, y: 0 };
    this.currentElement = null;
    this.selectionMarquee = null;

    // Inline text editing
    this.activeTextarea = null;
    this.editingElement = null;
    this.editingPos = { x: 0, y: 0 };

    // Callbacks
    this.onSelectionChange = options.onSelectionChange || null;
    this.onViewChange = options.onViewChange || null;
    this.onToolChange = options.onToolChange || null;

    this._bindEvents();
    this.resize();
  }

  setTool(tool) {
    this._commitTextIfEditing();
    this.tool = tool;
    if (tool !== 'select') {
      this.selectedIds.clear();
    }
    this._updateCursor();
    if (this.onToolChange) this.onToolChange(tool);
    this.render();
  }

  _updateCursor() {
    if (this.tool === 'pan') {
      this.canvas.style.cursor = 'grab';
    } else if (this.tool === 'pen') {
      this.canvas.style.cursor = 'crosshair';
    } else if (this.tool === 'text') {
      this.canvas.style.cursor = 'text';
    } else if (this.tool === 'eraser') {
      this.canvas.style.cursor = 'cell';
    } else if (this.tool === 'select') {
      this.canvas.style.cursor = 'default';
    } else {
      this.canvas.style.cursor = 'crosshair';
    }
  }

  setStrokeColor(color) {
    this.strokeColor = color;
    if (this.selectedIds.size > 0) {
      this.saveHistory();
      for (const el of this.elements) {
        if (this.selectedIds.has(el.id)) el.strokeColor = color;
      }
    }
    this.render();
  }

  setFillColor(color, style = null) {
    this.fillColor = color;
    if (style) this.fillStyle = style;
    if (this.selectedIds.size > 0) {
      this.saveHistory();
      for (const el of this.elements) {
        if (this.selectedIds.has(el.id)) {
          el.fillColor = color;
          if (style) el.fillStyle = style;
        }
      }
    }
    this.render();
  }

  setLineWidth(width) {
    this.lineWidth = width;
    if (this.selectedIds.size > 0) {
      this.saveHistory();
      for (const el of this.elements) {
        if (this.selectedIds.has(el.id)) el.lineWidth = width;
      }
    }
    this.render();
  }

  setRoughness(r) {
    this.roughness = r;
    if (this.selectedIds.size > 0) {
      this.saveHistory();
      for (const el of this.elements) {
        if (this.selectedIds.has(el.id)) el.roughness = r;
      }
    }
    this.render();
  }

  setFontSize(size) {
    this.fontSize = size;
    if (this.selectedIds.size > 0) {
      this.saveHistory();
      for (const el of this.elements) {
        if (this.selectedIds.has(el.id)) el.fontSize = size;
      }
    }
    this.render();
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

  // --- Geometry & Accurate Bounds Calculation ---

  getElementBounds(el) {
    if (!el.points || el.points.length === 0) return { x: 0, y: 0, w: 0, h: 0 };
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (const pt of el.points) {
      if (pt.x < minX) minX = pt.x;
      if (pt.y < minY) minY = pt.y;
      if (pt.x > maxX) maxX = pt.x;
      if (pt.y > maxY) maxY = pt.y;
    }

    if (el.type === 'circle') {
      const radius = Math.hypot(el.points[1].x - el.points[0].x, el.points[1].y - el.points[0].y);
      minX = el.points[0].x - radius;
      maxX = el.points[0].x + radius;
      minY = el.points[0].y - radius;
      maxY = el.points[0].y + radius;
    } else if (el.type === 'text') {
      const fontSize = el.fontSize || this.fontSize;
      const lines = (el.text || '').split('\n');
      this.ctx.save();
      this.ctx.font = `${fontSize}px 'Patrick Hand', 'Caveat', cursive, sans-serif`;
      let maxW = 20;
      for (const line of lines) {
        const w = this.ctx.measureText(line).width;
        if (w > maxW) maxW = w;
      }
      this.ctx.restore();
      const lineH = fontSize * 1.3;
      const totalH = Math.max(lineH, lines.length * lineH);
      maxX = minX + maxW + 8;
      maxY = minY + totalH;
    }

    return {
      x: minX,
      y: minY,
      w: Math.max(4, maxX - minX),
      h: Math.max(4, maxY - minY)
    };
  }

  getCombinedBounds(elements) {
    if (elements.length === 0) return null;
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (const el of elements) {
      const b = this.getElementBounds(el);
      if (b.x < minX) minX = b.x;
      if (b.y < minY) minY = b.y;
      if (b.x + b.w > maxX) maxX = b.x + b.w;
      if (b.y + b.h > maxY) maxY = b.y + b.h;
    }
    return { x: minX, y: minY, w: maxX - minX, h: maxY - minY };
  }

  isPointInElement(pt, el) {
    const pad = Math.max(10, (el.lineWidth || 3) * 2);
    const bounds = this.getElementBounds(el);

    if (pt.x < bounds.x - pad || pt.x > bounds.x + bounds.w + pad ||
        pt.y < bounds.y - pad || pt.y > bounds.y + bounds.h + pad) {
      return false;
    }

    if (el.type === 'rect' || el.type === 'diamond' || el.type === 'text') {
      return true;
    }

    if (el.type === 'circle') {
      const center = el.points[0];
      const radius = Math.hypot(el.points[1].x - el.points[0].x, el.points[1].y - el.points[0].y);
      const dist = Math.hypot(pt.x - center.x, pt.y - center.y);
      return dist <= radius + pad;
    }

    if (el.type === 'line' || el.type === 'arrow') {
      const p1 = el.points[0];
      const p2 = el.points[el.points.length - 1];
      return this._distToSegment(pt, p1, p2) <= pad;
    }

    if (el.type === 'pen' || el.type === 'eraser') {
      if (el.points.length === 1) {
        return Math.hypot(pt.x - el.points[0].x, pt.y - el.points[0].y) <= pad;
      }
      for (let i = 0; i < el.points.length - 1; i++) {
        if (this._distToSegment(pt, el.points[i], el.points[i + 1]) <= pad) return true;
      }
      return false;
    }

    return true;
  }

  _distToSegment(p, v, w) {
    const l2 = Math.hypot(v.x - w.x, v.y - w.y) ** 2;
    if (l2 === 0) return Math.hypot(p.x - v.x, p.y - v.y);
    let t = ((p.x - v.x) * (w.x - v.x) + (p.y - v.y) * (w.y - v.y)) / l2;
    t = Math.max(0, Math.min(1, t));
    return Math.hypot(p.x - (v.x + t * (w.x - v.x)), p.y - (v.y + t * (w.y - v.y)));
  }

  getResizeHandleAtPoint(pt, bounds) {
    if (!bounds) return null;
    const handleSize = 10 / this.scale;
    const handles = {
      nw: { x: bounds.x, y: bounds.y },
      ne: { x: bounds.x + bounds.w, y: bounds.y },
      se: { x: bounds.x + bounds.w, y: bounds.y + bounds.h },
      sw: { x: bounds.x, y: bounds.y + bounds.h },
      n: { x: bounds.x + bounds.w / 2, y: bounds.y },
      s: { x: bounds.x + bounds.w / 2, y: bounds.y + bounds.h },
      w: { x: bounds.x, y: bounds.y + bounds.h / 2 },
      e: { x: bounds.x + bounds.w, y: bounds.y + bounds.h / 2 }
    };

    for (const [pos, h] of Object.entries(handles)) {
      if (Math.abs(pt.x - h.x) <= handleSize && Math.abs(pt.y - h.y) <= handleSize) {
        return pos;
      }
    }
    return null;
  }

  // --- Excalidraw-Style Inline Text Editing ---

  startInlineTextEditing(worldX, worldY, existingElement = null, isContainer = false) {
    this._commitTextIfEditing();

    const fontSize = existingElement?.fontSize || this.fontSize;
    const color = existingElement?.strokeColor || this.strokeColor;
    const scr = this.worldToScreen(worldX, worldY);

    const textarea = document.createElement('textarea');
    textarea.className = 'canvas-inline-textarea';
    textarea.value = existingElement ? (existingElement.text || '') : '';
    textarea.placeholder = 'Type text... (Ctrl+Enter to finish)';
    textarea.style.position = 'absolute';
    textarea.style.left = `${Math.round(scr.x)}px`;
    textarea.style.top = `${Math.round(scr.y)}px`;
    textarea.style.fontSize = `${Math.round(fontSize * this.scale)}px`;
    textarea.style.color = color;
    textarea.style.fontFamily = "'Patrick Hand', 'Caveat', cursive, sans-serif";
    textarea.style.fontWeight = '500';
    textarea.style.lineHeight = '1.25';
    textarea.style.background = 'rgba(15, 23, 42, 0.95)';
    textarea.style.border = '1.5px dashed #38bdf8';
    textarea.style.borderRadius = '6px';
    textarea.style.outline = 'none';
    textarea.style.zIndex = '1000';
    textarea.style.padding = '4px 8px';
    textarea.style.minWidth = isContainer ? '180px' : '140px';
    textarea.style.minHeight = '36px';
    textarea.style.overflow = 'hidden';
    textarea.style.resize = 'none';
    textarea.style.boxShadow = '0 10px 25px rgba(0, 0, 0, 0.6)';

    if (isContainer) {
      textarea.style.transform = 'translate(-50%, -50%)';
      textarea.style.textAlign = 'center';
    }

    const autoResize = () => {
      textarea.style.height = 'auto';
      textarea.style.height = `${Math.max(36, textarea.scrollHeight + 4)}px`;
      textarea.style.width = 'auto';
      textarea.style.width = `${Math.max(isContainer ? 180 : 140, textarea.scrollWidth + 16)}px`;
    };

    textarea.addEventListener('input', autoResize);

    this.canvas.parentElement.appendChild(textarea);
    autoResize();
    textarea.focus();

    this.activeTextarea = textarea;
    this.editingElement = existingElement;
    this.isEditingContainer = isContainer;
    this.editingPos = { x: worldX, y: worldY };

    const commit = () => {
      this._commitTextIfEditing();
    };

    textarea.addEventListener('blur', commit);
    textarea.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') {
        commit();
      } else if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
        e.preventDefault();
        commit();
      }
    });
  }

  _commitTextIfEditing() {
    if (!this.activeTextarea) return;
    const text = this.activeTextarea.value.trim();
    const parent = this.activeTextarea.parentElement;
    if (parent) parent.removeChild(this.activeTextarea);
    this.activeTextarea = null;

    if (!text) {
      if (this.editingElement && this.editingElement.type === 'text') {
        this.deleteElements([this.editingElement.id]);
      } else if (this.editingElement && this.isEditingContainer) {
        this.saveHistory();
        this.editingElement.text = '';
      }
      this.editingElement = null;
      this.isEditingContainer = false;
      this.render();
      return;
    }

    this.saveHistory();

    if (this.editingElement) {
      this.editingElement.text = text;
      this.selectedIds = new Set([this.editingElement.id]);
    } else {
      const newEl = {
        id: Date.now().toString(),
        type: 'text',
        points: [{ x: this.editingPos.x, y: this.editingPos.y }],
        strokeColor: this.strokeColor,
        fontSize: this.fontSize,
        text: text,
        seed: Math.floor(Math.random() * 200000)
      };
      this.elements.push(newEl);
      this.selectedIds = new Set([newEl.id]);
    }

    this.editingElement = null;
    this.isEditingContainer = false;
    this.setTool('select');
    this.render();
  }

  // --- Interaction & Event Handlers ---

  _bindEvents() {
    this.canvas.addEventListener('mousedown', (e) => this._onMouseDown(e));
    window.addEventListener('mousemove', (e) => this._onMouseMove(e));
    window.addEventListener('mouseup', (e) => this._onMouseUp(e));
    this.canvas.addEventListener('dblclick', (e) => this._onDoubleClick(e));
    this.canvas.addEventListener('wheel', (e) => this._onWheel(e), { passive: false });
    window.addEventListener('keydown', (e) => this._onKeyDown(e));

    // Touch support
    this.canvas.addEventListener('touchstart', (e) => this._onTouchStart(e), { passive: false });
    this.canvas.addEventListener('touchmove', (e) => this._onTouchMove(e), { passive: false });
    this.canvas.addEventListener('touchend', (e) => this._onTouchEnd(e), { passive: false });
  }

  _onKeyDown(e) {
    if (this.activeTextarea) return;
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;

    if (e.key === 'Escape') {
      e.preventDefault();
      this.setTool('select');
      this.selectedIds.clear();
      this.render();
    } else if (e.key === 'Delete' || e.key === 'Backspace') {
      if (this.selectedIds.size > 0) {
        e.preventDefault();
        this.deleteElements(Array.from(this.selectedIds));
      }
    } else if (e.key === 'Enter') {
      // If a shape container is selected, Enter triggers in-shape text editing
      if (this.selectedIds.size === 1) {
        const sel = this.elements.find(el => this.selectedIds.has(el.id));
        if (sel && (sel.type === 'rect' || sel.type === 'diamond' || sel.type === 'circle')) {
          e.preventDefault();
          const bounds = this.getElementBounds(sel);
          this.startInlineTextEditing(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2, sel, true);
        }
      }
    } else if (e.key === 'z' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      if (e.shiftKey) this.redo();
      else this.undo();
    } else if (e.key === 'y' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      this.redo();
    } else if (e.key === 'd' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      this.duplicateSelected();
    } else if (e.key === 'a' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      this.selectedIds = new Set(this.elements.map(el => el.id));
      this.setTool('select');
      this.render();
    } else if (e.key === '1' || e.key === 'v' || e.key === 'V') {
      this.setTool('select');
    } else if (e.key === '2' || e.key === 'r' || e.key === 'R') {
      this.setTool('rect');
    } else if (e.key === '3' || e.key === 'd' || e.key === 'D') {
      this.setTool('diamond');
    } else if (e.key === '4' || e.key === 'o' || e.key === 'O') {
      this.setTool('circle');
    } else if (e.key === '5' || e.key === 'a' || e.key === 'A') {
      this.setTool('arrow');
    } else if (e.key === '6' || e.key === 'l' || e.key === 'L') {
      this.setTool('line');
    } else if (e.key === '7' || e.key === 'p' || e.key === 'P') {
      this.setTool('pen');
    } else if (e.key === '8' || e.key === 't' || e.key === 'T') {
      this.setTool('text');
    } else if (e.key === '9' || e.key === 'e' || e.key === 'E') {
      this.setTool('eraser');
    } else if (e.key === '0' || e.key === 'h' || e.key === 'H') {
      this.setTool('pan');
    }
  }

  _onDoubleClick(e) {
    const pt = this.screenToWorld(e.clientX, e.clientY);

    // Check if double-clicked existing text or shape container
    for (let i = this.elements.length - 1; i >= 0; i--) {
      const el = this.elements[i];
      if (this.isPointInElement(pt, el)) {
        if (el.type === 'text') {
          this.startInlineTextEditing(el.points[0].x, el.points[0].y, el, false);
          return;
        } else if (el.type === 'rect' || el.type === 'diamond' || el.type === 'circle') {
          const bounds = this.getElementBounds(el);
          this.startInlineTextEditing(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2, el, true);
          return;
        }
      }
    }

    // Double-clicked on empty canvas: spawn new text element
    this.startInlineTextEditing(pt.x, pt.y, null, false);
  }

  _onMouseDown(e) {
    if (e.button === 1 || e.spaceKey || this.tool === 'pan') {
      this.isPanning = true;
      this.lastPointer = { x: e.clientX, y: e.clientY };
      this.canvas.style.cursor = 'grab';
      return;
    }

    if (e.button !== 0) return;

    this._commitTextIfEditing();
    const pt = this.screenToWorld(e.clientX, e.clientY);
    this.startPointer = pt;
    this.lastPointer = { x: e.clientX, y: e.clientY };

    // 1. Text Tool: spawn inline editor directly on click
    if (this.tool === 'text') {
      this.startInlineTextEditing(pt.x, pt.y, null, false);
      return;
    }

    // 2. Eraser Tool: delete clicked element or begin swipe erasing
    if (this.tool === 'eraser') {
      this.isInteracting = true;
      this._eraseAt(pt);
      return;
    }

    // 3. Selection Tool: handle resize, drag, or marquee selection
    if (this.tool === 'select') {
      const selectedElements = this.elements.filter(el => this.selectedIds.has(el.id));
      const combinedBounds = this.getCombinedBounds(selectedElements);

      // Check resize handle
      if (selectedElements.length === 1 && combinedBounds) {
        const handle = this.getResizeHandleAtPoint(pt, combinedBounds);
        if (handle) {
          this.isResizing = true;
          this.activeResizeHandle = handle;
          this.resizeInitialElement = JSON.parse(JSON.stringify(selectedElements[0]));
          return;
        }
      }

      // Check if clicked inside currently selected element
      let clickedInsideSelection = false;
      for (const el of selectedElements) {
        if (this.isPointInElement(pt, el)) {
          clickedInsideSelection = true;
          break;
        }
      }

      if (clickedInsideSelection) {
        this.isDraggingSelected = true;
        this.isInteracting = true;
        return;
      }

      // Check if clicked any other element
      let clickedEl = null;
      for (let i = this.elements.length - 1; i >= 0; i--) {
        if (this.isPointInElement(pt, this.elements[i])) {
          clickedEl = this.elements[i];
          break;
        }
      }

      if (clickedEl) {
        if (e.shiftKey) {
          if (this.selectedIds.has(clickedEl.id)) this.selectedIds.delete(clickedEl.id);
          else this.selectedIds.add(clickedEl.id);
        } else {
          this.selectedIds = new Set([clickedEl.id]);
        }
        this.isDraggingSelected = true;
        this.isInteracting = true;
        this.render();
        return;
      }

      // Clicked empty canvas: start marquee selection
      if (!e.shiftKey) this.selectedIds.clear();
      this.selectionMarquee = { startX: pt.x, startY: pt.y, currentX: pt.x, currentY: pt.y };
      this.isInteracting = true;
      this.render();
      return;
    }

    // 4. Pen (Freedraw) & Shapes (Rect, Diamond, Ellipse, Arrow, Line)
    this.isInteracting = true;
    this.selectedIds.clear();

    const seed = Math.floor(Math.random() * 200000);
    this.currentElement = {
      id: Date.now().toString(),
      type: this.tool,
      points: this.tool === 'pen' ? [pt] : [pt, { ...pt }],
      strokeColor: this.strokeColor,
      fillColor: this.fillColor,
      fillStyle: this.fillStyle,
      lineWidth: this.lineWidth,
      roughness: this.roughness,
      seed: seed
    };
    this.render();
  }

  _onMouseMove(e) {
    const pt = this.screenToWorld(e.clientX, e.clientY);

    // Pan viewport
    if (this.isPanning) {
      const dx = e.clientX - this.lastPointer.x;
      const dy = e.clientY - this.lastPointer.y;
      this.panX += dx;
      this.panY += dy;
      this.lastPointer = { x: e.clientX, y: e.clientY };
      this.render();
      return;
    }

    // Hover handle cursor in select mode
    if (this.tool === 'select' && !this.isInteracting && !this.isResizing) {
      const selected = this.elements.filter(el => this.selectedIds.has(el.id));
      if (selected.length === 1) {
        const bounds = this.getElementBounds(selected[0]);
        const handle = this.getResizeHandleAtPoint(pt, bounds);
        if (handle) {
          const cursors = {
            nw: 'nwse-resize', se: 'nwse-resize',
            ne: 'nesw-resize', sw: 'nesw-resize',
            n: 'ns-resize', s: 'ns-resize',
            e: 'ew-resize', w: 'ew-resize'
          };
          this.canvas.style.cursor = cursors[handle] || 'default';
          return;
        }
      }
      this._updateCursor();
    }

    if (!this.isInteracting && !this.isResizing) return;

    // Resizing
    if (this.isResizing && this.resizeInitialElement) {
      this._handleElementResize(pt);
      this.render();
      return;
    }

    // Dragging selected
    if (this.isDraggingSelected) {
      const dx = pt.x - this.startPointer.x;
      const dy = pt.y - this.startPointer.y;
      this.startPointer = pt;

      for (const el of this.elements) {
        if (this.selectedIds.has(el.id)) {
          for (const p of el.points) {
            p.x += dx;
            p.y += dy;
          }
        }
      }
      this.render();
      return;
    }

    // Marquee selection
    if (this.selectionMarquee) {
      this.selectionMarquee.currentX = pt.x;
      this.selectionMarquee.currentY = pt.y;
      const mX = Math.min(this.selectionMarquee.startX, pt.x);
      const mY = Math.min(this.selectionMarquee.startY, pt.y);
      const mW = Math.abs(pt.x - this.selectionMarquee.startX);
      const mH = Math.abs(pt.y - this.selectionMarquee.startY);

      for (const el of this.elements) {
        const b = this.getElementBounds(el);
        if (b.x + b.w >= mX && b.x <= mX + mW && b.y + b.h >= mY && b.y <= mY + mH) {
          this.selectedIds.add(el.id);
        }
      }
      this.render();
      return;
    }

    // Swipe erasing
    if (this.tool === 'eraser') {
      this._eraseAt(pt);
      return;
    }

    // Drawing: Pen (Freedraw) appends continuous points
    if (this.currentElement) {
      if (this.tool === 'pen') {
        this.currentElement.points.push(pt);
      } else {
        this.currentElement.points = [this.currentElement.points[0], pt];
      }
      this.render();
    }
  }

  _handleElementResize(pt) {
    const el = this.elements.find(e => this.selectedIds.has(e.id));
    if (!el || !this.resizeInitialElement) return;

    const handle = this.activeResizeHandle;
    const init = this.resizeInitialElement;
    const initBounds = this.getElementBounds(init);

    let newX = initBounds.x;
    let newY = initBounds.y;
    let newW = initBounds.w;
    let newH = initBounds.h;

    if (handle.includes('e')) newW = Math.max(10, pt.x - initBounds.x);
    if (handle.includes('s')) newH = Math.max(10, pt.y - initBounds.y);
    if (handle.includes('w')) {
      const right = initBounds.x + initBounds.w;
      newX = Math.min(right - 10, pt.x);
      newW = right - newX;
    }
    if (handle.includes('n')) {
      const bottom = initBounds.y + initBounds.h;
      newY = Math.min(bottom - 10, pt.y);
      newH = bottom - newY;
    }

    if (el.type === 'rect' || el.type === 'diamond') {
      el.points = [{ x: newX, y: newY }, { x: newX + newW, y: newY + newH }];
    } else if (el.type === 'circle') {
      const radius = Math.max(newW, newH) / 2;
      el.points = [{ x: newX + radius, y: newY + radius }, { x: newX + radius * 2, y: newY + radius }];
    } else if (el.type === 'line' || el.type === 'arrow') {
      el.points = [{ x: newX, y: newY }, { x: newX + newW, y: newY + newH }];
    }
  }

  _onMouseUp(e) {
    if (this.isPanning) {
      this.isPanning = false;
      this._updateCursor();
    }

    if (this.isResizing) {
      this.saveHistory();
      this.isResizing = false;
      this.activeResizeHandle = null;
      this.resizeInitialElement = null;
      this.render();
      return;
    }

    if (this.isDraggingSelected) {
      this.saveHistory();
      this.isDraggingSelected = false;
    }

    if (this.selectionMarquee) {
      this.selectionMarquee = null;
    }

    if (this.currentElement) {
      const pts = this.currentElement.points;
      const p1 = pts[0];
      const p2 = pts[pts.length - 1];
      const dist = Math.hypot(p2.x - p1.x, p2.y - p1.y);

      if (this.tool === 'pen') {
        // Freedraw stroke finished: save stroke to elements
        this.saveHistory();
        this.elements.push(this.currentElement);
        // CRITICAL EXCALIDRAW FIX: In pen mode, STAY IN PEN MODE!
        // Do NOT switch to select, do NOT select the stroke with handles.
        this.selectedIds.clear();
      } else if (dist > 5) {
        // Shape finished: save shape
        this.saveHistory();
        this.elements.push(this.currentElement);
        this.selectedIds = new Set([this.currentElement.id]);
        // After drawing a shape, switch to select so user can immediately move/resize it
        this.setTool('select');
      }

      this.currentElement = null;
    }

    this.isInteracting = false;
    this.render();
  }

  _eraseAt(pt) {
    let changed = false;
    for (let i = this.elements.length - 1; i >= 0; i--) {
      if (this.isPointInElement(pt, this.elements[i])) {
        this.saveHistory();
        this.elements.splice(i, 1);
        changed = true;
      }
    }
    if (changed) this.render();
  }

  _onWheel(e) {
    e.preventDefault();
    const rect = this.canvas.getBoundingClientRect();
    const clientX = e.clientX - rect.left;
    const clientY = e.clientY - rect.top;

    if (e.ctrlKey || e.metaKey) {
      const zoomFactor = e.deltaY < 0 ? 1.10 : 0.90;
      this.zoomAt(clientX, clientY, zoomFactor);
    } else {
      this.panX -= e.deltaX;
      this.panY -= e.deltaY;
      this.render();
    }
  }

  zoomAt(screenX, screenY, factor) {
    const oldScale = this.scale;
    let newScale = this.scale * factor;
    newScale = Math.max(0.1, Math.min(8.0, newScale));

    const worldPoint = {
      x: (screenX - this.panX) / oldScale,
      y: (screenY - this.panY) / oldScale
    };

    this.scale = newScale;
    this.panX = screenX - worldPoint.x * newScale;
    this.panY = screenY - worldPoint.y * newScale;

    if (this.onViewChange) {
      this.onViewChange(Math.round(this.scale * 100));
    }
    this.render();
  }

  resetView() {
    this.scale = 1.0;
    this.panX = 0;
    this.panY = 0;
    if (this.onViewChange) this.onViewChange(100);
    this.render();
  }

  // --- Touch Support ---

  _onTouchStart(e) {
    e.preventDefault();
    if (e.touches.length === 1) {
      const t = e.touches[0];
      this._onMouseDown({ clientX: t.clientX, clientY: t.clientY, button: 0 });
    } else if (e.touches.length === 2) {
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
  }

  _onTouchMove(e) {
    e.preventDefault();
    if (e.touches.length === 1 && !this.isPanning) {
      const t = e.touches[0];
      this._onMouseMove({ clientX: t.clientX, clientY: t.clientY });
    } else if (e.touches.length === 2) {
      const p1 = e.touches[0];
      const p2 = e.touches[1];
      const currentDist = Math.hypot(p1.clientX - p2.clientX, p1.clientY - p2.clientY);
      const midX = (p1.clientX + p2.clientX) / 2;
      const midY = (p1.clientY + p2.clientY) / 2;

      if (this.lastTouchDistance && this.lastTouchDistance > 0) {
        this.zoomAt(midX, midY, currentDist / this.lastTouchDistance);
      }
      this.panX += midX - this.lastPointer.x;
      this.panY += midY - this.lastPointer.y;
      this.lastTouchDistance = currentDist;
      this.lastPointer = { x: midX, y: midY };
      this.render();
    }
  }

  _onTouchEnd(e) {
    e.preventDefault();
    this.lastTouchDistance = null;
    this._onMouseUp(e);
  }

  // --- History & Operations ---

  saveHistory() {
    this.history.push(JSON.parse(JSON.stringify(this.elements)));
    if (this.history.length > 50) this.history.shift();
    this.redoList = [];
  }

  undo() {
    if (this.history.length === 0) return;
    this.redoList.push(JSON.parse(JSON.stringify(this.elements)));
    this.elements = this.history.pop();
    this.selectedIds.clear();
    this.render();
  }

  redo() {
    if (this.redoList.length === 0) return;
    this.history.push(JSON.parse(JSON.stringify(this.elements)));
    this.elements = this.redoList.pop();
    this.selectedIds.clear();
    this.render();
  }

  deleteElements(ids) {
    this.saveHistory();
    const idSet = new Set(ids);
    this.elements = this.elements.filter(el => !idSet.has(el.id));
    this.selectedIds.clear();
    this.render();
  }

  duplicateSelected() {
    if (this.selectedIds.size === 0) return;
    this.saveHistory();
    const newSelected = new Set();
    const duplicated = [];

    for (const el of this.elements) {
      if (this.selectedIds.has(el.id)) {
        const copy = JSON.parse(JSON.stringify(el));
        copy.id = Date.now().toString() + Math.random().toString(36).substring(2, 6);
        copy.seed = Math.floor(Math.random() * 200000);
        for (const p of copy.points) {
          p.x += 24;
          p.y += 24;
        }
        duplicated.push(copy);
        newSelected.add(copy.id);
      }
    }

    this.elements.push(...duplicated);
    this.selectedIds = newSelected;
    this.render();
  }

  clear() {
    if (this.elements.length === 0) return;
    this.saveHistory();
    this.elements = [];
    this.selectedIds.clear();
    this.render();
  }

  // --- Rendering Engine with Rough.js ---

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

    // Excalidraw dot-grid
    this._renderGrid(ctx);

    // Render committed elements
    for (let i = 0; i < this.elements.length; i++) {
      this._renderElement(ctx, this.elements[i]);
    }

    // Render active drawing element
    if (this.currentElement) {
      this._renderElement(ctx, this.currentElement);
    }

    // Render selection bounding box & resize handles
    this._renderSelection(ctx);

    // Render marquee selection rectangle
    if (this.selectionMarquee) {
      ctx.save();
      ctx.strokeStyle = '#38bdf8';
      ctx.fillStyle = 'rgba(56, 189, 248, 0.15)';
      ctx.lineWidth = 1 / this.scale;
      const mX = Math.min(this.selectionMarquee.startX, this.selectionMarquee.currentX);
      const mY = Math.min(this.selectionMarquee.startY, this.selectionMarquee.currentY);
      const mW = Math.abs(this.selectionMarquee.currentX - this.selectionMarquee.startX);
      const mH = Math.abs(this.selectionMarquee.currentY - this.selectionMarquee.startY);
      ctx.fillRect(mX, mY, mW, mH);
      ctx.strokeRect(mX, mY, mW, mH);
      ctx.restore();
    }

    ctx.restore();
  }

  _renderGrid(ctx) {
    const gridSize = 28;
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
    ctx.fillStyle = '#1e293b';

    for (let x = firstX; x <= endX; x += gridSize) {
      for (let y = firstY; y <= endY; y += gridSize) {
        ctx.beginPath();
        ctx.arc(x, y, 1 / this.scale, 0, 2 * Math.PI);
        ctx.fill();
      }
    }

    ctx.restore();
  }

  _renderElement(ctx, el) {
    const rc = this.rc;
    const seed = el.seed || 1;
    const stroke = el.strokeColor || '#f8fafc';
    const strokeWidth = el.lineWidth || 2.5;
    const roughness = el.roughness != null ? el.roughness : 1.2;
    const fill = el.fillColor && el.fillColor !== 'transparent' ? el.fillColor : undefined;
    const fillStyle = el.fillStyle || 'hachure';

    const roughOptions = {
      seed: seed,
      stroke: stroke,
      strokeWidth: strokeWidth,
      roughness: roughness,
      fill: fill,
      fillStyle: fillStyle,
      hachureAngle: -41,
      hachureGap: Math.max(4, strokeWidth * 2)
    };

    const p1 = el.points[0];
    const p2 = el.points[el.points.length - 1];

    switch (el.type) {
      case 'rect': {
        const rx = Math.min(p1.x, p2.x);
        const ry = Math.min(p1.y, p2.y);
        const rw = Math.abs(p2.x - p1.x);
        const rh = Math.abs(p2.y - p1.y);
        rc.rectangle(rx, ry, rw, rh, roughOptions);
        if (el.text) this._renderTextLabel(ctx, rx + rw / 2, ry + rh / 2, el.text, el);
        break;
      }

      case 'diamond': {
        const cx = (p1.x + p2.x) / 2;
        const cy = (p1.y + p2.y) / 2;
        const rw = Math.abs(p2.x - p1.x) / 2;
        const rh = Math.abs(p2.y - p1.y) / 2;
        const diamondPts = [
          [cx, cy - rh],
          [cx + rw, cy],
          [cx, cy + rh],
          [cx - rw, cy]
        ];
        rc.polygon(diamondPts, roughOptions);
        if (el.text) this._renderTextLabel(ctx, cx, cy, el.text, el);
        break;
      }

      case 'circle': {
        const cx = p1.x;
        const cy = p1.y;
        const radius = Math.hypot(p2.x - p1.x, p2.y - p1.y);
        rc.circle(cx, cy, radius * 2, roughOptions);
        if (el.text) this._renderTextLabel(ctx, cx, cy, el.text, el);
        break;
      }

      case 'line': {
        rc.line(p1.x, p1.y, p2.x, p2.y, roughOptions);
        break;
      }

      case 'arrow': {
        rc.line(p1.x, p1.y, p2.x, p2.y, roughOptions);
        const angle = Math.atan2(p2.y - p1.y, p2.x - p1.x);
        const headlen = Math.max(14, strokeWidth * 4);
        const a1 = {
          x: p2.x - headlen * Math.cos(angle - Math.PI / 6),
          y: p2.y - headlen * Math.sin(angle - Math.PI / 6)
        };
        const a2 = {
          x: p2.x - headlen * Math.cos(angle + Math.PI / 6),
          y: p2.y - headlen * Math.sin(angle + Math.PI / 6)
        };
        rc.polygon([[p2.x, p2.y], [a1.x, a1.y], [a2.x, a2.y]], {
          ...roughOptions,
          fill: stroke,
          fillStyle: 'solid'
        });
        break;
      }

      case 'pen':
      case 'eraser': {
        if (!el.points || el.points.length === 0) break;
        ctx.save();
        ctx.strokeStyle = el.type === 'eraser' ? '#0f172a' : stroke;
        ctx.fillStyle = el.type === 'eraser' ? '#0f172a' : stroke;
        ctx.lineWidth = strokeWidth;
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';

        // Single click draws dot
        if (el.points.length === 1) {
          ctx.beginPath();
          ctx.arc(el.points[0].x, el.points[0].y, strokeWidth / 2, 0, Math.PI * 2);
          ctx.fill();
        } else {
          // Smooth Bezier curve through points
          ctx.beginPath();
          ctx.moveTo(el.points[0].x, el.points[0].y);
          for (let i = 1; i < el.points.length - 1; i++) {
            const xc = (el.points[i].x + el.points[i + 1].x) / 2;
            const yc = (el.points[i].y + el.points[i + 1].y) / 2;
            ctx.quadraticCurveTo(el.points[i].x, el.points[i].y, xc, yc);
          }
          ctx.lineTo(el.points[el.points.length - 1].x, el.points[el.points.length - 1].y);
          ctx.stroke();
        }
        ctx.restore();
        break;
      }

      case 'text': {
        if (!el.text) break;
        ctx.save();
        const fontSize = el.fontSize || this.fontSize;
        ctx.font = `${fontSize}px 'Patrick Hand', 'Caveat', cursive, sans-serif`;
        ctx.fillStyle = stroke;
        ctx.textBaseline = 'top';
        const lines = el.text.split('\n');
        const lineHeight = fontSize * 1.3;
        for (let i = 0; i < lines.length; i++) {
          ctx.fillText(lines[i], p1.x, p1.y + i * lineHeight);
        }
        ctx.restore();
        break;
      }
    }
  }

  _renderTextLabel(ctx, cx, cy, text, el) {
    ctx.save();
    const fontSize = el.fontSize || 18;
    ctx.font = `${fontSize}px 'Patrick Hand', 'Caveat', cursive, sans-serif`;
    ctx.fillStyle = el.strokeColor || '#f8fafc';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const lines = text.split('\n');
    const lineHeight = fontSize * 1.25;
    const startY = cy - ((lines.length - 1) * lineHeight) / 2;
    for (let i = 0; i < lines.length; i++) {
      ctx.fillText(lines[i], cx, startY + i * lineHeight);
    }
    ctx.restore();
  }

  _renderSelection(ctx) {
    if (this.selectedIds.size === 0) return;
    const selected = this.elements.filter(el => this.selectedIds.has(el.id));
    const b = this.getCombinedBounds(selected);
    if (!b) return;

    const pad = 8 / this.scale;
    const handleSize = 8 / this.scale;

    ctx.save();
    // Dashed selection rectangle
    ctx.strokeStyle = '#38bdf8';
    ctx.lineWidth = 1.5 / this.scale;
    ctx.setLineDash([5 / this.scale, 5 / this.scale]);
    ctx.strokeRect(b.x - pad, b.y - pad, b.w + pad * 2, b.h + pad * 2);

    // Draw handles if exactly 1 element is selected
    if (selected.length === 1) {
      ctx.setLineDash([]);
      ctx.fillStyle = '#0f172a';
      ctx.strokeStyle = '#38bdf8';
      ctx.lineWidth = 1.5 / this.scale;

      const handles = [
        { x: b.x - pad, y: b.y - pad },
        { x: b.x + b.w + pad, y: b.y - pad },
        { x: b.x + b.w + pad, y: b.y + b.h + pad },
        { x: b.x - pad, y: b.y + b.h + pad },
        { x: b.x + b.w / 2, y: b.y - pad },
        { x: b.x + b.w / 2, y: b.y + b.h + pad },
        { x: b.x - pad, y: b.y + b.h / 2 },
        { x: b.x + b.w + pad, y: b.y + b.h / 2 }
      ];

      for (const h of handles) {
        ctx.fillRect(h.x - handleSize / 2, h.y - handleSize / 2, handleSize, handleSize);
        ctx.strokeRect(h.x - handleSize / 2, h.y - handleSize / 2, handleSize, handleSize);
      }
    }

    ctx.restore();
  }

  // --- High-Resolution Bounding Box Export for OCR ---

  exportImageBase64() {
    if (this.elements.length === 0) return null;
    const bounds = this.getCombinedBounds(this.elements);
    if (!bounds) return null;

    const pad = 60;
    const minX = bounds.x - pad;
    const minY = bounds.y - pad;
    const w = Math.max(300, Math.ceil(bounds.w + pad * 2));
    const h = Math.max(300, Math.ceil(bounds.h + pad * 2));

    const offCanvas = document.createElement('canvas');
    offCanvas.width = w;
    offCanvas.height = h;
    const offCtx = offCanvas.getContext('2d');
    const offRc = rough.canvas(offCanvas);

    // Clean dark background for vision model clarity
    offCtx.fillStyle = '#0f172a';
    offCtx.fillRect(0, 0, w, h);

    offCtx.save();
    offCtx.translate(-minX, -minY);

    const originalRc = this.rc;
    this.rc = offRc;

    for (const el of this.elements) {
      this._renderElement(offCtx, el);
    }

    this.rc = originalRc;
    offCtx.restore();

    return offCanvas.toDataURL('image/png');
  }
}
