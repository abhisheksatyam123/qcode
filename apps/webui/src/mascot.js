// Mascot attach API: mount Spiddy into any element and drive it with a small state object.
// The pure helpers (normalizeMascotState, mascotMarkup, mascotStateFromEvents) have no DOM
// access and are unit-tested directly. attachMascot is the only DOM-touching function.
// Contract: docs/mascot.md.
import { heroMarkup, heroPoseForToolEvents, HERO_ACTS } from './hero.js';
import { clipForEmotion, sentimentFor, POSES, EMOTIONS, INTENSITIES, MOTIONS } from './emotions.js';

export const MASCOT_CAPTION_MAX = 140;
export const MASCOT_BUBBLE_MAX = 200;
export const MASCOT_DEFAULT_POSE = 'think';

const MOUNT_CLASS = 'mascot-mount';
// One controller per container, so attaching twice replaces the first mount.
const MOUNTS = new WeakMap();

function clampText(value, max) {
  if (value === null || value === undefined) return null;
  const text = String(value).replace(/\s+/g, ' ').trim();
  if (!text) return null;
  return text.length > max ? text.slice(0, max - 1) + '…' : text;
}

// Turns any partial input into a valid, catalog-backed state. Unknown values fall back, never throw.
// The clip goes through clipForEmotion, so the pose/emotion/intensity/motion combination always
// exists in the 864-row catalog. The text (optional) picks a stable motion variant.
export function normalizeMascotState(input = {}) {
  const src = input && typeof input === 'object' ? input : {};
  const clip = clipForEmotion({
    pose: POSES.includes(src.pose) ? src.pose : MASCOT_DEFAULT_POSE,
    emotion: EMOTIONS.includes(src.emotion) ? src.emotion : undefined,
    intensity: INTENSITIES.includes(src.intensity) ? src.intensity : undefined,
    text: src.text ? String(src.text) : '',
  });
  // The catalog holds all four motions for every pose/emotion/intensity, so a valid
  // requested motion always resolves to a real clip.
  const motion = MOTIONS.includes(src.motion) ? src.motion : clip.motion;
  return {
    pose: clip.pose,
    emotion: clip.emotion,
    intensity: clip.intensity,
    motion,
    act: HERO_ACTS.includes(src.act) ? src.act : null,
    caption: clampText(src.caption, MASCOT_CAPTION_MAX),
    bubble: clampText(src.bubble, MASCOT_BUBBLE_MAX),
  };
}

// The markup for a normalized (or raw) state. Captions and bubbles are escaped by hero.js.
export function mascotMarkup(state) {
  const s = normalizeMascotState(state);
  return heroMarkup(s.pose, {
    emotion: s.emotion,
    intensity: s.intensity,
    motion: s.motion,
    act: s.act || undefined,
    caption: s.caption || undefined,
    bubble: s.bubble || undefined,
  });
}

// Derives a state from what the chat knows: the message text and its tool events.
// Pose comes from the running tool unless the caller pins one; emotion and intensity come
// from the rule classifier (sentimentFor). Pure: no model call.
export function mascotStateFromEvents({ text = '', toolEvents = [], pose, caption, bubble } = {}) {
  const sentiment = sentimentFor(text, toolEvents);
  return normalizeMascotState({
    pose: POSES.includes(pose) ? pose : heroPoseForToolEvents(toolEvents),
    emotion: sentiment.emotion,
    intensity: sentiment.intensity,
    text,
    caption,
    bubble,
  });
}

// Mounts Spiddy inside `container` and returns a controller:
//   { el, state, update(patch), say(text), destroy() }
// `options.state` is the initial state (see normalizeMascotState). The host must set
// <html data-hero="spiddy"> for the theme CSS to draw the character; this function does not
// change the theme.
export function attachMascot(container, options = {}) {
  if (!container || typeof container.appendChild !== 'function') {
    throw new TypeError('attachMascot: container must be a DOM element');
  }
  const previous = MOUNTS.get(container);
  if (previous) previous.destroy();

  const doc = container.ownerDocument || (typeof document !== 'undefined' ? document : null);
  if (!doc) throw new TypeError('attachMascot: no document available');

  const el = doc.createElement('div');
  el.className = MOUNT_CLASS + (options.className ? ' ' + String(options.className) : '');
  el.dataset.mascot = 'spiddy';

  let state = normalizeMascotState(options.state || {});
  let destroyed = false;

  const render = () => { el.innerHTML = mascotMarkup(state); };
  render();
  container.appendChild(el);

  const controller = {
    el,
    get state() { return { ...state }; },
    update(patch = {}) {
      if (destroyed) return this.state;
      const p = patch && typeof patch === 'object' ? patch : {};
      // Fields the patch does not mention keep their current value. Changing the clip
      // (pose, emotion, intensity or text) re-picks the motion unless the patch sets one.
      const merged = { ...state, ...p };
      const changesClip = ['pose', 'emotion', 'intensity', 'text'].some(k => Object.prototype.hasOwnProperty.call(p, k));
      if (changesClip && !Object.prototype.hasOwnProperty.call(p, 'motion')) delete merged.motion;
      state = normalizeMascotState(merged);
      render();
      return this.state;
    },
    say(text) {
      return this.update({ caption: text });
    },
    destroy() {
      if (destroyed) return;
      destroyed = true;
      if (el.parentNode) el.parentNode.removeChild(el);
      if (MOUNTS.get(container) === controller) MOUNTS.delete(container);
    },
  };
  MOUNTS.set(container, controller);
  return controller;
}
