// Optional small-LLM sentiment for Spiddy's animation choice.
//
// The rule-based classifier in emotions.js is the always-available default. When a local
// model endpoint is configured, its label refines the clip: the label is validated against
// EMOTIONS/INTENSITIES and then resolved to a real animation row from CATALOG.
//
// NOTE ON MODEL CHOICE: Cactus-Compute/mirage is a *vision* model
// (pipeline_tag: image-text-to-text; object detection, segmentation, OCR). It cannot
// classify text sentiment. Point this adapter at a small *text* model instead; see
// docs/spiddy-sentiment.md. Any server that answers POST {endpoint} with an emotion label
// works, including tools/spiddy_sentiment_bridge.py.
import { EMOTIONS, INTENSITIES, clipForEmotion } from './emotions.js';

export const SENTIMENT_ENDPOINT_KEY = 'qcode-sentiment-endpoint';
export const DEFAULT_TIMEOUT_MS = 1500;

const LABELS = EMOTIONS;
const cache = new Map();
const MAX_CACHE = 200;

export function getEndpoint() {
  try {
    if (typeof localStorage === 'undefined') return '';
    return localStorage.getItem(SENTIMENT_ENDPOINT_KEY) || '';
  } catch (_) {
    return '';
  }
}

export function setEndpoint(url) {
  try {
    if (typeof localStorage === 'undefined') return false;
    if (url) localStorage.setItem(SENTIMENT_ENDPOINT_KEY, String(url));
    else localStorage.removeItem(SENTIMENT_ENDPOINT_KEY);
    return true;
  } catch (_) {
    return false;
  }
}

// Accept the shapes a small model or wrapper might return and normalise to our vocabulary.
// Returns null when nothing usable is present, so callers keep the rule-based label.
export function parseSentiment(raw) {
  if (!raw) return null;
  let value = raw;

  if (typeof value === 'string') {
    const text = value.trim();
    const json = text.match(/\{[\s\S]*\}/);
    if (json) {
      try { value = JSON.parse(json[0]); } catch (_) { value = text; }
    }
  }

  if (typeof value === 'string') {
    // "happy", "happy:high", "emotion: excited", or a line with ANSI colours around it.
    const tokens = value.replace(/\u001b\[[0-9;]*m/g, ' ').toLowerCase().split(/[^a-z]+/).filter(Boolean);
    const emotion = tokens.find(t => LABELS.includes(t));
    if (!emotion) return null;
    const intensity = tokens.find(t => INTENSITIES.includes(t)) || 'mid';
    return { emotion, intensity, confidence: null };
  }

  if (typeof value !== 'object') return null;

  const rawEmotion = value.emotion || value.label || value.sentiment || value.result || '';
  const emotion = String(rawEmotion).toLowerCase().trim();
  if (!LABELS.includes(emotion)) {
    // Some models answer with the top class of a label/scores pair.
    if (Array.isArray(value.labels) && Array.isArray(value.scores)) {
      const best = value.scores.indexOf(Math.max(...value.scores));
      const label = String(value.labels[best] || '').toLowerCase();
      if (!LABELS.includes(label)) return null;
      return { emotion: label, intensity: 'mid', confidence: Number(value.scores[best]) || null };
    }
    return null;
  }
  const intensity = INTENSITIES.includes(String(value.intensity || '').toLowerCase())
    ? String(value.intensity).toLowerCase()
    : 'mid';
  const confidence = Number.isFinite(Number(value.confidence)) ? Number(value.confidence) : null;
  return { emotion, intensity, confidence };
}

// Ask the configured endpoint for a label. Never throws; null means "keep the rule label".
export async function classifySentiment(text, toolEvents = [], opts = {}) {
  const endpoint = opts.endpoint !== undefined ? opts.endpoint : getEndpoint();
  if (!endpoint) return null;

  const body = String(text || '').slice(0, 4000);
  if (!body.trim()) return null;

  const key = endpoint + '|' + body;
  if (!opts.noCache && cache.has(key)) return cache.get(key);

  const fetchImpl = opts.fetchImpl || (typeof fetch === 'function' ? fetch : null);
  if (!fetchImpl) return null;

  const timeoutMs = Number.isFinite(opts.timeoutMs) ? opts.timeoutMs : DEFAULT_TIMEOUT_MS;
  const controller = typeof AbortController === 'function' ? new AbortController() : null;
  const timer = controller ? setTimeout(() => controller.abort(), timeoutMs) : null;

  try {
    const res = await fetchImpl(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        text: body,
        labels: LABELS,
        intensities: INTENSITIES,
        toolEvents: (Array.isArray(toolEvents) ? toolEvents : []).slice(-8).map(t => ({
          tool_name: t && (t.tool_name || t.tool), status: t && t.status,
        })),
      }),
      signal: controller ? controller.signal : undefined,
    });
    if (!res || !res.ok) return null;
    const payload = await res.json();
    const parsed = parseSentiment(payload);
    if (!parsed) return null;
    const result = {
      ...parsed,
      source: (payload && payload.source) || 'llm',
      model: (payload && payload.model) || null,
    };
    if (!opts.noCache) {
      if (cache.size >= MAX_CACHE) cache.clear();
      cache.set(key, result);
    }
    return result;
  } catch (_) {
    return null;
  } finally {
    if (timer) clearTimeout(timer);
  }
}

// Rule clip + model label -> a real catalog row. Invalid labels are ignored.
export function refineClip(clip = {}, label = null, text = '') {
  if (!label || !LABELS.includes(label.emotion)) return clip;
  const intensity = INTENSITIES.includes(label.intensity) ? label.intensity : clip.intensity;
  return clipForEmotion({
    pose: clip.pose || 'think',
    emotion: label.emotion,
    intensity,
    text: text || clip.text || '',
  });
}

export function clearSentimentCache() { cache.clear(); }
