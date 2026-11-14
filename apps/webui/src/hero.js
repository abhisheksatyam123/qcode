// Spiddy: the animated hero character for the "spiddy" theme.
// Pure functions only (no DOM access) so they can be imported and tested directly.
import { EMOTIONS, INTENSITIES, MOTIONS } from './emotions.js';

// Pose -> caption shown under the character.
export const HERO_CAPTIONS = {
  intro: "Hi, I'm Spiddy",
  think: 'Thinking',
  computer: 'Using the computer',
  phone: 'Checking the web',
  tools: 'Running tools',
  build: 'Building',
};

const HANGING_POSES = new Set(['intro']);

// Whole-character routines (CSS in style.css, `.hero-act-<name>`). `hang` is an act (arms out
// on the thread), separate from the intro pose `hero-hanging`.
export const HERO_ACTS = ['run', 'swing', 'crawl', 'thwip', 'backflip', 'dance', 'sit', 'peek',
  'hero', 'cheer', 'salute', 'sneak', 'moonwalk', 'nap', 'spin', 'wave',
  'hang', 'glide', 'batarang', 'brood', 'flight', 'unibeam', 'smash', 'thunderclap',
  'rage', 'leap', 'masterbuild', 'rock'];

// Text helpers: captions and bubbles can come from the personality layer, not just constants.
const ESC = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
const esc = v => String(v).replace(/[&<>"']/g, c => ESC[c]);

export const HERO_EMOTIONS = EMOTIONS;
const POSE_EMOTION = { intro: 'happy', think: 'focused', computer: 'focused', phone: 'happy', tools: 'focused', build: 'proud' };

// Emotion from the latest tool outcome: a failure looks worried, a success looks happy,
// a running tool looks focused, otherwise the pose's default.
export function heroEmotionForToolEvents(toolEvents) {
  const list = Array.isArray(toolEvents) ? toolEvents.filter(Boolean) : [];
  const last = list[list.length - 1];
  if (last && last.status === 'error') return 'worried';
  if (last && last.status === 'success') return 'happy';
  if (last && last.status === 'running') return 'focused';
  return POSE_EMOTION.think;
}

// Tool name -> pose. Unknown tools fall back to 'tools'.
const TOOL_POSES = {
  read: 'computer', grep: 'computer', glob: 'computer', ls: 'computer',
  search: 'computer',
  websearch: 'phone', web_search: 'phone', webfetch: 'phone', web_fetch: 'phone',
  bash: 'tools', shell: 'tools', run_command: 'tools',
  edit: 'build', write: 'build', multiedit: 'build', apply_patch: 'build', patch: 'build',
  task: 'build', dispatch_agent: 'build',
};

export function heroPoseForTool(toolName) {
  const key = String(toolName || '').toLowerCase();
  return TOOL_POSES[key] || 'tools';
}

// The most recent running tool sets the pose. No running tool means Spiddy is thinking.
export function heroPoseForToolEvents(toolEvents) {
  const running = (Array.isArray(toolEvents) ? toolEvents : []).filter(t => t && t.status === 'running');
  if (running.length === 0) return 'think';
  return heroPoseForTool(running[running.length - 1].tool_name);
}

// Markup for the character. `opts.bubble` adds a speech bubble (used for the greeting).
// Every prop is always present; CSS shows only the ones that match the pose.
export function heroMarkup(pose, opts = {}) {
  const safePose = Object.prototype.hasOwnProperty.call(HERO_CAPTIONS, pose) ? pose : 'think';
  // The caption is the one line Spiddy says. app.js passes a personality line for live
  // replies; without one the neutral pose caption is used.
  const caption = opts.caption ? String(opts.caption) : HERO_CAPTIONS[safePose];
  const emotion = HERO_EMOTIONS.includes(opts.emotion) ? opts.emotion : (POSE_EMOTION[safePose] || 'focused');
  const intensity = INTENSITIES.includes(opts.intensity) ? opts.intensity : 'mid';
  const motion = MOTIONS.includes(opts.motion) ? opts.motion : 'idle';
  const hanging = HANGING_POSES.has(safePose) ? ' hero-hanging' : '';
  const act = HERO_ACTS.includes(opts.act) ? ' hero-act-' + opts.act : '';
  const bubble = opts.bubble
    ? '<div class="hero-bubble">' + esc(opts.bubble) + '</div>'
    : '';
  return (
    '<div class="hero hero-' + safePose + hanging + ' hero-emo-' + emotion + ' hero-int-' + intensity + ' hero-motion-' + motion + act + '" role="status" aria-label="' + esc(caption) + '">' +
      '<div class="hero-stage">' +
        '<div class="hero-thread"></div>' +
        '<div class="hero-spidey">' +
          '<div class="hero-hat"></div>' +
          '<div class="hero-head"><div class="hero-eye-left"></div><div class="hero-eye-right"></div>' +
          '<div class="hero-brow hero-brow-l"></div><div class="hero-brow hero-brow-r"></div><div class="hero-mouth"></div></div>' +
          '<div class="hero-body">' +
            '<div class="hero-spider"></div>' +
            '<div class="hero-arm-left"></div>' +
            '<div class="hero-arm-right"></div>' +
          '</div>' +
          '<div class="hero-legs"><div class="hero-boot-left"></div><div class="hero-boot-right"></div></div>' +
        '</div>' +
        '<div class="hero-bulb"></div>' +
        '<div class="hero-laptop"></div>' +
        '<div class="hero-handset"></div>' +
        '<div class="hero-wrench"></div>' +
        '<div class="hero-hammer"></div>' +
        bubble +
      '</div>' +
      '<div class="hero-caption">' + esc(caption) + '<span class="hero-dots"><i></i><i></i><i></i></span></div>' +
    '</div>'
  );
}
