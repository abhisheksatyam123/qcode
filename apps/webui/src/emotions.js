// Spiddy emotion library: a generated catalog of clip presets plus a rule-based sentiment mapper.
// Pure functions only (no DOM, no network) so they can be imported and tested directly.

export const EMOTIONS = ['happy', 'surprised', 'worried', 'focused', 'proud', 'sad', 'angry', 'sleepy', 'excited', 'confused', 'goofy', 'dizzy'];
export const INTENSITIES = ['low', 'mid', 'high'];
export const POSES = ['intro', 'think', 'computer', 'phone', 'tools', 'build'];
export const MOTIONS = ['idle', 'bounce', 'shake', 'sway'];

// Which motion presets suit each emotion. clipFor picks one deterministically from the text.
const MOTION_VARIANTS = {
  happy: ['bounce', 'idle'], surprised: ['bounce', 'idle'], worried: ['shake', 'sway'],
  focused: ['idle', 'sway'], proud: ['bounce', 'idle'], sad: ['sway', 'idle'],
  angry: ['shake', 'idle'], sleepy: ['sway', 'idle'], excited: ['bounce', 'shake'],
  confused: ['sway', 'idle'], goofy: ['bounce', 'sway'], dizzy: ['sway', 'shake'],
};

// Generated once: every pose x emotion x intensity x motion combination.
export const CATALOG = [];
for (const emotion of EMOTIONS) {
  for (const intensity of INTENSITIES) {
    for (const pose of POSES) {
      for (const motion of MOTIONS) {
        CATALOG.push({ id: pose + '-' + emotion + '-' + intensity + '-' + motion, pose, emotion, intensity, motion });
      }
    }
  }
}

// Sentiment rules, checked in priority order. Tool outcome beats wording.
const RULES = {
  angry: /\b(blocked|forbidden|denied|conflict|critical|furious|unacceptable)\b/i,
  goofy: /\b(oops|whoops|my bad|silly|haha|lol)\b/i,
  dizzy: /\b(timed? out|timeout|retrying|spinning|lost track)\b/i,
  worried: /\b(sorry|fail(ed|ure|s)?|error|broken|bug|cannot|can't|unable|wrong|crash|exception)\b/i,
  sad: /\b(unfortunately|sadly|lost|missing|no results?|not found|nothing matched)\b/i,
  focused: /\b(let me|checking|looking|analy[sz]|investigat|reading|searching|one moment|hmm)\b/i,
  confused: /\b(unclear|not sure|which one|confus|ambiguous)\b/i,
  positive: /\b(done|fixed|works?|success(ful|fully)?|great|nice|perfect|passed|ready|good|all set)\b/i,
};

// Map the reply text and tool outcomes to { emotion, intensity }.
export function sentimentFor(text = '', toolEvents = []) {
  const list = Array.isArray(toolEvents) ? toolEvents.filter(Boolean) : [];
  const last = list[list.length - 1];
  if (last && last.status === 'error') return { emotion: 'worried', intensity: 'mid' };
  if (last && last.status === 'running') return { emotion: 'focused', intensity: 'low' };

  const t = String(text || '');
  const bangs = (t.match(/!/g) || []).length;
  if (RULES.angry.test(t)) return { emotion: 'angry', intensity: 'mid' };
  if (RULES.goofy.test(t)) return { emotion: 'goofy', intensity: 'mid' };
  if (RULES.dizzy.test(t)) return { emotion: 'dizzy', intensity: 'mid' };
  if (RULES.worried.test(t)) return { emotion: 'worried', intensity: 'mid' };
  if (RULES.sad.test(t)) return { emotion: 'sad', intensity: 'mid' };
  if (RULES.positive.test(t)) {
    return bangs >= 2 ? { emotion: 'excited', intensity: 'high' } : { emotion: 'happy', intensity: 'mid' };
  }
  if (RULES.confused.test(t) || /\?/.test(t)) return { emotion: 'confused', intensity: 'low' };
  if (RULES.focused.test(t)) return { emotion: 'focused', intensity: 'low' };
  if (last && last.status === 'success') return { emotion: 'happy', intensity: 'low' };
  return { emotion: 'focused', intensity: 'low' };
}

function stableHash(str) {
  let h = 0;
  for (const ch of String(str || '')) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
  return h;
}

// Pick the actual clip row from CATALOG (the animation DB) for a pose + emotion + intensity.
// Falls back to a wider slice of the catalog when an exact combination is missing, so a
// model-supplied label can never produce a clip that does not exist.
export function clipForEmotion({ pose = 'think', emotion = 'focused', intensity = 'mid', text = '' } = {}) {
  const po = POSES.includes(pose) ? pose : 'think';
  const em = EMOTIONS.includes(emotion) ? emotion : 'focused';
  const it = INTENSITIES.includes(intensity) ? intensity : 'mid';
  const suited = MOTION_VARIANTS[em] || ['idle'];
  const byPose = CATALOG.filter(c => c.pose === po && c.emotion === em);
  // Narrowest tier first: suited motion at the requested intensity, then suited motion,
  // then any motion for the pose, then that emotion in any pose. A model label can never
  // point at a clip that is not in the DB.
  const tiers = [
    byPose.filter(c => suited.includes(c.motion) && c.intensity === it),
    byPose.filter(c => suited.includes(c.motion)),
    byPose,
    CATALOG.filter(c => c.emotion === em),
  ];
  const rows = tiers.find(t => t.length) || CATALOG;
  const row = rows[stableHash(text) % rows.length] || CATALOG[0];
  return { pose: row.pose, emotion: row.emotion, intensity: row.intensity, motion: row.motion };
}

// The clip to play for a live message: pose from the caller, emotion from sentiment,
// motion chosen from the catalog by a stable hash of the text.
export function clipFor({ pose = 'think', text = '', toolEvents = [] } = {}) {
  const s = sentimentFor(text, toolEvents);
  return clipForEmotion({ pose, emotion: s.emotion, intensity: s.intensity, text });
}
