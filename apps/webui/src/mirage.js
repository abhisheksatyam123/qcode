// Frontend client for the local Cactus Mirage vision bridge.
//
// Mirage sees images: scene, regions, OCR text, relations, reasoning. Spiddy uses it to
// comment on an attached image ("let me take a look"). It is optional and local; with no
// endpoint configured nothing is requested and the UI stays unchanged.
//
// Mirage is NOT a sentiment model; the animation label still comes from emotions.js
// (rules) or sentiment-llm.js (small text model). See docs/cactus-spiddy.md.
export const MIRAGE_ENDPOINT_KEY = 'qcode-mirage-endpoint';
export const DEFAULT_MIRAGE_TIMEOUT_MS = 45000;
export const MAX_IMAGE_BYTES = 8 * 1024 * 1024;
export const MIRAGE_WASM_BASE_KEY = 'qcode-mirage-wasm-base';

// In-browser mode: the Cactus engine compiled to WASM (cactus.js + cactus.wasm) plus the
// 20.6 MB mirage.cact weights, all served as static files. Verified in Chromium: engine
// 0.1 s, weights load 0.3 s, one 512px describe ~17 s, no server process.
const WASM = { state: 'idle', mod: null, error: null, promise: null };

export function getMirageEndpoint() {
  try {
    if (typeof localStorage === 'undefined') return '';
    return localStorage.getItem(MIRAGE_ENDPOINT_KEY) || '';
  } catch (_) {
    return '';
  }
}

export function setMirageEndpoint(url) {
  try {
    if (typeof localStorage === 'undefined') return false;
    if (url) localStorage.setItem(MIRAGE_ENDPOINT_KEY, String(url));
    else localStorage.removeItem(MIRAGE_ENDPOINT_KEY);
    return true;
  } catch (_) {
    return false;
  }
}

export function getMirageWasmBase() {
  try {
    if (typeof localStorage === 'undefined') return '';
    return localStorage.getItem(MIRAGE_WASM_BASE_KEY) || '';
  } catch (_) {
    return '';
  }
}

export function setMirageWasmBase(url) {
  try {
    if (typeof localStorage === 'undefined') return false;
    if (url) localStorage.setItem(MIRAGE_WASM_BASE_KEY, String(url));
    else localStorage.removeItem(MIRAGE_WASM_BASE_KEY);
    return true;
  } catch (_) {
    return false;
  }
}

// A bridge endpoint OR a wasm asset base makes Mirage usable.
export function mirageAvailable() { return !!getMirageEndpoint() || !!getMirageWasmBase(); }
export function mirageWasmReady() { return WASM.state === 'ready'; }

// Rough byte size of a data URL payload, without decoding it.
export function dataUrlBytes(dataUrl) {
  if (typeof dataUrl !== 'string') return 0;
  const comma = dataUrl.indexOf(',');
  if (comma === -1) return 0;
  const b64 = dataUrl.slice(comma + 1);
  const padding = b64.endsWith('==') ? 2 : b64.endsWith('=') ? 1 : 0;
  return Math.max(0, Math.floor((b64.length * 3) / 4) - padding);
}

function loadScriptOnce(src) {
  return new Promise((resolve, reject) => {
    const existing = document.querySelector('script[data-mirage-src="' + src + '"]');
    if (existing) {
      if (existing.dataset.loaded === '1') { resolve(); return; }
      existing.addEventListener('load', () => resolve(), { once: true });
      existing.addEventListener('error', () => reject(new Error('cactus.js failed to load')), { once: true });
      return;
    }
    const el = document.createElement('script');
    el.src = src;
    el.async = true;
    el.dataset.mirageSrc = src;
    el.addEventListener('load', () => { el.dataset.loaded = '1'; resolve(); }, { once: true });
    el.addEventListener('error', () => reject(new Error('cactus.js failed to load')), { once: true });
    document.head.appendChild(el);
  });
}

// Boot the WASM engine and load mirage.cact once per page. Returns the Emscripten module.
export async function loadMirageWasm(opts = {}) {
  const base = String(opts.base || getMirageWasmBase() || '').replace(/\/+$/, '');
  if (!base || typeof document === 'undefined' || typeof window === 'undefined') return null;
  if (WASM.state === 'ready' && WASM.mod) return WASM.mod;
  if (WASM.state === 'loading') return WASM.promise;
  WASM.state = 'loading';
  WASM.promise = (async () => {
    try {
      await loadScriptOnce(base + '/cactus.js');
      const factory = window.createCactus;
      if (typeof factory !== 'function') throw new Error('createCactus missing');
      const mod = await factory();
      if (mod._cactus_set_threads) mod._cactus_set_threads(1);
      const res = await fetch(base + '/mirage.cact');
      if (!res.ok) throw new Error('mirage.cact ' + res.status);
      const bytes = new Uint8Array(await res.arrayBuffer());
      if (!bytes.length) throw new Error('mirage.cact is empty');
      const ptr = mod._malloc(bytes.length);
      mod.HEAPU8.set(bytes, ptr);
      const rc = mod._cactus_load(ptr, BigInt(bytes.length));
      mod._free(ptr);
      if (rc !== 0) {
        const err = mod._cactus_last_error ? mod.UTF8ToString(mod._cactus_last_error()) : '';
        throw new Error('cactus_load rc=' + rc + (err ? ' ' + err : ''));
      }
      WASM.mod = mod;
      WASM.state = 'ready';
      return mod;
    } catch (e) {
      WASM.state = 'error';
      WASM.error = e;
      return null;
    }
  })();
  return WASM.promise;
}

// Draw the data URL at <= maxWidth and run cactus_describe inside the engine.
export async function describeWithWasm(dataUrl, opts = {}) {
  if (typeof document === 'undefined' || typeof Image === 'undefined') return null;
  const mod = await loadMirageWasm(opts);
  if (!mod) return null;
  let img;
  try {
    img = new Image();
    img.src = dataUrl;
    if (img.decode) await img.decode();
    else await new Promise((res, rej) => { img.onload = res; img.onerror = rej; });
  } catch (_) {
    return null;
  }
  const maxW = Number(opts.maxWidth) > 0 ? Number(opts.maxWidth) : 512;
  const scale = Math.min(1, maxW / (img.naturalWidth || img.width || maxW));
  const w = Math.max(1, Math.round((img.naturalWidth || img.width) * scale));
  const h = Math.max(1, Math.round((img.naturalHeight || img.height) * scale));
  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d');
  if (!ctx) return null;
  ctx.drawImage(img, 0, 0, w, h);
  const rgba = ctx.getImageData(0, 0, w, h).data;
  const rgb = new Uint8Array(w * h * 3);
  for (let i = 0, j = 0; i < rgba.length; i += 4) { rgb[j++] = rgba[i]; rgb[j++] = rgba[i + 1]; rgb[j++] = rgba[i + 2]; }
  const cap = Number(opts.outCapacity) > 0 ? Number(opts.outCapacity) : (1 << 20);
  const rgbPtr = mod._malloc(rgb.length);
  const outPtr = mod._malloc(cap);
  if (!rgbPtr || !outPtr) { if (rgbPtr) mod._free(rgbPtr); if (outPtr) mod._free(outPtr); return null; }
  mod.HEAPU8.set(rgb, rgbPtr);
  try {
    const started = (typeof performance !== 'undefined' ? performance.now() : Date.now());
    const rc = mod._cactus_describe(rgbPtr, w, h, 0, 0, 0, outPtr, cap);
    const tookMs = Math.round((typeof performance !== 'undefined' ? performance.now() : Date.now()) - started);
    if (rc <= 0) return null;
    const text = mod.UTF8ToString(outPtr);
    let result = null;
    try { result = JSON.parse(text); } catch (_) { result = text; }
    const payload = { result };
    const summary = summarizeMirage(payload);
    if (!summary) return null;
    return { summary, result, tookMs, model: 'mirage-wasm' };
  } finally {
    mod._free(rgbPtr);
    mod._free(outPtr);
  }
}

// Compact, readable line from either the bridge's summary or a raw Mirage result.
export function summarizeMirage(payload) {
  if (!payload) return '';
  if (typeof payload.summary === 'string' && payload.summary.trim()) return payload.summary.trim();
  const result = payload.result || payload;
  if (typeof result === 'string') return result.trim();
  if (!result || typeof result !== 'object') return '';
  const parts = [];
  const scene = result.scene;
  if (scene && typeof scene === 'object') {
    for (const key of ['setting', 'background', 'style']) {
      if (scene[key]) parts.push(String(scene[key]));
    }
  }
  if (Array.isArray(result.regions)) {
    const names = result.regions.slice(0, 5).map(r => r && r.name).filter(Boolean);
    if (names.length) parts.push(names.join(', '));
  }
  if (Array.isArray(result.text)) {
    const strings = result.text.slice(0, 3).map(t => t && t.string).filter(Boolean);
    if (strings.length) parts.push('text: ' + strings.map(s => s.slice(0, 60)).join(' / '));
  }
  if (result.reasoning) parts.push(String(result.reasoning).slice(0, 220));
  return parts.join(' · ');
}

// Describe an image data URL. Never throws: null means "show the fallback message".
export async function describeImage(dataUrl, opts = {}) {
  if (typeof dataUrl !== 'string' || !dataUrl.startsWith('data:image/')) return null;
  if (dataUrlBytes(dataUrl) > MAX_IMAGE_BYTES) return null;

  const endpoint = opts.endpoint !== undefined ? opts.endpoint : getMirageEndpoint();
  if (!endpoint) {
    // No bridge: use the in-browser WASM engine when its assets are configured.
    const base = opts.wasmBase !== undefined ? opts.wasmBase : getMirageWasmBase();
    if (!base) return null;
    return describeWithWasm(dataUrl, { ...opts, base });
  }

  const fetchImpl = opts.fetchImpl || (typeof fetch === 'function' ? fetch : null);
  if (!fetchImpl) return null;

  const timeoutMs = Number.isFinite(opts.timeoutMs) ? opts.timeoutMs : DEFAULT_MIRAGE_TIMEOUT_MS;
  const controller = typeof AbortController === 'function' ? new AbortController() : null;
  const timer = controller ? setTimeout(() => controller.abort(), timeoutMs) : null;

  try {
    const res = await fetchImpl(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ image: dataUrl }),
      signal: controller ? controller.signal : undefined,
    });
    if (!res || !res.ok) return null;
    const payload = await res.json();
    if (!payload || payload.ok === false) return null;
    const summary = summarizeMirage(payload);
    if (!summary) return null;
    return {
      summary,
      result: payload.result || null,
      tookMs: Number.isFinite(payload.took_ms) ? payload.took_ms : null,
      model: payload.model || 'mirage',
    };
  } catch (_) {
    return null;
  } finally {
    if (timer) clearTimeout(timer);
  }
}
