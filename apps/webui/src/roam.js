// Roaming Spiddy: the mascot that wanders the chat window while the Spiddy theme is on.
// Pure planning only (no DOM): app.js owns the element and the timer, and passes in
// the state, a random source, and the chat box size. Tests: test/roam.test.js.
import { heroPoseForToolEvents, heroEmotionForToolEvents, HERO_ACTS } from './hero.js';
import { lineFor } from './personality.js';

export const ROAM_MODES = ['idle', 'think', 'tools', 'success', 'error'];
// Spiddy's footprint in px: the hero plus its caption (style.css .mascot-roam is 160px wide).
export const ROAM_SIZE = { mascotWidth: 160, mascotHeight: 150 };
export const ROAM_MARGIN = 8;          // px kept between Spiddy and the chat edge
export const ROAM_MIN_TRAVEL = 0.3;    // a new spot must be this far (fraction of box diagonal) from the last

// Which mode the chat is in. Running tool calls win over plain generation; after a turn,
// the last tool outcome sets a success or error mood until the next message.
export function roamModeFor({ generating = false, toolEvents = [] } = {}) {
  const list = Array.isArray(toolEvents) ? toolEvents.filter(Boolean) : [];
  if (list.some(t => t.status === 'running')) return 'tools';
  if (generating) return 'think';
  const last = list[list.length - 1];
  if (last && last.status === 'error') return 'error';
  if (last && last.status === 'success') return 'success';
  return 'idle';
}

// What Spiddy shows in a mode. No randomness except the caption, which is stable per seed.
export function roamStateFor({ mode = 'idle', toolEvents = [], seed = '' } = {}) {
  const m = ROAM_MODES.includes(mode) ? mode : 'idle';
  if (m === 'tools') {
    return {
      pose: heroPoseForToolEvents(toolEvents),
      emotion: heroEmotionForToolEvents(toolEvents),
      motion: 'bounce',
      caption: lineFor('tools', seed + ':tools'),
    };
  }
  if (m === 'think') {
    return { pose: 'think', emotion: 'focused', motion: 'bounce', caption: lineFor('think', seed + ':think') };
  }
  if (m === 'success') {
    return { pose: 'build', emotion: 'proud', motion: 'bounce', caption: lineFor('success', seed + ':success') };
  }
  if (m === 'error') {
    return { pose: 'tools', emotion: 'worried', motion: 'shake', caption: lineFor('error', seed + ':error') };
  }
  return { pose: 'think', emotion: 'happy', motion: 'sway', caption: lineFor('idle', seed + ':idle') };
}

// Delay until Spiddy moves again. Busy chats move a little less often so it does not
// pull attention away from the tool output.
export function roamDelayMs(rand = Math.random, mode = 'idle') {
  const ranges = { idle: [6000, 11000], think: [9000, 14000], tools: [10000, 16000] };
  const [lo, hi] = ranges[mode] || ranges.idle;
  return Math.round(lo + rand() * (hi - lo));
}

// Acts per busy mode. Idle uses the theme's own pool (THEME_POOLS) instead.
export const ROAM_POOLS = {
  think: ['sit', 'nap', 'brood'],
  tools: ['thwip', 'run', 'peek'],
  success: ['cheer', 'salute', 'hero'],
  error: ['sit', 'peek'],
};
// Idle acts per hero theme (the acts each suit's move set has, original CSS).
export const THEME_POOLS = {
  'spiddy': ['run', 'swing', 'crawl', 'thwip', 'backflip', 'hero', 'hang', 'dance', 'sit', 'peek', 'nap', 'sneak', 'salute'],
  'spiddy-night': ['glide', 'batarang', 'swing', 'brood', 'hero', 'run', 'sneak', 'salute', 'sit', 'peek', 'nap'],
  'spiddy-steel': ['flight', 'unibeam', 'thwip', 'hero', 'cheer', 'dance', 'salute', 'run', 'moonwalk', 'peek', 'nap'],
  'spiddy-jade': ['smash', 'thunderclap', 'rage', 'leap', 'run', 'hero', 'cheer', 'dance', 'sit', 'peek', 'nap'],
  'spiddy-brick': ['masterbuild', 'rock', 'glide', 'batarang', 'dance', 'hero', 'run', 'salute', 'peek', 'nap'],
};

// The acts Spiddy may pick in a mode for a theme. Unknown themes fall back to all acts.
export function actPoolFor(mode = 'idle', theme = '') {
  const pool = mode === 'idle' ? THEME_POOLS[theme] : ROAM_POOLS[mode];
  const list = pool || HERO_ACTS;
  return list.filter(a => HERO_ACTS.includes(a));
}

// A random act from a pool (default: all acts). Never the same act twice in a row.
export function pickRoamAct(rand = Math.random, prev = null, pool = HERO_ACTS) {
  const list = prev ? pool.filter(a => a !== prev) : pool;
  const from = list.length ? list : pool;
  if (from.length === 0) return null;
  return from[Math.min(from.length - 1, Math.floor(rand() * from.length))];
}

// A random top-left corner (px, relative to the chat box) for Spiddy. `box` gives the chat
// size and Spiddy's own size. The spot is pulled away from the previous one so every move is
// visible; after a few tries it accepts whatever it has.
export function pickRoamSpot(rand = Math.random, box, prev = null) {
  const maxX = Math.max(0, box.width - box.mascotWidth - 2 * ROAM_MARGIN);
  const maxY = Math.max(0, box.height - box.mascotHeight - 2 * ROAM_MARGIN);
  const diag = Math.hypot(maxX, maxY) || 1;
  let spot = null;
  for (let i = 0; i < 8; i++) {
    spot = { left: ROAM_MARGIN + Math.round(rand() * maxX), top: ROAM_MARGIN + Math.round(rand() * maxY) };
    if (!prev || Math.hypot(spot.left - prev.left, spot.top - prev.top) >= ROAM_MIN_TRAVEL * diag) break;
  }
  return spot;
}

// One relocation: where to go, what to do there, and when to move again.
// Every mode gets an act from its pool (idle: the theme's pool). `reduced`
// (prefers-reduced-motion) keeps Spiddy in place and skips acts.
export function planRoam({ rand = Math.random, mode = 'idle', theme = '', box, prev = null, prevAct = null, reduced = false, pool = null } = {}) {
  const spot = reduced && prev ? prev : pickRoamSpot(rand, box, prev);
  const acts = Array.isArray(pool) ? pool : actPoolFor(mode, theme);
  const act = reduced ? null : pickRoamAct(rand, prevAct, acts);
  return { spot, act, delayMs: roamDelayMs(rand, mode) };
}
