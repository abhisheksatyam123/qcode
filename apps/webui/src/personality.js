// Spiddy's personality: a voice and mood layer that is separate from the assistant's.
// The assistant writes the answer; Spiddy comments on the work. Pure functions only.
//
// Design rules:
//  - Kid-hero energy: short, warm, a little cocky, never mean.
//  - Spiddy never claims credit for the assistant's work and never pretends to be a person.
//  - One line at a time. The caption is 1.5rem of space, not a paragraph.
//  - No borrowed slogans or catchphrases: everything here is original.

export const SPIDDY = {
  name: 'Spiddy',
  tagline: 'Small spider. Big opinions.',
};

// Each list is rotated by a stable hash, so the same event always says the same thing
// (no flicker when a message re-renders) but different events vary.
export const VOICE = {
  greeting: [
    'Dropping in! Point me at something.',
    'Spiddy on the ceiling, ready to help.',
    'Webs ready. What are we building?',
    'Hi! I hang around and narrate. Ask away.',
  ],
  idle: [
    'Just hanging out.',
    'Ceiling inspection: all clear.',
    'I can wait. I have webs.',
    'Idle. Suspiciously idle.',
  ],
  think: [
    'Hmm. Let me think.',
    'Spinning up a plan...',
    'Working it out.',
    'Give me a sec, I am thinking with all eight legs.',
    'Something is sticky here. Thinking.',
  ],
  computer: [
    'Reading. Actually reading.',
    'Found the file. Eyes on it.',
    'Skimming the source. No shortcuts.',
    'Grepping around.',
  ],
  phone: [
    'Checking the web. The other web.',
    'Looking this up.',
    'Fetching a page. Hold on.',
    'Research mode: on.',
  ],
  tools: [
    'Running it. Carefully.',
    'Shell time. Watching the output.',
    'Command sent. Fingers crossed.',
    'Let us see what it says.',
  ],
  build: [
    'Editing. Small changes, clean diffs.',
    'Writing the patch.',
    'Building. Please compile.',
    'Patching it up.',
  ],
  success: [
    'That worked. I am taking some credit.',
    'Green. All green.',
    'Nailed it. Thwip.',
    'Tests pass. I will be insufferable about it.',
  ],
  error: [
    'Ouch. That did not work.',
    'Red. Very red.',
    'Well. That failed. Let me look.',
    'Okay, we have a bug. I am on it.',
  ],
  look: [
    'Let me take a look.',
    'Ooh, an image. Zooming in.',
    'Eight eyes, one screenshot. On it.',
    'Looking. Carefully.',
  ],
  risky: [
    'Uh. That command is spicy.',
    'I would read that one twice.',
    'That could delete something real.',
  ],
};

// Angel/devil conscience, borrowed from the "two shoulder spiders" idea: when a command is
// destructive, Spiddy argues with itself instead of quietly running it.
const RISKY = new RegExp([
  '\\brm\\s+-[a-z]*r',      // recursive delete
  'drop\\s+table',
  'truncate\\s+table',
  '--force\\b',
  'force[- ]push',
  'reset\\s+--hard',
  'chmod\\s+777',
  'm[k]fs\\b',
  '\\bsudo\\b',
  '>\\s*/dev/sd',
].join('|'), 'i');

const CONSCIENCE = {
  devil: [
    'Send it. What is the worst that could happen?',
    'Skip the review. Ship it.',
    'It is probably fine. Probably.',
  ],
  angel: [
    'Check that command before running it.',
    'Read it twice. Then read it again.',
    'We can undo code. We cannot undo a dropped table.',
  ],
};

function hash(str) {
  let h = 0;
  for (const ch of String(str || '')) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
  return h;
}

// Pick a line for a voice kind. `seed` makes the choice stable for the same event.
export function lineFor(kind, seed = '') {
  const list = VOICE[kind] || VOICE.idle;
  return list[hash(seed) % list.length];
}

// The command text of a tool event, across the shapes the WebUI receives.
function commandOf(ev) {
  if (!ev) return '';
  const cand = [ev.command, ev.input, ev.args, ev.arguments, ev.text, ev.title, ev.detail];
  for (const c of cand) {
    if (typeof c === 'string' && c) return c;
    if (c && typeof c === 'object') {
      if (typeof c.command === 'string') return c.command;
      try { return JSON.stringify(c); } catch (_) {}
    }
  }
  return '';
}

// The most recent destructive tool event, or null. Only shell-like tools are considered.
export function riskyToolEvent(toolEvents) {
  const list = Array.isArray(toolEvents) ? toolEvents.filter(Boolean) : [];
  for (let i = list.length - 1; i >= 0; i--) {
    const ev = list[i];
    const name = String(ev.tool_name || ev.tool || '').toLowerCase();
    const isShell = /^(bash|shell|run_command|exec|terminal)$/.test(name);
    if (!isShell && !RISKY.test(commandOf(ev))) continue;
    if (RISKY.test(commandOf(ev))) return ev;
  }
  return null;
}

// Angel vs devil for a destructive command. Returns null when nothing is risky.
export function conscienceFor(toolEvents, seed = '') {
  if (!riskyToolEvent(toolEvents)) return null;
  return {
    angel: CONSCIENCE.angel[hash(seed + ':angel') % CONSCIENCE.angel.length],
    devil: CONSCIENCE.devil[hash(seed + ':devil') % CONSCIENCE.devil.length],
  };
}

// Emotion/pose -> which voice to use. Tool outcome wins, because Spiddy reacts to results.
export function voiceFor(clip = {}) {
  const emotion = clip.emotion || '';
  if (emotion === 'worried' || emotion === 'sad' || emotion === 'angry') return 'error';
  if (emotion === 'excited' || emotion === 'proud' || emotion === 'happy') return 'success';
  if (clip.pose === 'intro') return 'greeting';
  if (VOICE[clip.pose]) return clip.pose;
  return 'think';
}

// The one line Spiddy says for a clip. Stable for the same text, varied across texts.
export function quipFor(clip = {}) {
  const seed = [clip.pose, clip.emotion, clip.intensity, clip.motion, clip.text || ''].join('|');
  const conscience = conscienceFor(clip.toolEvents, seed);
  if (conscience) {
    // Alternate who wins the argument, so it does not always nag the same way.
    return hash(seed) % 2 === 0 ? conscience.angel : conscience.devil;
  }
  return lineFor(voiceFor(clip), seed);
}

// Mood drifts with outcomes. Exposed so the hero can tint the face without state in the DOM.
export function moodFor(clip = {}) {
  const emotion = clip.emotion || '';
  if (emotion === 'excited' || emotion === 'proud') return 'chuffed';
  if (emotion === 'worried' || emotion === 'sad' || emotion === 'angry') return 'sheepish';
  if (emotion === 'surprised' || emotion === 'dizzy') return 'wired';
  if (emotion === 'goofy' || emotion === 'confused') return 'amused';
  return 'steady';
}
