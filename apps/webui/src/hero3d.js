// 3D chibi superhero mascot renderer for the WebUI.
//
// Ported from the standalone "3D Chibi Superhero Mascots" demo (Three.js r128): five heroes
// (Spider-Man, Batman, Iron Man, Hulk, LEGO Batman), nine expressions, and the original
// signature move choreography. The pure helpers below have no DOM/WebGL access and are
// unit-tested directly; attachHero3D is the only function that touches the page.
//
// Three.js is served from /vendor-three.min.js and loaded before app.js. The renderer is
// transparent, pointer-events: none, and sized to its container, so it can sit in the same
// overlay layer as the CSS mascot without intercepting chat input.
//
// Contract: docs/mascot.md.

const THREE = globalThis.THREE;

export const HERO3D_HEROES = ['spidey', 'batman', 'ironman', 'hulk', 'legobatman'];
export const HERO3D_EXPRESSIONS = [
  'neutral', 'happy', 'sad', 'spideysense', 'love',
  'angry', 'zipped', 'angel', 'devil',
];

// The five hero themes map to the matching 3D model.
export const HERO3D_THEME_HERO = {
  'spiddy': 'spidey',
  'spiddy-night': 'batman',
  'spiddy-steel': 'ironman',
  'spiddy-jade': 'hulk',
  'spiddy-brick': 'legobatman',
};

export function hero3dForTheme(theme = '') {
  return HERO3D_THEME_HERO[String(theme || '')] || 'spidey';
}

// The animation loop knows a few names the browser data used before the 3D port.
const ANIMATION_ALIASES = { glide: 'batglide' };


  // ============================================================
  // 3. HERO PROFILES & SIGNATURE MOVE LISTS
  // ============================================================
  export const HERO3D_CONFIGS = {
    spidey: {
      name: 'Spider-Man',
      zipLabel: '🕸️ Web-Zip to Click',
      zipText: 'Thwip! ⚡',
      techNote: '<strong>Spider-Man:</strong> Web-Zip to click, Wall Crawl on glass, Upside-Down Hang, Web Swing, and Thwip Shoot.',
      cordColor: 0xf1f5f9,
      moves: [
        ['idle', 'Idle Bounce'],
        ['swing', '🕸️ Web Swing'],
        ['crawl', '🕷️ Wall Crawl'],
        ['hang', '🙃 Web Hang'],
        ['thwip', '💥 Thwip Shoot'],
        ['run', '🏃 Run'],
        ['backflip', '🤸 Backflip'],
        ['hero', '🦸 Hero Land'],
        ['dance', '🕺 Spidey Dance'],
        ['sit', '🪑 Chill Sit'],
        ['peek', '👀 Curious Peek'],
        ['nap', '😴 Power Nap'],
        ['think', '🤔 Spidey Think'],
        ['search', '🔍 Web Scan'],
        ['wave', '👋 Friendly Wave'],
        ['spin', '🌀 Web Spin'],
        ['jump', '🦘 Rooftop Hop'],
        ['stretch', '🙆 Web Stretch'],
        ['shrug', '🤷 Web Shrug'],
        ['point', '👉 Thwip Point'],
        ['facepalm', '🤦 Web Facepalm'],
        ['taunt', '😏 Wisecrack'],
        ['bow', '🙇 Spider Bow'],
        ['celebrate', '🎉 Web Celebration']
      ],
      bubbles: {
        neutral: 'Reporting for duty! 🕷️', happy: 'Feeling great! 😄', sad: 'Aww, really...? 😢',
        spideysense: 'My Spider-Sense is tingling! ⚡', love: 'Sending good vibes! ❤️',
        angry: 'Not on my watch! 😠', zipped: 'Mmph! Lips are sealed! 🤐',
        angel: 'Who, me? Purely innocent! 😇', devil: 'Hehe... trouble time! 😈'
      }
    },
    batman: {
      name: 'Batman',
      zipLabel: '🪝 Grapple to Click',
      zipText: 'Grapple! 🪝',
      techNote: '<strong>Batman:</strong> Bat-Glide with billowing cape, Grapple Swing, Batarang Throw, and Shadow Brood.',
      cordColor: 0x64748b,
      moves: [
        ['idle', 'Idle Brood'],
        ['batglide', '🦇 Bat-Glide'],
        ['batarang', '🪃 Batarang'],
        ['swing', '🪝 Grapple Swing'],
        ['brood', '🫥 Shadow Perch'],
        ['hero', '🦸 Hero Land'],
        ['run', '🏃 Sprint'],
        ['sneak', '🥷 Stealth Sneak'],
        ['salute', '🫡 Respect'],
        ['sit', '🪑 Gargoyle Sit'],
        ['peek', '👀 Vigilant Peek'],
        ['nap', '😴 Bat-Nap'],
        ['think', '🤔 Brooding Think'],
        ['search', '🔍 Detective Scan'],
        ['wave', '👋 Reluctant Wave'],
        ['spin', '🌀 Cape Spin'],
        ['jump', '🦘 Rooftop Leap'],
        ['stretch', '🧘 Cape Stretch'],
        ['shrug', '🤷 Bat Shrug'],
        ['point', '👉 Bat Point'],
        ['facepalm', '🤦 Alfred...'],
        ['taunt', '😏 Bat Taunt'],
        ['bow', '🙇 Bat Bow'],
        ['celebrate', '🎉 Mission Complete']
      ],
      bubbles: {
        neutral: 'I am the night. 🦇', happy: 'Hmph. Acceptable. 😏', sad: 'Gotham needs me... 😢',
        spideysense: 'Incoming threat! ⚡', love: 'Even Batman cares. 🖤',
        angry: 'Where is she?! 😠', zipped: '... *silent stare* 🤐',
        angel: 'Guardian of Gotham. 😇', devil: 'Fear the Bat! 😈'
      }
    },
    ironman: {
      name: 'Iron Man',
      zipLabel: '🚀 Jet-Boost to Click',
      zipText: 'Boost! 🚀',
      techNote: '<strong>Iron Man:</strong> Repulsor Flight with cyan thrusters, Unibeam Blast, Repulsor Shot, and Supersonic Hover.',
      cordColor: 0x38bdf8,
      moves: [
        ['idle', '🛸 Arc Hover'],
        ['flight', '🚀 Repulsor Flight'],
        ['unibeam', '⚡ Unibeam Blast'],
        ['thwip', '✋ Repulsor Shot'],
        ['hero', '🦸 Hero Land'],
        ['cheer', '🎉 Victory Cheer'],
        ['dance', '🕺 Stark Groove'],
        ['salute', '🫡 Salute'],
        ['run', '🏃 Dash'],
        ['moonwalk', '🌙 Moonwalk'],
        ['peek', '👀 Jarvis Scan'],
        ['nap', '😴 Low Power'],
        ['think', '🤔 Arc-Reactor Think'],
        ['search', '🔍 HUD Scan'],
        ['wave', '👋 Repulsor Wave'],
        ['spin', '🌀 Repulsor Spin'],
        ['jump', '🚀 Thruster Hop'],
        ['stretch', '🙆 Suit Stretch'],
        ['shrug', '🤷 Armor Shrug'],
        ['point', '👉 Repulsor Point'],
        ['facepalm', '🤦 Systems Facepalm'],
        ['taunt', '😏 Stark Taunt'],
        ['bow', '🙇 Suit Bow'],
        ['celebrate', '🎉 Victory Pose']
      ],
      bubbles: {
        neutral: 'Jarvis, run diagnostics. 🤖', happy: 'Genius, billionaire, chibi! 😄', sad: 'Suit power critical... 😢',
        spideysense: 'Target locked! ⚡', love: 'I love you 3000! ❤️',
        angry: 'Firing repulsors! 😠', zipped: 'Comms offline. 🤐',
        angel: 'Saving the world, again. 😇', devil: 'Maximum firepower! 😈'
      }
    },
    hulk: {
      name: 'Hulk',
      zipLabel: '🦘 Super Leap to Click',
      zipText: 'HULK LEAP! 💥',
      techNote: '<strong>Hulk:</strong> Hulk Smash with shockwaves, Thunderclap, Gamma Rage flex, and Super Leap.',
      cordColor: 0x4ade80,
      moves: [
        ['idle', 'Idle Flex'],
        ['smash', '💥 Hulk Smash!'],
        ['thunderclap', '👏 Thunderclap'],
        ['rage', '😤 Gamma Rage'],
        ['leap', '🦘 Super Leap'],
        ['run', '🏃 Charge'],
        ['hero', '🦸 Heavy Land'],
        ['cheer', '🎉 Roar Cheer'],
        ['dance', '🕺 Hulk Groove'],
        ['sit', '🪑 Chill Sit'],
        ['peek', '👀 Big Peek'],
        ['nap', '😴 Power Nap'],
        ['think', '🤔 Hulk Think'],
        ['search', '🔍 Hulk Sniff'],
        ['wave', '👋 Hulk Wave'],
        ['spin', '🌀 Hulk Spin'],
        ['jump', '🦘 Gamma Hop'],
        ['stretch', '🙆 Hulk Stretch'],
        ['shrug', '🤷 Hulk Shrug'],
        ['point', '👉 Hulk Point'],
        ['facepalm', '🤦 Hulk Sorry'],
        ['taunt', '😏 Hulk Taunt'],
        ['bow', '🙇 Hulk Bow'],
        ['celebrate', '🎉 Hulk Party']
      ],
      bubbles: {
        neutral: 'HULK READY! 💪', happy: 'HULK HAPPY! 😄', sad: 'Hulk sad... 😢',
        spideysense: 'HULK SENSE DANGER! ⚡', love: 'Hulk like you! 💚',
        angry: 'HULK SMASH!! 😠', zipped: 'Mmmph!! 🤐',
        angel: 'Hulk good boy! 😇', devil: 'UNLEASH THE HULK! 😈'
      }
    },
    legobatman: {
      name: 'LEGO Batman',
      zipLabel: '🧱 Brick-Grapple to Click',
      zipText: 'First try! 🧱',
      techNote: '<strong>LEGO Batman:</strong> Master Builder brick assembly, Bat-Glide, Dark Rock Guitar, and Batarang.',
      cordColor: 0xfacc15,
      moves: [
        ['idle', 'Idle Brick'],
        ['masterbuild', '🧱 Master Builder'],
        ['rock', '🎸 Dark Rock'],
        ['batglide', '🦇 Bat-Glide'],
        ['batarang', '🪃 Batarang'],
        ['swing', '🪝 Grapple Swing'],
        ['dance', '🕺 Bat-Dance'],
        ['hero', '🦸 Hero Land'],
        ['run', '🏃 Brick Sprint'],
        ['salute', '🫡 Salute'],
        ['peek', '👀 Brick Peek'],
        ['nap', '😴 Bat-Nap'],
        ['think', '🤔 Brick Think'],
        ['search', '🔍 Brick Scan'],
        ['wave', '👋 Brick Wave'],
        ['spin', '🌀 Brick Spin'],
        ['jump', '🧱 Brick Hop'],
        ['stretch', '🙆 Plastic Stretch'],
        ['shrug', '🤷 Brick Shrug'],
        ['point', '👉 Brick Point'],
        ['facepalm', '🤦 First Try...'],
        ['taunt', '😏 Brick Taunt'],
        ['bow', '🙇 Brick Bow'],
        ['celebrate', '🎉 Everything Is Awesome']
      ],
      bubbles: {
        neutral: 'First try! 🧱', happy: 'I only work in black! 😎', sad: 'Darkness... no parents... 😢',
        spideysense: 'Wait—did you hear that?! ⚡', love: 'You complete my Bat-heart! 💛',
        angry: 'WHO ALWAYS PAYS THEIR TAXES?! 😠', zipped: '... *plastic silence* 🤐',
        angel: 'Totally innocent! 😇', devil: 'Check out my abs! 😈'
      }
    }
  };

export const HERO3D_MOVES = Object.fromEntries(
  HERO3D_HEROES.map(hero => [hero, HERO3D_CONFIGS[hero].moves.map(([id]) => id)]),
);

const ALL_ANIMATIONS = new Set(
  Object.values(HERO3D_MOVES).flat().concat(['batglide']),
);

// Accent colour for each hero's thought/search props (bubbles, scanner lens, scan ring).
export const HERO3D_ACCENTS = {
  spidey: 0xf1f5f9,
  batman: 0xfacc15,
  ironman: 0x7dd3fc,
  hulk: 0x4ade80,
  legobatman: 0xfacc15,
};

// Tool names that mean "the model is looking for something" (files or the web).
// While one of these runs, the 3D mascot plays the hero-specific search animation.
export const HERO3D_SEARCH_TOOLS = new Set([
  'grep', 'glob', 'ls', 'read', 'search', 'find', 'rg',
  'websearch', 'web_search', 'webfetch', 'web_fetch',
]);

export function hero3dHasSearchTool(toolEvents) {
  const list = Array.isArray(toolEvents) ? toolEvents : [];
  const running = list.filter(t => t && t.status === 'running');
  const last = running[running.length - 1];
  return !!(last && HERO3D_SEARCH_TOOLS.has(String(last.tool_name || '').toLowerCase()));
}

// Per-hero acting pools for the chat's busy modes. Idle uses the full move list.
// Every entry must also exist in that hero's HERO3D_CONFIGS.moves list.
export const HERO3D_MODE_POOLS = {
  spidey: {
    think: ['think'],
    search: ['search'],
    tools: ['thwip', 'crawl', 'run'],
    success: ['celebrate', 'backflip', 'hero', 'dance'],
    error: ['facepalm', 'shrug', 'sit'],
  },
  batman: {
    think: ['think'],
    search: ['search'],
    tools: ['batarang', 'batglide', 'sneak'],
    success: ['hero', 'salute', 'celebrate', 'bow'],
    error: ['facepalm', 'brood', 'shrug'],
  },
  ironman: {
    think: ['think'],
    search: ['search'],
    tools: ['unibeam', 'flight', 'thwip'],
    success: ['celebrate', 'cheer', 'hero', 'dance'],
    error: ['facepalm', 'shrug', 'peek'],
  },
  hulk: {
    think: ['think'],
    search: ['search'],
    tools: ['smash', 'thunderclap', 'leap'],
    success: ['celebrate', 'cheer', 'hero', 'dance'],
    error: ['facepalm', 'rage', 'shrug'],
  },
  legobatman: {
    think: ['think'],
    search: ['search'],
    tools: ['masterbuild', 'batarang', 'batglide'],
    success: ['celebrate', 'hero', 'dance', 'salute'],
    error: ['facepalm', 'shrug', 'rock'],
  },
};

function resolveHero3D(heroOrTheme) {
  return HERO3D_HEROES.includes(heroOrTheme) ? heroOrTheme : hero3dForTheme(heroOrTheme);
}

// The acts a hero may play in a chat mode ('idle' gets the full 24-move list).
export function hero3dPoolFor(mode = 'idle', heroOrTheme) {
  const hero = resolveHero3D(heroOrTheme);
  const pools = HERO3D_MODE_POOLS[hero] || {};
  const pool = mode && mode !== 'idle' ? pools[mode] : null;
  if (Array.isArray(pool) && pool.length) return pool.slice();
  return HERO3D_MOVES[hero].slice();
}

// Pick the 3D act for a chat mode. Thinking and file/web searching are semantic and
// deterministic; success/error/idle draw from the hero's own pool.
export function hero3dAnimationFor({ mode = 'idle', theme = '', hero, toolEvents = [], rand = Math.random, prev = null } = {}) {
  const h = HERO3D_HEROES.includes(hero) ? hero : hero3dForTheme(theme);
  if (mode === 'think') return 'think';
  if (mode === 'tools' && hero3dHasSearchTool(toolEvents)) return 'search';
  const pool = hero3dPoolFor(mode, h);
  const list = prev ? pool.filter(a => a !== prev) : pool;
  const from = list.length ? list : pool;
  if (from.length === 0) return 'idle';
  return from[Math.min(from.length - 1, Math.floor(rand() * from.length))];
}

// All nine faces, in the order the idle loop rotates through them.
export const HERO3D_EXPRESSION_CYCLE = [
  'neutral', 'happy', 'sad', 'spideysense', 'love', 'angry', 'zipped', 'angel', 'devil',
];

// Busy modes keep the face appropriate to what is happening, while still rotating.
const HERO3D_MODE_EXPRESSIONS = {
  idle: HERO3D_EXPRESSION_CYCLE,
  think: ['neutral', 'spideysense', 'zipped'],
  search: ['neutral', 'spideysense', 'happy'],
  tools: ['neutral', 'spideysense', 'happy', 'angry'],
  success: ['happy', 'love', 'angel', 'spideysense', 'devil'],
  error: ['sad', 'angry', 'zipped'],
};

// Pick a face for a chat mode. `index` advances on every roam tick, so idle cycles all nine
// and busy modes rotate through their mood subset. Never returns an unknown expression.
export function hero3dExpressionFor({ mode = 'idle', index = 0 } = {}) {
  const list = HERO3D_MODE_EXPRESSIONS[mode] || HERO3D_EXPRESSION_CYCLE;
  const i = Math.abs(Math.floor(Number(index) || 0));
  return list[i % list.length];
}

// Hero-specific dialogue. A hero only ever speaks lines from its own pool: Batman never
// thwips, Hulk never mentions Gotham, and the LEGO hero never borrows Stark's snark.
export const HERO3D_VOICE = {
  spidey: {
    greeting: [
      'Dropping in! Point me at something.',
      'Spiddy on the ceiling, ready to help.',
      'Webs ready. What are we building?',
      'Hi! I hang around and narrate.',
    ],
    idle: [
      'Just hanging out. Literally.',
      'Ceiling inspection: all clear.',
      'Webs ready. Point me at something.',
      'Idle. Suspiciously idle.',
    ],
    think: [
      'Hmm. Let me think.',
      'Spinning up a plan...',
      'Working it out, eight legs at a time.',
      'Something is sticky here. Thinking.',
    ],
    tools: [
      'Running it. Carefully.',
      'Shell time. Watching the output.',
      'Command sent. Fingers crossed.',
      'Let us see what it says.',
    ],
    search: [
      'Thwipping through the files.',
      'Web-scanning. Found a trail.',
      'Grepping around the web.',
      'Reading. Actually reading.',
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
      'Web snapped. Let me look.',
      'Okay, we have a bug. I am on it.',
    ],
  },
  batman: {
    greeting: [
      'The Bat is here. What broke?',
      'Gotham can wait. Show me the code.',
      'I am the night. And I am listening.',
      'Report. Quietly.',
    ],
    idle: [
      'I am the night. Also idle.',
      'Gotham is quiet. Too quiet.',
      'Watching. Always watching.',
      'The cape waits.',
    ],
    think: [
      'Analyzing. No distractions.',
      'The Detective is thinking.',
      'Gathering the facts.',
      'Every clue has a pattern.',
    ],
    tools: [
      'Executing. Precisely.',
      'The Batcomputer is on it.',
      'Running the command. Quietly.',
      'No mistakes. Not tonight.',
    ],
    search: [
      'Scanning the files.',
      'Following the evidence.',
      'The lens does not blink.',
      'Searching. Gotham depends on it.',
    ],
    success: [
      'Acceptable. Continue.',
      'The mission is complete.',
      'Justice, delivered.',
      'Hmph. Well done.',
    ],
    error: [
      'A setback. Nothing more.',
      'The trail went cold.',
      'We missed something.',
      'Gotham can wait. Fix this.',
    ],
  },
  ironman: {
    greeting: [
      'Suit ready. What are we building?',
      'JARVIS is warmed up. Go.',
      'Stark online. Try to impress me.',
      'Let us build something that flies.',
    ],
    idle: [
      'Suit up. Or just stand here.',
      'JARVIS, are we idle?',
      'Arc reactor at standby.',
      'Genius, billionaire, idle.',
    ],
    think: [
      'Running diagnostics.',
      'Hmm. Let me think.',
      'JARVIS, options.',
      'Calculating. Obviously.',
    ],
    tools: [
      'Repulsors online. Running it.',
      'Command sent. Watching the HUD.',
      'Executing. Try not to explode.',
      'Let us see what the suit says.',
    ],
    search: [
      'HUD scan. Reading everything.',
      'JARVIS, find it.',
      'Scanning the files.',
      'Target acquired. Reading.',
    ],
    success: [
      'Nailed it. You are welcome.',
      'Victory pose. Obviously.',
      'All systems green.',
      'I love it when a plan works.',
    ],
    error: [
      'Suit malfunction. Fixing it.',
      'That is not supposed to happen.',
      'JARVIS, roll that back.',
      'Okay. We have a problem.',
    ],
  },
  hulk: {
    greeting: [
      'HULK HERE!',
      'Hulk ready to help.',
      'Point Hulk at problem.',
      'Hulk not smash code. Maybe.',
    ],
    idle: [
      'HULK WAIT.',
      'Hulk stand here. Quiet.',
      'Hulk not smash. Yet.',
      'Hulk ready.',
    ],
    think: [
      'HULK THINK.',
      'Hulk thinking. Hard.',
      'Hulk have idea. Maybe.',
      'Hmm. Hulk think.',
    ],
    tools: [
      'HULK RUN IT.',
      'Hulk press button.',
      'Command go. Hulk watch.',
      'Hulk careful. Mostly.',
    ],
    search: [
      'HULK LOOK.',
      'Hulk sniff for clue.',
      'Hulk search. Found something?',
      'Hulk read. Slowly.',
    ],
    success: [
      'HULK DID IT!',
      'HULK WIN!',
      'Good. Hulk happy.',
      'Hulk smash problem!',
    ],
    error: [
      'HULK SORRY.',
      'Problem not smashable.',
      'Hulk try again.',
      'Hulk confused.',
    ],
  },
  legobatman: {
    greeting: [
      'First try! What are we building?',
      'LEGO Batman reporting. Brick by brick.',
      'I am the night. In brick form.',
      'Everything is awesome. Let us build.',
    ],
    idle: [
      'First try. Always.',
      'I only work in black. And very dark grey.',
      'Brick by brick.',
      'LEGO Batman waits.',
    ],
    think: [
      'Master Builder thinking.',
      'First try... maybe.',
      'Bricks assembling in my head.',
      'Hmm. I have a plan.',
    ],
    tools: [
      'Building it. First try.',
      'Running it. Brick by brick.',
      'Command sent. Obviously.',
      'I press the button. Heroically.',
    ],
    search: [
      'Scanning. First try.',
      'Looking for the missing brick.',
      'Brick scan. Reading.',
      'Found it. Obviously.',
    ],
    success: [
      'Everything is awesome.',
      'First try. Told you.',
      'Another victory. Naturally.',
      'I am the night. In brick form.',
    ],
    error: [
      'That was not first try.',
      'A missing brick. Fix it.',
      'Darkness... no parents... no.',
      'Rebuild. Brick by brick.',
    ],
  },
};

function hero3dHash(str) {
  let h = 0;
  for (const ch of String(str || '')) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
  return h;
}

// A line from this hero's own pool. `seed` makes the choice stable per roam tick.
export function hero3dLineFor(heroOrTheme, mode = 'idle', seed = '') {
  const hero = resolveHero3D(heroOrTheme);
  const voice = HERO3D_VOICE[hero] || HERO3D_VOICE.spidey;
  const lines = voice[mode] || voice.idle;
  return lines[hero3dHash(seed) % lines.length];
}

// A hero-specific line for a classic-mascot clip (pose + emotion), mirroring the old
// personality quip selection without borrowing another hero's voice.
const HERO3D_CLIP_MODE = {
  intro: 'greeting',
  think: 'think',
  computer: 'search',
  phone: 'search',
  tools: 'tools',
  build: 'tools',
};

export function hero3dQuipFor(heroOrTheme, clip = {}) {
  const c = clip && typeof clip === 'object' ? clip : {};
  const emotion = c.emotion || '';
  let mode = HERO3D_CLIP_MODE[c.pose] || 'think';
  if (emotion === 'worried' || emotion === 'sad' || emotion === 'angry') mode = 'error';
  else if (emotion === 'excited' || emotion === 'proud' || emotion === 'happy') mode = 'success';
  const seed = [c.pose, c.emotion, c.intensity, c.motion, c.text || ''].join('|');
  return hero3dLineFor(heroOrTheme, mode, seed);
}

function clampCaption(value, max = 140) {
  if (value === null || value === undefined) return '';
  const text = String(value).replace(/\s+/g, ' ').trim();
  if (!text) return '';
  return text.length > max ? text.slice(0, max - 1) + '…' : text;
}

// Turns any partial input into a valid 3D mascot state. Unknown values fall back; never throws.
export function normalizeHero3DState(input = {}) {
  const src = input && typeof input === 'object' ? input : {};
  const hero = HERO3D_HEROES.includes(src.hero) ? src.hero : hero3dForTheme(src.theme);
  const expression = HERO3D_EXPRESSIONS.includes(src.expression) ? src.expression : 'neutral';
  const requested = ANIMATION_ALIASES[src.animation] || src.animation;
  const animation = ALL_ANIMATIONS.has(requested) ? requested : 'idle';
  return { hero, expression, animation, caption: clampCaption(src.caption) };
}

// Chat emotions (emotions.js) -> the nine 3D facial expressions.
const EMOTION_EXPRESSION = {
  happy: 'happy',
  excited: 'happy',
  proud: 'happy',
  goofy: 'happy',
  sad: 'sad',
  worried: 'sad',
  angry: 'angry',
  surprised: 'spideysense',
  sleepy: 'zipped',
  focused: 'neutral',
  confused: 'neutral',
  dizzy: 'neutral',
};

export function expressionForEmotion(emotion) {
  return EMOTION_EXPRESSION[emotion] || 'neutral';
}

// True when a WebGL context can be created. Used by the UI to fall back to the CSS mascot.
export function hero3dAvailable() {
  if (!THREE || !THREE.WebGLRenderer) return false;
  try {
    const canvas = document.createElement('canvas');
    const gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
    if (!gl) return false;
    const lose = gl.getExtension('WEBGL_lose_context');
    if (lose) lose.loseContext();
    return true;
  } catch (_) {
    return false;
  }
}


// Procedural canvas textures. They are independent of the renderer, so one cache is shared by
// every mount (theme switches recreate the scene but reuse these).
// ============================================================
// 1. PROCEDURAL TEXTURES FOR ALL HEROES & 9 EXPRESSIONS
// ============================================================
const headTexCache = {};
let webbedRedTexture = null;
function getHeadTexture(hero, expr) {
    const key = `${hero}:${expr}`;
    if (headTexCache[key]) return headTexCache[key];

    const W = 1024, H = 512;
    const c = document.createElement('canvas');
    c.width = W; c.height = H;
    const ctx = c.getContext('2d');
    const img = ctx.createImageData(W, H);
    const data = img.data;

    const dx = 1.0, dy = -0.06, dz = 0.0;

    for (let py = 0; py < H; py++) {
      const v = py / (H - 1);
      const phi = v * Math.PI;
      const sinPhi = Math.sin(phi);
      const cosPhi = Math.cos(phi);

      for (let px = 0; px < W; px++) {
        const u = px / (W - 1);
        const theta = u * Math.PI * 2;
        const x = -sinPhi * Math.cos(theta);
        const y = cosPhi;
        const z = sinPhi * Math.sin(theta);

        const dot = Math.max(-1, Math.min(1, x * dx + y * dy + z * dz));
        const alpha = Math.acos(dot);
        const az = Math.atan2(z, y - dy * dot);

        // Base colors per hero
        let r = 207, g = 66, b = 48; // spidey red
        if (hero === 'batman' || hero === 'legobatman') {
          const isLego = hero === 'legobatman';
          r = isLego ? 24 : 34; g = isLego ? 28 : 41; b = isLego ? 38 : 56;
          if (x > 0.42 && y < -0.18 && y > -0.68 && Math.abs(z) < 0.42) {
            r = isLego ? 250 : 242; g = isLego ? 204 : 194; b = isLego ? 21 : 160; // LEGO yellow vs skin
            if (y > -0.22 || Math.abs(z) > 0.38 || y < -0.64) {
              r = 12; g = 10; b = 11;
            }
          }
        } else if (hero === 'hulk') {
          r = 74; g = 188; b = 82; // Hulk green
          // Messy jagged black hair cap on top
          const hairLine = 0.36 + 0.08 * Math.sin(az * 9);
          if (y > hairLine) { r = 22; g = 24; b = 28; }
        } else if (false) {
          r = 34; g = 41; b = 56; // dark slate cowl
          // Exposed lower-jaw mouth opening
          if (x > 0.42 && y < -0.18 && y > -0.68 && Math.abs(z) < 0.42) {
            r = 242; g = 194; b = 160; // warm skin
            // Crisp black cowl jawline border
            if (y > -0.22 || Math.abs(z) > 0.38 || y < -0.64) {
              r = 12; g = 10; b = 11;
            }
          }
        } else if (hero === 'ironman') {
          r = 198; g = 34; b = 42; // crimson armor
          // Iconic Gold Faceplate
          const inFaceplate = x > 0.35 && Math.abs(z) < 0.56 && y < 0.50 && y > -0.72 &&
            !(y > 0.28 && Math.abs(z) < 0.16); // forehead V-notch
          if (inFaceplate) {
            r = 245; g = 182; b = 36; // metallic gold
            const onEdge = x < 0.39 || Math.abs(z) > 0.52 || y > 0.46 || y < -0.68 || (y > 0.25 && Math.abs(z) < 0.19);
            if (onEdge) { r = 14; g = 11; b = 12; }
            // Chin/mouthplate slit
            if (Math.abs(y + 0.36) < 0.018 && Math.abs(z) < 0.24) {
              r = 28; g = 24; b = 26;
            }
          }
        }

        // Evaluate Eyes (0 = keep base, 1 = black rim, 2 = white/cyan lens, 3 = tear, 4 = blush, 5 = zipper)
        let state = 0;
        if (x > -0.05 && Math.abs(z) > 0.115) {
          const side = z >= 0 ? 1 : -1;
          const ex = Math.abs(z) - 0.115;
          const ey = y + (hero === 'spidey' ? 0.065 : 0.02);

          if (expr === 'happy') {
            const arcX = ex - 0.34;
            const arcY = ey - (0.04 - 0.65 * arcX * arcX);
            if (Math.abs(arcX) < 0.25 && Math.abs(arcY) < 0.048) state = 1;
          } else if (expr === 'angel' && side === -1) {
            const winkX = ex - 0.33;
            const winkY = ey - 0.01;
            const line1 = Math.abs(winkY - (0.16 * (0.22 - winkX) * 2.2)) < 0.042 && Math.abs(winkX) < 0.22;
            const line2 = Math.abs(winkY + (0.16 * (0.22 - winkX) * 2.2)) < 0.042 && Math.abs(winkX) < 0.22;
            if (line1 || line2) state = 1;
          } else {
            let tilt = hero === 'spidey' ? 0.208 : (hero === 'hulk' ? 0.34 : 0.28);
            let rxScale = hero === 'spidey' ? 0.40 : (hero === 'hulk' ? 0.30 : 0.34);
            let ryScale = hero === 'spidey' ? 0.35 : (hero === 'ironman' ? 0.16 : (hero === 'hulk' ? 0.20 : 0.22));
            let pinchFactor = hero === 'spidey' ? 0.32 : 0.38;
            let centerShift = hero === 'hulk' ? 0.34 : 0.37;
            let yShift = hero === 'hulk' ? 0.04 : 0.0;

            if (expr === 'sad') {
              tilt = -0.26; rxScale = 0.37; ryScale = hero === 'ironman' ? 0.15 : 0.28; pinchFactor = -0.22; yShift = -0.04;
            } else if (expr === 'angry') {
              tilt = 0.44; ryScale = hero === 'ironman' ? 0.13 : 0.20; pinchFactor = 0.45;
            } else if (expr === 'devil') {
              tilt = 0.38; ryScale = 0.16; pinchFactor = 0.42;
            } else if (expr === 'spideysense' || expr === 'zipped') {
              rxScale = 0.41; ryScale = hero === 'ironman' ? 0.21 : 0.34;
            } else if (expr === 'love') {
              tilt = 0.14; rxScale = 0.37; ryScale = hero === 'ironman' ? 0.18 : 0.30;
            }

            const cosA = Math.cos(tilt), sinA = Math.sin(tilt);
            const rxE = ex * cosA + (ey - yShift) * sinA - centerShift;
            const ryE = -ex * sinA + (ey - yShift) * cosA;
            const pinch = 1.0 + pinchFactor * Math.tanh(rxE * 2.1);
            const dEye = (rxE / rxScale) ** 2 + (ryE / (ryScale * pinch)) ** 2;

            const sadBrowClip = expr === 'sad' && (ryE > 0.16 - 0.18 * rxE);
            const hulkBrow = hero === 'hulk' && expr !== 'sad' && (ryE > 0.11 - 0.14 * rxE);
            if (!sadBrowClip && !hulkBrow) {
              if (dEye < 0.62) {
                state = 2;
                if (hero === 'hulk') {
                  const pDist = (rxE / 0.12) ** 2 + (ryE / 0.12) ** 2;
                  if (pDist < 0.35) state = 1; // Black pupil
                  else if (pDist < 0.85) state = 6; // Gamma green iris
                }
              } else if (dEye < 1.0) {
                state = 1;
              }
            } else if (hulkBrow && dEye < 1.15) {
              state = 1; // Heavy furrowed Hulk brow
            }
          }
        }

        // Hulk gritted white teeth (hangry / roaring)
        if (hero === 'hulk' && x > 0.52 && Math.abs(z) < 0.26 && state === 0) {
          if (expr === 'happy' || expr === 'love') {
            const my = y + (0.42 - 0.85 * z * z);
            if (Math.abs(my) < 0.026) state = 1;
          } else if (expr === 'sad') {
            const my = y + (0.46 + 0.85 * z * z);
            if (Math.abs(my) < 0.026) state = 1;
          } else if (expr !== 'zipped') {
            // Big gritted teeth mouth box with vertical tooth dividers
            const mx = Math.abs(z);
            const my = y + 0.43;
            const halfH = (expr === 'angry' || expr === 'devil' || expr === 'spideysense') ? 0.085 : 0.068;
            if (mx < 0.24 && Math.abs(my) < halfH) {
              if (mx > 0.21 || Math.abs(my) > halfH - 0.022 || Math.abs(my) < 0.012) {
                state = 1; // Black border & center bite line
              } else if (Math.abs((z + 0.24) % 0.08 - 0.04) > 0.032) {
                state = 1; // Vertical tooth dividers
              } else {
                state = 2; // Bright white gritted teeth!
              }
            }
          }
        } else if ((hero === 'batman' || hero === 'legobatman') && x > 0.55 && Math.abs(z) < 0.24 && state === 0) {
          if (expr === 'happy' || expr === 'love') {
            const my = y + (0.42 - 0.85 * z * z);
            if (Math.abs(my) < 0.024) state = 1;
          } else if (expr === 'sad') {
            const my = y + (0.46 + 0.85 * z * z);
            if (Math.abs(my) < 0.024) state = 1;
          } else if (expr === 'angry' || expr === 'devil') {
            const my = y + 0.43;
            if (Math.abs(my) < 0.025 && Math.abs(z) < 0.18) state = 1;
          } else if (expr === 'spideysense') {
            if ((z / 0.08) ** 2 + ((y + 0.44) / 0.065) ** 2 < 1.0) state = 1;
          } else if (expr !== 'zipped') {
            // Classic slight Batman smirk
            if (Math.abs(y + (0.43 - 0.15 * z)) < 0.018 && Math.abs(z) < 0.16) state = 1;
          }
        }

        // Blush
        if ((expr === 'happy' || expr === 'love') && x > 0.35 && state === 0) {
          const bx = Math.abs(z) - 0.56;
          const by = y + 0.27;
          if ((bx / 0.14) ** 2 + (by / 0.075) ** 2 < 1.0) state = 4;
        }

        // Teardrop for Sad
        if (expr === 'sad' && x > 0.32 && state === 0) {
          const tx = Math.abs(z) - 0.44;
          const ty = y + 0.30;
          const tearD = (tx / 0.075) ** 2 + (ty / 0.105) ** 2;
          if (tearD < 0.55) {
            state = ((tx + 0.018) ** 2 + (ty - 0.032) ** 2 < 0.0009) ? 2 : 3;
          } else if (tearD < 0.95) {
            state = 1;
          }
        }

        // Zipped mouth
        if (expr === 'zipped' && x > 0.55 && Math.abs(z) < 0.46) {
          const zy = y + 0.36;
          if (Math.abs(zy) < 0.058) {
            if (Math.abs(zy) > 0.042 || Math.abs(zy) < 0.012) state = 1;
            else state = (Math.floor((z + 0.5) * 28) % 2) === 0 ? 5 : 1;
          }
          if (z > 0.40 && z < 0.58 && Math.abs(y + 0.37) < 0.075) state = 5;
        }

        // Spider-Man webbing only
        let isWeb = false;
        if (hero === 'spidey') {
          const noseOval = (z / 0.095) ** 2 + ((y + 0.06) / 0.045) ** 2;
          const isNoseOval = x > 0.85 && noseOval > 0.65 && noseOval < 1.05;
          const spokeDist = Math.abs(Math.sin(az * 6)) * Math.sin(alpha);
          const scallop = 0.028 * Math.abs(Math.sin(az * 6));
          const ringVal = (alpha + scallop) / 0.33;
          const ringFrac = ringVal - Math.floor(ringVal);
          const ringDist = Math.min(ringFrac, 1 - ringFrac) * 0.33;
          isWeb = isNoseOval || (alpha > 0.09 && spokeDist < 0.021) || (alpha > 0.18 && ringDist < 0.014);
        }

        const idx = (py * W + px) * 4;
        if (state === 2) {
          if (hero === 'ironman') {
            data[idx] = 125; data[idx + 1] = 228; data[idx + 2] = 255; data[idx + 3] = 255; // Glowing cyan arc eyes
          } else {
            data[idx] = 252; data[idx + 1] = 252; data[idx + 2] = 254; data[idx + 3] = 255;
          }
        } else if (state === 3) {
          data[idx] = 147; data[idx + 1] = 197; data[idx + 2] = 253; data[idx + 3] = 255;
        } else if (state === 4) {
          data[idx] = 244; data[idx + 1] = 114; data[idx + 2] = 128; data[idx + 3] = 255;
        } else if (state === 5) {
          data[idx] = 203; data[idx + 1] = 213; data[idx + 2] = 225; data[idx + 3] = 255;
        } else if (state === 6) {
          data[idx] = 34; data[idx + 1] = 197; data[idx + 2] = 94; data[idx + 3] = 255;
        } else if (state === 1 || isWeb) {
          data[idx] = 12; data[idx + 1] = 10; data[idx + 2] = 11; data[idx + 3] = 255;
        } else {
          data[idx] = r; data[idx + 1] = g; data[idx + 2] = b; data[idx + 3] = 255;
        }
      }
    }
    ctx.putImageData(img, 0, 0);
    const tex = new THREE.CanvasTexture(c);
    tex.needsUpdate = true;
    headTexCache[key] = tex;
    return tex;
  }

const torsoTexCache = {};
function getTorsoTexture(hero) {
    if (torsoTexCache[hero]) return torsoTexCache[hero];
    const c = document.createElement('canvas');
    c.width = 512; c.height = 512;
    const ctx = c.getContext('2d');

    if (hero === 'spidey') {
      ctx.fillStyle = '#233040';
      ctx.fillRect(0, 0, 512, 512);
      ctx.fillStyle = '#cf4230';
      ctx.beginPath();
      ctx.moveTo(118, 0); ctx.quadraticCurveTo(132, 140, 138, 285);
      ctx.lineTo(256, 336); ctx.lineTo(374, 285);
      ctx.quadraticCurveTo(380, 140, 394, 0); ctx.closePath(); ctx.fill();
      ctx.beginPath();
      ctx.moveTo(0, 268); ctx.lineTo(138, 268); ctx.quadraticCurveTo(256, 294, 374, 268);
      ctx.lineTo(512, 268); ctx.lineTo(512, 324); ctx.lineTo(374, 324);
      ctx.quadraticCurveTo(256, 350, 138, 324); ctx.lineTo(0, 324); ctx.closePath(); ctx.fill();
      ctx.strokeStyle = '#0c080a'; ctx.lineWidth = 6; ctx.lineJoin = 'round';
      ctx.beginPath();
      ctx.moveTo(118, 0); ctx.quadraticCurveTo(132, 140, 138, 268);
      ctx.moveTo(394, 0); ctx.quadraticCurveTo(380, 140, 374, 268);
      ctx.moveTo(0, 268); ctx.lineTo(138, 268); ctx.quadraticCurveTo(256, 294, 374, 268); ctx.lineTo(512, 268);
      ctx.moveTo(0, 324); ctx.lineTo(138, 324); ctx.quadraticCurveTo(256, 350, 374, 324); ctx.lineTo(512, 324);
      ctx.stroke();
      ctx.lineWidth = 3.2;
      [184, 256, 328].forEach((x) => { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, 334); ctx.stroke(); });
      [76, 150, 224, 294].forEach((y) => { ctx.beginPath(); ctx.moveTo(124, y); ctx.quadraticCurveTo(256, y + 16, 388, y); ctx.stroke(); });
      for (let bx = 28; bx < 512; bx += 52) { ctx.beginPath(); ctx.moveTo(bx, 268); ctx.lineTo(bx, 324); ctx.stroke(); }
      ctx.fillStyle = '#080607'; ctx.strokeStyle = '#080607'; ctx.lineWidth = 6.5; ctx.lineCap = 'round';
      ctx.beginPath(); ctx.ellipse(256, 126, 12, 15, 0, 0, Math.PI * 2); ctx.fill();
      ctx.beginPath(); ctx.ellipse(256, 174, 17, 25, 0, 0, Math.PI * 2); ctx.fill();
      [-1, 1].forEach((sx) => {
        ctx.beginPath(); ctx.moveTo(256 + sx * 10, 130); ctx.lineTo(256 + sx * 38, 108); ctx.lineTo(256 + sx * 50, 78); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(256 + sx * 12, 142); ctx.lineTo(256 + sx * 48, 128); ctx.lineTo(256 + sx * 62, 100); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(256 + sx * 14, 154); ctx.lineTo(256 + sx * 50, 160); ctx.lineTo(256 + sx * 62, 198); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(256 + sx * 12, 166); ctx.lineTo(256 + sx * 42, 184); ctx.lineTo(256 + sx * 50, 218); ctx.stroke();
      });
    } else if (hero === 'batman') {
      // Dark Slate Suit + Charcoal Chest + Gold Utility Belt + Bat Emblem
      ctx.fillStyle = '#1b2230';
      ctx.fillRect(0, 0, 512, 512);
      ctx.fillStyle = '#475569';
      ctx.fillRect(70, 0, 372, 275);
      // Gold Utility Belt with Pouches
      ctx.fillStyle = '#eab308';
      ctx.fillRect(0, 265, 512, 58);
      ctx.strokeStyle = '#0b090a'; ctx.lineWidth = 6;
      ctx.strokeRect(0, 265, 512, 58);
      [90, 170, 256, 342, 422].forEach((px) => {
        ctx.fillStyle = px === 256 ? '#facc15' : '#ca8a04';
        ctx.fillRect(px - 22, 272, 44, 44);
        ctx.strokeRect(px - 22, 272, 44, 44);
      });
      // Yellow-Gold Oval + Black Bat Emblem
      ctx.fillStyle = '#facc15';
      ctx.beginPath(); ctx.ellipse(256, 138, 76, 46, 0, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#0b090a'; ctx.lineWidth = 6; ctx.stroke();
      ctx.fillStyle = '#0b090a';
      ctx.beginPath();
      ctx.moveTo(256, 110);
      ctx.lineTo(266, 122); ctx.lineTo(286, 114); ctx.quadraticCurveTo(318, 122, 316, 142);
      ctx.quadraticCurveTo(294, 144, 284, 158); ctx.quadraticCurveTo(270, 150, 256, 168);
      ctx.quadraticCurveTo(242, 150, 228, 158); ctx.quadraticCurveTo(218, 144, 196, 142);
      ctx.quadraticCurveTo(194, 122, 226, 114); ctx.lineTo(246, 122);
      ctx.closePath(); ctx.fill();
    } else if (hero === 'legobatman') {
      // LEGO Batman: Glossy dark charcoal brick torso, abs lines, yellow Bat-symbol & gold utility belt
      ctx.fillStyle = '#1e2430';
      ctx.fillRect(0, 0, 512, 512);
      ctx.fillStyle = '#2b3344';
      ctx.fillRect(60, 0, 392, 275);
      ctx.fillStyle = '#facc15';
      ctx.fillRect(0, 265, 512, 58);
      ctx.strokeStyle = '#0b090a'; ctx.lineWidth = 6;
      ctx.strokeRect(0, 265, 512, 58);
      [90, 170, 256, 342, 422].forEach((px) => {
        ctx.fillStyle = px === 256 ? '#fde047' : '#eab308';
        ctx.fillRect(px - 22, 272, 44, 44);
        ctx.strokeRect(px - 22, 272, 44, 44);
      });
      ctx.fillStyle = '#facc15';
      ctx.beginPath(); ctx.ellipse(256, 138, 76, 46, 0, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
      ctx.fillStyle = '#0b090a';
      ctx.beginPath();
      ctx.moveTo(256, 110); ctx.lineTo(266, 122); ctx.lineTo(286, 114); ctx.quadraticCurveTo(318, 122, 316, 142);
      ctx.quadraticCurveTo(294, 144, 284, 158); ctx.quadraticCurveTo(270, 150, 256, 168);
      ctx.quadraticCurveTo(242, 150, 228, 158); ctx.quadraticCurveTo(218, 144, 196, 142);
      ctx.quadraticCurveTo(194, 122, 226, 114); ctx.lineTo(246, 122); ctx.closePath(); ctx.fill();
    } else if (hero === 'hulk') {
      // Hulk: Green muscular chest + jagged tattered purple shorts
      ctx.fillStyle = '#4abc52';
      ctx.fillRect(0, 0, 512, 512);
      // Pectoral & ab definition lines
      ctx.strokeStyle = '#166534'; ctx.lineWidth = 5; ctx.lineCap = 'round';
      ctx.beginPath(); ctx.moveTo(130, 140); ctx.quadraticCurveTo(195, 165, 250, 145); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(382, 140); ctx.quadraticCurveTo(317, 165, 262, 145); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(256, 155); ctx.lineTo(256, 255); ctx.stroke();
      // Tattered Purple Shorts
      ctx.fillStyle = '#7e22ce';
      ctx.beginPath();
      ctx.moveTo(0, 285);
      for (let x = 0; x <= 512; x += 32) {
        ctx.lineTo(x, 285 + (x % 64 === 0 ? -18 : 14));
      }
      ctx.lineTo(512, 512); ctx.lineTo(0, 512); ctx.closePath(); ctx.fill();
      ctx.strokeStyle = '#0b090a'; ctx.lineWidth = 6; ctx.stroke();
    } else {
      // Iron Man: Red & Gold Armor + Glowing Cyan Arc Reactor
      ctx.fillStyle = '#c6222a';
      ctx.fillRect(0, 0, 512, 512);
      // Gold abdominal & side armor plates
      ctx.fillStyle = '#f5b624';
      ctx.fillRect(156, 200, 200, 120);
      ctx.fillRect(0, 255, 512, 44);
      ctx.strokeStyle = '#0c080a'; ctx.lineWidth = 6;
      ctx.strokeRect(156, 200, 200, 120);
      ctx.strokeRect(0, 255, 512, 44);
      // Chest panel lines
      ctx.beginPath();
      ctx.moveTo(130, 0); ctx.lineTo(175, 195);
      ctx.moveTo(382, 0); ctx.lineTo(337, 195);
      ctx.stroke();
      // Glowing Cyan Arc Reactor
      ctx.fillStyle = '#0f172a';
      ctx.beginPath(); ctx.arc(256, 128, 52, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
      ctx.fillStyle = '#38bdf8';
      ctx.beginPath(); ctx.arc(256, 128, 38, 0, Math.PI * 2); ctx.fill();
      ctx.fillStyle = '#e0f2fe';
      ctx.beginPath(); ctx.arc(256, 128, 20, 0, Math.PI * 2); ctx.fill();
    }

    const tex = new THREE.CanvasTexture(c);
    tex.needsUpdate = true;
    torsoTexCache[hero] = tex;
    return tex;
  }

function makeWebbedRedTexture() {
    if (webbedRedTexture) return webbedRedTexture;
    const c = document.createElement('canvas');
    c.width = 256; c.height = 256;
    const ctx = c.getContext('2d');
    ctx.fillStyle = '#cf4230';
    ctx.fillRect(0, 0, 256, 256);
    ctx.strokeStyle = '#0c080a';
    ctx.lineWidth = 4.5;
    for (let x = 32; x < 256; x += 64) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, 256); ctx.stroke(); }
    for (let y = 42; y < 256; y += 64) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(256, y); ctx.stroke(); }
    webbedRedTexture = new THREE.CanvasTexture(c);
    webbedRedTexture.needsUpdate = true;
    return webbedRedTexture;
  }

export function attachHero3D(container, options = {}) {
  if (!container || typeof container.appendChild !== 'function') {
    throw new TypeError('attachHero3D: container must be a DOM element');
  }
  if (!THREE || !THREE.WebGLRenderer) {
    throw new TypeError('attachHero3D: three.js is not loaded (expected /vendor-three.min.js)');
  }
  const doc = container.ownerDocument || (typeof document !== 'undefined' ? document : null);
  if (!doc) throw new TypeError('attachHero3D: no document available');

  const initial = normalizeHero3DState(options);
  // Start from the default model so the boot setters always apply the requested state.
  let hero = 'spidey';
  let expression = 'neutral';
  let animation = 'idle';
  let caption = initial.caption;
  let reducedMotion = !!options.reducedMotion;
  let paused = false;
  let destroyed = false;

  const canvas = doc.createElement('canvas');
  canvas.className = 'hero3d-canvas' + (options.className ? ' ' + String(options.className) : '');
  canvas.setAttribute('aria-hidden', 'true');
  container.appendChild(canvas);

  const captionEl = doc.createElement('div');
  captionEl.className = 'hero3d-caption';
  captionEl.setAttribute('aria-hidden', 'true');
  captionEl.hidden = true;
  container.appendChild(captionEl);

  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, powerPreference: 'low-power' });
  } catch (err) {
    canvas.remove();
    captionEl.remove();
    throw err;
  }
  renderer.setPixelRatio(Math.min(doc.defaultView?.devicePixelRatio || 1, 2));
  renderer.setClearColor(0x000000, 0);

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 100);
  camera.position.set(0, 0, 64); // quarter of the demo size, tucked beside the chat column

  scene.add(new THREE.AmbientLight(0xffffff, 0.45));
  const keyLight = new THREE.DirectionalLight(0xffffff, 0.56);
  keyLight.position.set(4, 7, 6);
  scene.add(keyLight);
  const rimLight = new THREE.DirectionalLight(0x93c5fd, 0.25);
  rimLight.position.set(-5, 3, -4);
  scene.add(rimLight);
  scene.add(new THREE.HemisphereLight(0xffffff, 0x1e293b, 0.30));

  // 2-Tone Cel-Shading Gradient
  const gradData = new Uint8Array([205, 205, 205, 255, 255, 255, 255, 255]);
  const toonGrad = new THREE.DataTexture(gradData, 2, 1, THREE.RGBAFormat);
  toonGrad.minFilter = THREE.NearestFilter;
  toonGrad.magFilter = THREE.NearestFilter;
  toonGrad.needsUpdate = true;

  // Materials swapped dynamically per hero
  const headMat = new THREE.MeshToonMaterial({ map: getHeadTexture('spidey', 'neutral'), gradientMap: toonGrad });
  const torsoMat = new THREE.MeshToonMaterial({ map: getTorsoTexture('spidey'), gradientMap: toonGrad });
  const spideyWebbedRedMat = new THREE.MeshToonMaterial({ map: makeWebbedRedTexture(), gradientMap: toonGrad });
  const upperArmMat = new THREE.MeshToonMaterial({ color: 0x233040, gradientMap: toonGrad });
  const foreArmMat = new THREE.MeshToonMaterial({ map: makeWebbedRedTexture(), gradientMap: toonGrad });
  const bootMat = new THREE.MeshToonMaterial({ map: makeWebbedRedTexture(), gradientMap: toonGrad });
  const outlineMat = new THREE.MeshBasicMaterial({ color: 0x0b090a, side: THREE.BackSide });

  function addWithOutline(parent, geo, mat, outlineScale = 1.09) {
    const mesh = new THREE.Mesh(geo, mat);
    const outline = new THREE.Mesh(geo, outlineMat);
    outline.scale.setScalar(outlineScale);
    parent.add(mesh, outline);
    return mesh;
  }


    // ============================================================
    // 2. BUILD EXACT PROPORTIONED 3D CHIBI RIG
    // ============================================================
    const mascot = new THREE.Group();
    scene.add(mascot);

    const bodyPivot = new THREE.Group();
    mascot.add(bodyPivot);

    const hangHandAnchor = new THREE.Object3D();
    const hangAnchorWorld = new THREE.Vector3();

    // Torso
    const bodyShape = new THREE.Shape();
    bodyShape.moveTo(-0.27, 0.02);
    bodyShape.quadraticCurveTo(-0.28, -0.14, -0.26, -0.26);
    bodyShape.quadraticCurveTo(-0.31, -0.38, -0.32, -0.53);
    bodyShape.lineTo(-0.145, -0.53);
    bodyShape.absarc(0, -0.53, 0.145, Math.PI, 0, true);
    bodyShape.lineTo(0.32, -0.53);
    bodyShape.quadraticCurveTo(0.31, -0.38, 0.26, -0.26);
    bodyShape.quadraticCurveTo(0.28, -0.14, 0.27, 0.02);
    bodyShape.quadraticCurveTo(0, 0.05, -0.27, 0.02);

    const torsoGeo = new THREE.ExtrudeGeometry(bodyShape, {
      depth: 0.14, bevelEnabled: true, bevelThickness: 0.10, bevelSize: 0.035, bevelSegments: 10, curveSegments: 24
    });
    torsoGeo.translate(0, 0, -0.07);
    const tPos = torsoGeo.attributes.position;
    const tUv = torsoGeo.attributes.uv;
    for (let i = 0; i < tPos.count; i++) {
      tUv.setXY(i, Math.max(0, Math.min(1, (tPos.getX(i) + 0.355) / 0.71)), Math.max(0, Math.min(1, (tPos.getY(i) + 0.565) / 0.635)));
    }
    tUv.needsUpdate = true;

    const torsoMesh = new THREE.Mesh(torsoGeo, torsoMat);
    const torsoOutline = new THREE.Mesh(torsoGeo, outlineMat);
    torsoOutline.scale.set(1.045, 1.03, 1.05);
    bodyPivot.add(torsoMesh, torsoOutline);

    // Head (1.35:1 oblate)
    const headGroup = new THREE.Group();
    headGroup.position.set(0, 0.60, 0);
    bodyPivot.add(headGroup);

    const headGeo = new THREE.SphereGeometry(0.72, 48, 36);
    const headSphere = new THREE.Mesh(headGeo, headMat);
    headSphere.scale.set(1.111, 0.823, 0.944);
    headSphere.rotation.y = -Math.PI / 2;
    const headOutline = new THREE.Mesh(headGeo, outlineMat);
    headOutline.scale.set(1.148, 0.858, 0.978);
    headGroup.add(headSphere, headOutline);


    // Hulk 3D Gamma Ray Beams (shoot outward during Gamma Rage)
    const gammaRaysGroup = new THREE.Group();
    const gammaRayGeo = new THREE.CylinderGeometry(0.03, 0.16, 4.2, 10);
    gammaRayGeo.translate(0, 2.2, 0);
    const gammaRayMat = new THREE.MeshBasicMaterial({ color: 0x4ade80, transparent: true, opacity: 0.82 });
    const gammaCoreMat = new THREE.MeshBasicMaterial({ color: 0xd9f99d, transparent: true, opacity: 0.92 });
    [
      [0, 0, -0.55, 0.25],
      [0, 0, 0.55, 0.25],
      [0, 0, -1.15, 0.15],
      [0, 0, 1.15, 0.15],
      [0, 0, -0.25, 0.95],
      [0, 0, 0.25, 0.95]
    ].forEach(([gx, gy, rz, rx]) => {
      const ray = new THREE.Mesh(gammaRayGeo, gammaRayMat);
      ray.rotation.set(rx, 0, rz);
      const core = new THREE.Mesh(gammaRayGeo, gammaCoreMat);
      core.scale.set(0.45, 1, 0.45);
      ray.add(core);
      gammaRaysGroup.add(ray);
    });
    gammaRaysGroup.position.set(0, -0.1, 0.1);
    gammaRaysGroup.visible = false;
    mascot.add(gammaRaysGroup);

    // Hulk 3D Animated Roaring Mouth (opens/chomps dynamically during moves)
    const hulkMouthGroup = new THREE.Group();
    const mouthCavity = new THREE.Mesh(
      new THREE.BoxGeometry(0.32, 0.14, 0.08),
      new THREE.MeshBasicMaterial({ color: 0x1e090c })
    );
    const upperTeeth = new THREE.Mesh(
      new THREE.BoxGeometry(0.28, 0.04, 0.09),
      new THREE.MeshBasicMaterial({ color: 0xffffff })
    );
    upperTeeth.position.y = 0.045;
    const lowerTeeth = new THREE.Mesh(
      new THREE.BoxGeometry(0.28, 0.04, 0.09),
      new THREE.MeshBasicMaterial({ color: 0xffffff })
    );
    lowerTeeth.position.y = -0.045;
    hulkMouthGroup.add(mouthCavity, upperTeeth, lowerTeeth);
    hulkMouthGroup.position.set(0, -0.25, 0.62);
    hulkMouthGroup.visible = false;
    headGroup.add(hulkMouthGroup);

    // Hulk Shockwave Ring
    const shockRing = new THREE.Mesh(
      new THREE.RingGeometry(0.35, 0.55, 28),
      new THREE.MeshBasicMaterial({ color: 0x4ade80, side: THREE.DoubleSide, transparent: true, opacity: 0.85 })
    );
    shockRing.rotation.x = Math.PI / 2;
    shockRing.position.set(0, -0.72, 0);
    shockRing.visible = false;
    mascot.add(shockRing);

    // LEGO Master-Builder floating bricks
    const legoBricksGroup = new THREE.Group();
    const brickColors = [0xfacc15, 0xef4444, 0x3b82f6, 0x22c55e];
    for (let i = 0; i < 4; i++) {
      const bMesh = addWithOutline(
        legoBricksGroup,
        new THREE.BoxGeometry(0.22, 0.14, 0.14),
        new THREE.MeshToonMaterial({ color: brickColors[i], gradientMap: toonGrad }),
        1.12
      );
      bMesh.userData.phase = i * (Math.PI / 2);
    }
    legoBricksGroup.visible = false;
    mascot.add(legoBricksGroup);

    // --- BATMAN SPECIFIC 3D PARTS (Ears + Scalloped Cape + Batarang) ---
    const batEarsGroup = new THREE.Group();
    const earGeo = new THREE.ConeGeometry(0.14, 0.42, 12);
    const batCowlMat = new THREE.MeshToonMaterial({ color: 0x222938, gradientMap: toonGrad });
    [-1, 1].forEach((side) => {
      const ear = addWithOutline(batEarsGroup, earGeo, batCowlMat, 1.10);
      ear.position.set(side * 0.48, 0.62, 0.04);
      ear.rotation.z = -side * 0.14;
      batEarsGroup.children[batEarsGroup.children.length - 1].position.set(side * 0.48, 0.62, 0.04);
      batEarsGroup.children[batEarsGroup.children.length - 1].rotation.z = -side * 0.14;
    });
    batEarsGroup.visible = false;
    headGroup.add(batEarsGroup);

    // LEGO Batman Cylindrical Head + Top Stud (swapped in when legobatman is active)
    const legoHeadGroup = new THREE.Group();
    const legoCylGeo = new THREE.CylinderGeometry(0.66, 0.66, 1.05, 32);
    const legoCylMesh = new THREE.Mesh(legoCylGeo, headMat);
    legoCylMesh.rotation.y = Math.PI;
    const legoCylOut = new THREE.Mesh(legoCylGeo, outlineMat);
    legoCylOut.scale.set(1.05, 1.04, 1.05);
    const legoStudGeo = new THREE.CylinderGeometry(0.24, 0.24, 0.16, 20);
    legoStudGeo.translate(0, 0.58, 0);
    addWithOutline(legoHeadGroup, legoStudGeo, batCowlMat, 1.08);
    legoHeadGroup.add(legoCylMesh, legoCylOut);
    legoHeadGroup.visible = false;
    headGroup.add(legoHeadGroup);

    const batCapeGroup = new THREE.Group();
    const capeShape = new THREE.Shape();
    capeShape.moveTo(-0.30, 0.02);
    capeShape.lineTo(0.30, 0.02);
    capeShape.lineTo(0.56, -0.72);
    capeShape.quadraticCurveTo(0.36, -0.58, 0.20, -0.76);
    capeShape.quadraticCurveTo(0, -0.60, -0.20, -0.76);
    capeShape.quadraticCurveTo(-0.36, -0.58, -0.56, -0.72);
    capeShape.closePath();
    const capeGeo = new THREE.ExtrudeGeometry(capeShape, { depth: 0.03, bevelEnabled: false });
    const batCapeMat = new THREE.MeshToonMaterial({ color: 0x161c28, gradientMap: toonGrad });
    addWithOutline(batCapeGroup, capeGeo, batCapeMat, 1.04);
    batCapeGroup.position.set(0, 0.02, -0.20);
    batCapeGroup.rotation.x = 0.14;
    batCapeGroup.visible = false;
    bodyPivot.add(batCapeGroup);

    const batarangShape = new THREE.Shape();
    batarangShape.moveTo(0, 0.05);
    batarangShape.lineTo(0.24, 0.12);
    batarangShape.quadraticCurveTo(0.14, 0.02, 0.18, -0.06);
    batarangShape.lineTo(0, -0.02);
    batarangShape.lineTo(-0.18, -0.06);
    batarangShape.quadraticCurveTo(-0.14, 0.02, -0.24, 0.12);
    batarangShape.closePath();
    const batarangMesh = new THREE.Mesh(
      new THREE.ExtrudeGeometry(batarangShape, { depth: 0.02, bevelEnabled: false }),
      new THREE.MeshBasicMaterial({ color: 0xfacc15 })
    );
    batarangMesh.visible = false;
    scene.add(batarangMesh);

    // --- IRON MAN SPECIFIC 3D PARTS (Thruster Flames & Unibeam) ---
    const flameMat = new THREE.MeshBasicMaterial({ color: 0x38bdf8, transparent: true, opacity: 0.92 });
    const flameGeo = new THREE.ConeGeometry(0.075, 0.34, 12);
    flameGeo.rotateX(Math.PI); // Point downward
    flameGeo.translate(0, -0.17, 0);

    const unibeamGeo = new THREE.CylinderGeometry(0.06, 0.22, 4.5, 16);
    unibeamGeo.rotateX(Math.PI / 2);
    unibeamGeo.translate(0, 0, 2.35);
    const unibeamMesh = new THREE.Mesh(unibeamGeo, new THREE.MeshBasicMaterial({ color: 0x7dd3fc, transparent: true, opacity: 0.85 }));
    unibeamMesh.position.set(0, -0.14, 0.16);
    unibeamMesh.visible = false;
    bodyPivot.add(unibeamMesh);

    // --- THINKING & SEARCHING PROPS -------------------------------------------------------
    // Thought bubbles trail up from the head while the model is thinking.
    const thinkGroup = new THREE.Group();
    const thinkMat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.9 });
    [[0.02, 0.88, 0.04, 0.11], [0.26, 1.09, 0.02, 0.075], [0.44, 1.25, 0.0, 0.05]].forEach(([bx, by, bz, r]) => {
      const bubble = new THREE.Mesh(new THREE.SphereGeometry(r, 16, 12), thinkMat);
      bubble.position.set(bx, by, bz);
      thinkGroup.add(bubble);
    });
    const thinkRing = new THREE.Mesh(
      new THREE.TorusGeometry(0.17, 0.028, 8, 22),
      new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.85 })
    );
    thinkRing.position.set(0.12, 0.98, 0.02);
    thinkGroup.add(thinkRing);
    thinkGroup.visible = false;
    headGroup.add(thinkGroup);

    // A magnifier the hero holds out while scanning files or the web.
    const searchGroup = new THREE.Group();
    const lensRing = new THREE.Mesh(
      new THREE.TorusGeometry(0.28, 0.045, 10, 26),
      new THREE.MeshBasicMaterial({ color: 0xffffff })
    );
    const lensGlass = new THREE.Mesh(
      new THREE.CircleGeometry(0.25, 26),
      new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.16, side: THREE.DoubleSide })
    );
    const lensHandle = new THREE.Mesh(
      new THREE.CylinderGeometry(0.03, 0.04, 0.38, 10),
      new THREE.MeshBasicMaterial({ color: 0xffffff })
    );
    lensHandle.position.set(0, -0.42, 0);
    searchGroup.add(lensRing, lensGlass, lensHandle);
    searchGroup.position.set(0.62, 0.04, 0.58);
    searchGroup.rotation.set(0.06, -0.35, -0.28);
    searchGroup.visible = false;
    bodyPivot.add(searchGroup);

    // Ground scan ping that spreads out while searching.
    const scanRing = new THREE.Mesh(
      new THREE.TorusGeometry(0.85, 0.028, 8, 44),
      new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.55 })
    );
    scanRing.rotation.x = Math.PI / 2;
    scanRing.position.set(0, -0.62, 0.1);
    scanRing.visible = false;
    mascot.add(scanRing);

    // --- EXPRESSION ACCESSORIES ---
    const heartsGroup = new THREE.Group();
    const heartShape = new THREE.Shape();
    heartShape.moveTo(0, 0.04);
    heartShape.bezierCurveTo(0.05, 0.14, 0.18, 0.12, 0.18, 0.02);
    heartShape.bezierCurveTo(0.18, -0.08, 0.04, -0.15, 0, -0.20);
    heartShape.bezierCurveTo(-0.04, -0.15, -0.18, -0.08, -0.18, 0.02);
    heartShape.bezierCurveTo(-0.18, 0.12, -0.05, 0.14, 0, 0.04);
    const heartGeo = new THREE.ExtrudeGeometry(heartShape, { depth: 0.04, bevelEnabled: true, bevelSize: 0.015, bevelThickness: 0.015 });
    const heartMat = new THREE.MeshBasicMaterial({ color: 0xef4444 });
    [[-0.95, 0.65, 0.15, 0.25], [0.92, 0.72, 0.15, -0.22], [1.08, 0.18, 0.15, -0.35]].forEach(([hx, hy, hz, hrot]) => {
      const hMesh = addWithOutline(heartsGroup, heartGeo, heartMat, 1.15);
      hMesh.position.set(hx, hy, hz); hMesh.rotation.z = hrot;
      heartsGroup.children[heartsGroup.children.length - 1].position.set(hx, hy, hz);
      heartsGroup.children[heartsGroup.children.length - 1].rotation.z = hrot;
    });
    heartsGroup.visible = false;
    headGroup.add(heartsGroup);

    const senseGroup = new THREE.Group();
    const senseShape = new THREE.Shape();
    senseShape.moveTo(0, 0); senseShape.lineTo(0.12, 0.07); senseShape.lineTo(0.08, 0.14);
    senseShape.lineTo(0.24, 0.22); senseShape.lineTo(0.10, 0.10); senseShape.lineTo(0.15, 0.04); senseShape.closePath();
    const senseGeo = new THREE.ShapeGeometry(senseShape);
    const senseMat = new THREE.MeshBasicMaterial({ color: 0xfacc15, side: THREE.DoubleSide });
    [[-0.95, 0.45, Math.PI * 0.85], [-1.02, 0, Math.PI], [-0.85, -0.45, Math.PI * 1.2],
     [0.95, 0.45, Math.PI * 0.15], [1.02, 0, 0], [0.85, -0.45, -Math.PI * 0.2]].forEach(([sx, sy, srot]) => {
      const bolt = new THREE.Mesh(senseGeo, senseMat);
      bolt.position.set(sx, sy, 0.15); bolt.rotation.z = srot;
      senseGroup.add(bolt);
    });
    senseGroup.visible = false;
    headGroup.add(senseGroup);

    const angelGroup = new THREE.Group();
    const haloMesh = new THREE.Mesh(new THREE.TorusGeometry(0.38, 0.032, 12, 32), new THREE.MeshBasicMaterial({ color: 0xfbbf24 }));
    haloMesh.rotation.x = Math.PI / 2 - 0.15; haloMesh.position.set(0, 0.82, 0);
    angelGroup.add(haloMesh);
    const wingShape = new THREE.Shape();
    wingShape.moveTo(0, 0); wingShape.quadraticCurveTo(0.32, 0.24, 0.42, 0.06);
    wingShape.quadraticCurveTo(0.36, -0.06, 0.24, -0.04); wingShape.quadraticCurveTo(0.22, -0.14, 0.10, -0.10);
    wingShape.quadraticCurveTo(0.06, -0.18, 0, -0.06);
    const wingGeo = new THREE.ExtrudeGeometry(wingShape, { depth: 0.025, bevelEnabled: false });
    const whiteWingMat = new THREE.MeshBasicMaterial({ color: 0xf8fafc });
    const wingL = addWithOutline(angelGroup, wingGeo, whiteWingMat, 1.12);
    wingL.position.set(-0.24, -0.75, -0.22); wingL.scale.x = -1;
    angelGroup.children[angelGroup.children.length - 1].position.set(-0.24, -0.75, -0.22);
    angelGroup.children[angelGroup.children.length - 1].scale.x = -1;
    const wingR = addWithOutline(angelGroup, wingGeo, whiteWingMat, 1.12);
    wingR.position.set(0.24, -0.75, -0.22);
    angelGroup.children[angelGroup.children.length - 1].position.set(0.24, -0.75, -0.22);
    angelGroup.visible = false;
    headGroup.add(angelGroup);

    const devilGroup = new THREE.Group();
    const hornGeo = new THREE.ConeGeometry(0.085, 0.26, 12);
    const hornMat = new THREE.MeshToonMaterial({ color: 0x334155, gradientMap: toonGrad });
    [-1, 1].forEach((side) => {
      const h = addWithOutline(devilGroup, hornGeo, hornMat, 1.12);
      h.position.set(side * 0.42, 0.62, 0.08); h.rotation.z = -side * 0.38;
      devilGroup.children[devilGroup.children.length - 1].position.set(side * 0.42, 0.62, 0.08);
      devilGroup.children[devilGroup.children.length - 1].rotation.z = -side * 0.38;
    });
    const batMat = new THREE.MeshBasicMaterial({ color: 0x1e293b });
    const batL = addWithOutline(devilGroup, wingGeo, batMat, 1.12);
    batL.position.set(-0.24, -0.75, -0.22); batL.scale.x = -1;
    devilGroup.children[devilGroup.children.length - 1].position.set(-0.24, -0.75, -0.22);
    devilGroup.children[devilGroup.children.length - 1].scale.x = -1;
    const batR = addWithOutline(devilGroup, wingGeo, batMat, 1.12);
    batR.position.set(0.24, -0.75, -0.22);
    devilGroup.children[devilGroup.children.length - 1].position.set(0.24, -0.75, -0.22);
    devilGroup.visible = false;
    headGroup.add(devilGroup);

    // Arms
    function makeArm(side) {
      const pivot = new THREE.Group();
      pivot.position.set(side * 0.28, -0.04, 0.02);
      pivot.rotation.z = side * 0.52;

      addWithOutline(pivot, new THREE.SphereGeometry(0.078, 14, 12), upperArmMat, 1.14);
      const upperGeo = new THREE.CylinderGeometry(0.078, 0.070, 0.20, 14);
      upperGeo.translate(0, -0.10, 0);
      addWithOutline(pivot, upperGeo, upperArmMat, 1.12);

      const elbow = new THREE.Group();
      elbow.position.set(0, -0.20, 0);
      elbow.rotation.z = -side * 1.42;
      elbow.rotation.x = -0.18;

      addWithOutline(elbow, new THREE.SphereGeometry(0.072, 12, 10), foreArmMat, 1.14);
      const foreGeo = new THREE.CylinderGeometry(0.070, 0.062, 0.165, 14);
      foreGeo.translate(0, -0.082, 0);
      addWithOutline(elbow, foreGeo, foreArmMat, 1.12);

      const handGroup = new THREE.Group();
      handGroup.position.y = -0.175;
      const fistGeo = new THREE.SphereGeometry(0.072, 14, 12);
      fistGeo.scale(0.95, 0.90, 0.85);
      addWithOutline(handGroup, fistGeo, foreArmMat, 1.14);

      const palmFlame = new THREE.Mesh(flameGeo, flameMat);
      palmFlame.position.set(0, -0.06, 0);
      palmFlame.visible = false;
      handGroup.add(palmFlame);

      elbow.add(handGroup);
      pivot.add(elbow);
      pivot.userData.elbow = elbow;
      pivot.userData.hand = handGroup;
      pivot.userData.flame = palmFlame;
      return pivot;
    }
    const armL = makeArm(-1);
    const armR = makeArm(1);
    armR.userData.hand.add(hangHandAnchor);
    bodyPivot.add(armL, armR);

    // Legs with Knee Joint + Boot Flame
    function makeLeg(side) {
      const pivot = new THREE.Group();
      pivot.position.set(side * 0.23, -0.50, 0);
      pivot.rotation.z = side * 0.12;

      const thighGeo = new THREE.SphereGeometry(0.098, 14, 12);
      thighGeo.scale(1.0, 1.15, 1.0);
      thighGeo.translate(0, -0.03, 0);
      addWithOutline(pivot, thighGeo, upperArmMat, 1.10);

      const knee = new THREE.Group();
      knee.position.set(0, -0.06, 0);

      const bootGeo = new THREE.CylinderGeometry(0.090, 0.110, 0.16, 16);
      bootGeo.translate(0, -0.08, 0);
      addWithOutline(knee, bootGeo, bootMat, 1.10);

      const footGeo = new THREE.SphereGeometry(0.115, 16, 12);
      footGeo.scale(1.12, 0.56, 1.32);
      footGeo.translate(side * 0.035, -0.185, 0.04);
      addWithOutline(knee, footGeo, bootMat, 1.10);

      const bootFlame = new THREE.Mesh(flameGeo, flameMat);
      bootFlame.position.set(side * 0.02, -0.22, 0.02);
      bootFlame.visible = false;
      knee.add(bootFlame);

      pivot.add(knee);
      pivot.userData.knee = knee;
      pivot.userData.flame = bootFlame;
      return pivot;
    }
    const legL = makeLeg(-1);
    const legR = makeLeg(1);
    bodyPivot.add(legL, legR);

    // 3D Cable / Web / Grapple Line
    const webGroup = new THREE.Group();
    const webCordMat = new THREE.MeshBasicMaterial({ color: 0xf1f5f9 });
    const webCordGeo = new THREE.CylinderGeometry(0.022, 0.028, 1, 8);
    webCordGeo.translate(0, 0.5, 0);
    const webCord = new THREE.Mesh(webCordGeo, webCordMat);
    webGroup.add(webCord);

    const helixPts = [];
    for (let i = 0; i <= 40; i++) {
      const u = i / 40;
      const ang = u * Math.PI * 10;
      const rad = 0.045 + Math.sin(u * Math.PI) * 0.035;
      helixPts.push(new THREE.Vector3(Math.cos(ang) * rad, u, Math.sin(ang) * rad));
    }
    const helixMesh = new THREE.Mesh(
      new THREE.TubeGeometry(new THREE.CatmullRomCurve3(helixPts), 40, 0.011, 6, false),
      new THREE.MeshBasicMaterial({ color: 0xcbd5e1 })
    );
    webGroup.add(helixMesh);
    webGroup.visible = false;
    scene.add(webGroup);


  // ---- viewport + screen-space positioning ------------------------------------------------
  let width = 1, height = 1;
  let screenX = 0, screenY = 0;
  let targetScreenX = 0, targetScreenY = 0;
  let placed = false;
  let positioned = false;
  let zipTimer = 0;
  let lookX = 0, lookY = 0, targetLookX = 0, targetLookY = 0;
  let animationStartedAt = 0;
  let animationTime = 0;
  let raf = 0;

  const clock = new THREE.Clock();

  function screenToWorld(sx, sy) {
    const ndcX = (sx / width) * 2 - 1;
    const ndcY = -(sy / height) * 2 + 1;
    const v = new THREE.Vector3(ndcX, ndcY, 0.5).unproject(camera);
    const dir = v.sub(camera.position).normalize();
    const dist = -camera.position.z / dir.z;
    return camera.position.clone().add(dir.multiplyScalar(dist));
  }

  function measure() {
    const rect = container.getBoundingClientRect ? container.getBoundingClientRect() : null;
    const w = Math.round((rect && rect.width) || container.clientWidth || 1);
    const h = Math.round((rect && rect.height) || container.clientHeight || 1);
    return { width: Math.max(1, w), height: Math.max(1, h) };
  }

  function resize() {
    const size = measure();
    width = size.width;
    height = size.height;
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    if (!placed) {
      placed = true;
      screenX = targetScreenX = width * 0.72;
      screenY = targetScreenY = height * 0.68;
    } else {
      targetScreenX = Math.max(60, Math.min(width - 60, targetScreenX));
      targetScreenY = Math.max(100, Math.min(height - 100, targetScreenY));
      screenX = Math.max(60, Math.min(width - 60, screenX));
      screenY = Math.max(100, Math.min(height - 100, screenY));
    }
  }

  resize();

  // ---- public state setters ---------------------------------------------------------------
  function setHero(next) {
    if (destroyed) return;
    const h = HERO3D_HEROES.includes(next) ? next : 'spidey';
    if (h === hero) return;
    hero = h;
    const cfg = HERO3D_CONFIGS[hero];
    headMat.map = getHeadTexture(hero, expression);
    headMat.needsUpdate = true;
    torsoMat.map = getTorsoTexture(hero);
    torsoMat.needsUpdate = true;

    const isBat = hero === 'batman' || hero === 'legobatman';
    batEarsGroup.visible = isBat;
    batCapeGroup.visible = isBat;
    headSphere.visible = hero !== 'legobatman';
    headOutline.visible = hero !== 'legobatman';
    legoHeadGroup.visible = hero === 'legobatman';
    mascot.scale.setScalar(hero === 'hulk' ? 1.13 : 1.0);
    webCordMat.color.setHex(cfg.cordColor);

    if (hero === 'spidey') {
      upperArmMat.color.setHex(0x233040);
      foreArmMat.map = spideyWebbedRedMat.map; foreArmMat.color.setHex(0xffffff);
      bootMat.map = spideyWebbedRedMat.map; bootMat.color.setHex(0xffffff);
    } else if (hero === 'batman') {
      upperArmMat.color.setHex(0x3b4454);
      foreArmMat.map = null; foreArmMat.color.setHex(0x18202c);
      bootMat.map = null; bootMat.color.setHex(0x18202c);
    } else if (hero === 'legobatman') {
      upperArmMat.color.setHex(0x1e2430);
      foreArmMat.map = null; foreArmMat.color.setHex(0x1e2430);
      bootMat.map = null; bootMat.color.setHex(0x18202c);
    } else if (hero === 'hulk') {
      upperArmMat.color.setHex(0x4abc52);
      foreArmMat.map = null; foreArmMat.color.setHex(0x4abc52);
      bootMat.map = null; bootMat.color.setHex(0x4abc52);
    } else {
      upperArmMat.color.setHex(0xc6222a);
      foreArmMat.map = null; foreArmMat.color.setHex(0xf5b624);
      bootMat.map = null; bootMat.color.setHex(0xc6222a);
    }
    foreArmMat.needsUpdate = true;
    bootMat.needsUpdate = true;

    // Hero-coloured thinking / searching props.
    const accent = HERO3D_ACCENTS[hero] ?? 0xffffff;
    thinkGroup.children.forEach(child => child.material.color.setHex(accent));
    searchGroup.children.forEach(child => child.material.color.setHex(accent));
    searchGroup.scale.setScalar(hero === 'hulk' ? 1.25 : 1);
    scanRing.material.color.setHex(accent);

    setAnimation('idle');
  }

  function setExpression(next) {
    if (destroyed) return;
    const expr = HERO3D_EXPRESSIONS.includes(next) ? next : 'neutral';
    if (expr === expression) return;
    expression = expr;
    headMat.map = getHeadTexture(hero, expression);
    headMat.needsUpdate = true;
    heartsGroup.visible = expr === 'love';
    senseGroup.visible = expr === 'spideysense';
    angelGroup.visible = expr === 'angel';
    devilGroup.visible = expr === 'devil';
  }

  function setAnimation(next) {
    if (destroyed) return;
    const requested = ANIMATION_ALIASES[next] || next;
    const anim = ALL_ANIMATIONS.has(requested) ? requested : 'idle';
    if (anim === animation && animationStartedAt !== 0) return;
    animation = anim;
    animationStartedAt = clock.elapsedTime;
    animationTime = 0;
    zipTimer = 0;
  }

  function setCaption(next) {
    if (destroyed) return;
    caption = clampCaption(next);
    captionEl.textContent = caption;
    captionEl.hidden = !caption;
  }

  // `box` is a top-left box in container pixels (the roam planner's footprint).
  function moveTo(box) {
    if (destroyed || !box) return;
    const bw = box.width ?? box.w ?? 0;
    const bh = box.height ?? box.h ?? 0;
    const cx = (box.left ?? box.x ?? 0) + bw / 2;
    const cy = (box.top ?? box.y ?? 0) + bh / 2;
    const dx = cx - targetScreenX;
    const dy = cy - targetScreenY;
    targetScreenX = Math.max(40, Math.min(width - 40, cx));
    targetScreenY = Math.max(70, Math.min(height - 70, cy));
    if (!positioned) {
      positioned = true;
      screenX = targetScreenX;
      screenY = targetScreenY;
    }
    if (Math.hypot(dx, dy) > 90) {
      // Long hop: replay the original web/grapple/jet travel cord.
      zipTimer = 0.65;
    }
  }

  function setReducedMotion(on) {
    reducedMotion = !!on;
  }

  // Freeze the current frame; play resumes from the same point.
  function setPaused(on) {
    paused = !!on;
  }

  // ---- pointer look-at + resize -----------------------------------------------------------
  function onPointerMove(e) {
    if (destroyed) return;
    const rect = canvas.getBoundingClientRect();
    const dx = (e.clientX - (rect.left + screenX)) / 300;
    const dy = (e.clientY - (rect.top + screenY)) / 300;
    targetLookX = Math.max(-0.28, Math.min(0.28, dx * 0.28));
    targetLookY = Math.max(-0.18, Math.min(0.18, dy * 0.18));
  }
  let resizeObserver = null;
  if (typeof ResizeObserver === 'function') {
    resizeObserver = new ResizeObserver(() => resize());
    resizeObserver.observe(container);
  }
  const view = doc.defaultView;
  if (view) view.addEventListener('resize', resize);
  doc.addEventListener('pointermove', onPointerMove);

  // ---- boot -------------------------------------------------------------------------------
  setHero(initial.hero);
  setExpression(initial.expression);
  setAnimation(initial.animation);
  setCaption(initial.caption);

  // Start the requested animation even if setAnimation saw the default idle state.
  animation = initial.animation;
  animationStartedAt = 0;
  animationTime = 0;



    function animate() {
      if (destroyed) return;
      raf = requestAnimationFrame(animate);
      const dt = clock.getDelta();
      if (!paused && !reducedMotion) animationTime += dt;
      const t = animationTime;

      screenX += (targetScreenX - screenX) * 0.14;
      screenY += (targetScreenY - screenY) * 0.14;
      lookX += (targetLookX - lookX) * 0.12;
      lookY += (targetLookY - lookY) * 0.12;

      const worldPos = screenToWorld(screenX, screenY);
      mascot.position.copy(worldPos);

      // Reset joints
      bodyPivot.rotation.set(0, 0, 0);
      headGroup.rotation.set(0, 0, 0);
      armL.rotation.set(0, 0, -0.52); armL.userData.elbow.rotation.set(0, 0, 1.42);
      armR.rotation.set(0, 0, 0.52); armR.userData.elbow.rotation.set(0, 0, -1.42);
      legL.rotation.set(0, 0, -0.12); legL.userData.knee.rotation.set(0, 0, 0);
      legR.rotation.set(0, 0, 0.12); legR.userData.knee.rotation.set(0, 0, 0);

      batCapeGroup.rotation.set(0.14, 0, 0);
      batCapeGroup.scale.set(1, 1, 1);
      batarangMesh.visible = false;
      unibeamMesh.visible = false;
      shockRing.visible = false;
      gammaRaysGroup.visible = false;
      hulkMouthGroup.visible = false;
      legoBricksGroup.visible = false;
      thinkGroup.visible = false;
      thinkGroup.position.set(0, 0, 0);
      thinkGroup.rotation.set(0, 0, 0);
      searchGroup.visible = false;
      searchGroup.scale.setScalar(hero === 'hulk' ? 1.25 : 1);
      searchGroup.rotation.set(0.06, -0.35, -0.28);
      lensRing.rotation.set(0, 0, 0);
      lensGlass.material.opacity = 0.16;
      scanRing.visible = false;
      scanRing.scale.setScalar(1);
      scanRing.rotation.set(Math.PI / 2, 0, 0);

      const showJetFlames = hero === 'ironman' && (animation === 'flight' || animation === 'idle' || zipTimer > 0);
      legL.userData.flame.visible = showJetFlames;
      legR.userData.flame.visible = showJetFlames;
      armL.userData.flame.visible = hero === 'ironman' && animation === 'flight';
      armR.userData.flame.visible = hero === 'ironman' && (animation === 'flight' || animation === 'thwip');
      if (showJetFlames) {
        const fScale = 0.8 + Math.sin(t * 24) * 0.25;
        legL.userData.flame.scale.set(1, fScale, 1);
        legR.userData.flame.scale.set(1, fScale, 1);
        armL.userData.flame.scale.set(0.8, fScale * 0.8, 0.8);
        armR.userData.flame.scale.set(0.8, fScale * 0.8, 0.8);
      }

      if (animation === 'idle') {
        const isIronHover = hero === 'ironman';
        const isSad = expression === 'sad';
        mascot.position.y += Math.sin(t * (isIronHover ? 2.4 : isSad ? 1.8 : 3.5)) * (isSad ? 0.05 : 0.11);
        bodyPivot.rotation.y = Math.sin(t * 1.4) * 0.16 + lookX * 0.35;
        headGroup.rotation.y = lookX * 0.65;
        headGroup.rotation.x = lookY * 0.55 + (isSad ? 0.22 : 0);
        headGroup.rotation.z = Math.sin(t * 2.2) * 0.05;
        if (isSad) {
          armL.rotation.z = -0.22; armL.userData.elbow.rotation.z = 0.35;
          armR.rotation.z = 0.22; armR.userData.elbow.rotation.z = -0.35;
        } else {
          armL.rotation.z = -0.52 + Math.sin(t * 3.5) * 0.04;
          armR.rotation.z = 0.52 - Math.sin(t * 3.5) * 0.04;
        }
        if (batCapeGroup.visible) batCapeGroup.rotation.x = 0.16 + Math.sin(t * 3) * 0.05;
      } else if (animation === 'batglide') {
        // Batman spreads cape & arms wide, swooping smoothly
        mascot.position.x += Math.sin(t * 1.4) * 1.25;
        mascot.position.y += Math.sin(t * 2.8) * 0.28 + 0.15;
        bodyPivot.rotation.set(-0.65, Math.cos(t * 1.4) * 0.35, -Math.cos(t * 1.4) * 0.22);
        headGroup.rotation.x = 0.45;
        armL.rotation.set(0, 0, -1.45); armL.userData.elbow.rotation.z = 0.1;
        armR.rotation.set(0, 0, 1.45); armR.userData.elbow.rotation.z = -0.1;
        batCapeGroup.scale.set(1.65, 1.15, 1);
        batCapeGroup.rotation.x = 0.35 + Math.sin(t * 8) * 0.08;
      } else if (animation === 'batarang') {
        // Batman wind-up & spinning Batarang throw
        const cyc = (t * 0.9) % 1;
        bodyPivot.rotation.y = cyc < 0.3 ? -0.55 : 0.45;
        armR.rotation.set(-1.35 + Math.sin(cyc * Math.PI) * 0.6, 0, 0.35);
        armR.userData.elbow.rotation.z = -0.2;
        if (cyc > 0.25) {
          const p = (cyc - 0.25) / 0.75;
          batarangMesh.visible = true;
          batarangMesh.position.set(mascot.position.x + 0.25 + lookX * 2 * p, mascot.position.y + 0.15 - lookY * p, mascot.position.z + p * 4.5);
          batarangMesh.rotation.z = t * 18;
        }
      } else if (animation === 'brood') {
        // Dark Knight crouch with cape wrapped around shoulders
        mascot.position.y -= 0.15;
        bodyPivot.rotation.set(-0.22, Math.sin(t * 0.7) * 0.12, 0);
        headGroup.rotation.set(0.24 + lookY * 0.3, lookX * 0.5, 0);
        armL.rotation.set(-0.55, 0, 0.35); armL.userData.elbow.rotation.z = 1.1;
        armR.rotation.set(-0.55, 0, -0.35); armR.userData.elbow.rotation.z = -1.1;
        batCapeGroup.scale.set(1.25, 1.05, 1.2);
      } else if (animation === 'flight') {
        // Iron Man banking supersonic hover/flight
        mascot.position.x += Math.sin(t * 1.5) * 1.25;
        mascot.position.y += Math.sin(t * 3.0) * 0.28 + 0.20;
        bodyPivot.rotation.set(-0.45, Math.cos(t * 1.5) * 0.4, -Math.cos(t * 1.5) * 0.25);
        headGroup.rotation.x = 0.35;
        armL.rotation.set(0.35, 0, -0.35); armL.userData.elbow.rotation.z = 0.1;
        armR.rotation.set(0.35, 0, 0.35); armR.userData.elbow.rotation.z = -0.1;
      } else if (animation === 'smash') {
        const cyc = (t * 0.9) % 1;
        if (cyc < 0.45) {
          const u = cyc / 0.45;
          mascot.position.y += Math.sin(u * Math.PI) * 1.1;
          armL.rotation.set(-0.3, 0, -2.5); armL.userData.elbow.rotation.z = 0.2;
          armR.rotation.set(-0.3, 0, 2.5); armR.userData.elbow.rotation.z = -0.2;
        } else {
          const p = (cyc - 0.45) / 0.55;
          mascot.position.y -= 0.16;
          bodyPivot.rotation.x = -0.35;
          armL.rotation.set(-1.35, 0, 0.25); armL.userData.elbow.rotation.z = 0;
          armR.rotation.set(-1.35, 0, -0.25); armR.userData.elbow.rotation.z = 0;
          shockRing.visible = true;
          shockRing.scale.setScalar(1 + p * 3.8);
          shockRing.material.opacity = Math.max(0, 1 - p);
        }
      } else if (animation === 'thunderclap') {
        // Wind up wide, then slam palms together at center (hands touch!)
        const clap = Math.abs(Math.sin(t * 3.2));
        const spread = clap * 1.45;
        armL.rotation.set(-1.12, 0, 0.78 - spread); armL.userData.elbow.rotation.set(0, 0, 0.18);
        armR.rotation.set(-1.12, 0, -0.78 + spread); armR.userData.elbow.rotation.set(0, 0, -0.18);
        bodyPivot.rotation.x = -0.12 * (1 - clap);
        if (clap < 0.22) {
          shockRing.visible = true;
          shockRing.rotation.x = 0;
          shockRing.position.set(0, -0.1, 0.55);
          shockRing.scale.setScalar(1 + (0.22 - clap) * 13);
          shockRing.material.opacity = 0.9;
        } else {
          shockRing.rotation.x = Math.PI / 2;
          shockRing.position.set(0, -0.72, 0);
        }
      } else if (animation === 'rage') {
        mascot.position.x += Math.sin(t * 42) * 0.04;
        bodyPivot.rotation.x = -0.18;
        headGroup.rotation.x = 0.25;
        armL.rotation.set(-0.35, 0, -1.35); armL.userData.elbow.rotation.z = -1.65;
        armR.rotation.set(-0.35, 0, 1.35); armR.userData.elbow.rotation.z = 1.65;
        gammaRaysGroup.visible = true;
        const beamPulse = 0.85 + Math.sin(t * 24) * 0.28;
        gammaRaysGroup.scale.set(1, beamPulse, 1);
        gammaRaysGroup.rotation.y = t * 1.8;
        shockRing.visible = true;
        shockRing.scale.setScalar(1.2 + Math.sin(t * 10) * 0.35);
        shockRing.material.opacity = 0.75;
      } else if (animation === 'leap') {
        // 3-phase Super Leap: crouch & bend knees -> airborne tucked jump -> bent-knee landing
        const u = (t * 0.78) % 1;
        mascot.position.x += Math.sin(t * 1.3) * 1.1;
        if (u < 0.22) {
          const c = u / 0.22;
          mascot.position.y -= c * 0.20;
          bodyPivot.rotation.x = -0.28 * c;
          legL.rotation.set(-0.95 * c, 0, -0.18); legL.userData.knee.rotation.set(1.35 * c, 0, 0);
          legR.rotation.set(-0.95 * c, 0, 0.18); legR.userData.knee.rotation.set(1.35 * c, 0, 0);
          armL.rotation.set(0.65 * c, 0, -0.6); armR.rotation.set(0.65 * c, 0, 0.6);
        } else if (u < 0.78) {
          const air = (u - 0.22) / 0.56;
          const arc = Math.sin(air * Math.PI);
          mascot.position.y += arc * 1.75 - 0.10;
          bodyPivot.rotation.x = -0.25 + air * 0.35;
          legL.rotation.set(-1.15 * arc, 0, -0.24); legL.userData.knee.rotation.set(1.55 * arc, 0, 0);
          legR.rotation.set(-0.85 * arc, 0, 0.24); legR.userData.knee.rotation.set(1.35 * arc, 0, 0);
          armL.rotation.set(-0.5, 0, -2.35); armR.rotation.set(-0.5, 0, 2.35);
        } else {
          const l = 1 - (u - 0.78) / 0.22;
          mascot.position.y -= l * 0.22;
          bodyPivot.rotation.x = -0.25 * l;
          legL.rotation.set(-0.90 * l, 0, -0.22); legL.userData.knee.rotation.set(1.25 * l, 0, 0);
          legR.rotation.set(-0.90 * l, 0, 0.22); legR.userData.knee.rotation.set(1.25 * l, 0, 0);
          armL.rotation.set(-0.4, 0, -0.8); armR.rotation.set(-0.4, 0, 0.8);
        }
      } else if (animation === 'masterbuild') {
        bodyPivot.rotation.y = t * 4.5;
        mascot.position.y += Math.abs(Math.sin(t * 6)) * 0.18;
        armL.rotation.set(-0.85, 0, -0.4); armR.rotation.set(-0.85, 0, 0.4);
        legoBricksGroup.visible = true;
        legoBricksGroup.children.forEach((b, idx) => {
          const ang = t * 4 + idx * (Math.PI / 2);
          b.position.set(Math.cos(ang) * 0.95, Math.sin(t * 5 + idx) * 0.35, Math.sin(ang) * 0.95);
          b.rotation.set(t * 2, t * 3, 0);
        });
      } else if (animation === 'rock') {
        const thrash = Math.sin(t * 12);
        bodyPivot.rotation.set(-0.18, 0.25, thrash * 0.12);
        headGroup.rotation.x = thrash * 0.28;
        armL.rotation.set(-0.95, 0, -0.35); armL.userData.elbow.rotation.z = 0.85;
        armR.rotation.set(-0.75 + thrash * 0.35, 0, 0.35); armR.userData.elbow.rotation.z = -0.5;
      } else if (animation === 'unibeam') {
        // Iron Man charges & fires glowing cyan Unibeam
        bodyPivot.rotation.set(0.12, lookX * 0.4, 0);
        armL.rotation.set(0.2, 0, -1.25); armL.userData.elbow.rotation.z = 0.2;
        armR.rotation.set(0.2, 0, 1.25); armR.userData.elbow.rotation.z = -0.2;
        unibeamMesh.visible = true;
        const beamPulse = 0.85 + Math.sin(t * 28) * 0.25;
        unibeamMesh.scale.set(beamPulse, beamPulse, 1);
      } else if (animation === 'run') {
        const patrol = t * 1.5;
        const dir = Math.cos(patrol) >= 0 ? 1 : -1;
        const stride = Math.sin(t * 10.5);
        mascot.position.x += Math.sin(patrol) * 1.35;
        mascot.position.y += Math.abs(stride) * 0.14;
        bodyPivot.rotation.set(-0.18, dir * 1.05, 0);
        headGroup.rotation.set(0.16, -dir * 0.25, 0);
        legL.rotation.set(-stride * 0.78, 0, -0.08); legL.userData.knee.rotation.set(Math.max(0, stride * 0.90), 0, 0);
        legR.rotation.set(stride * 0.78, 0, 0.08); legR.userData.knee.rotation.set(Math.max(0, -stride * 0.90), 0, 0);
        armL.rotation.set(stride * 0.78, 0, -0.18); armL.userData.elbow.rotation.set(-0.95, 0, 0.15);
        armR.rotation.set(-stride * 0.78, 0, 0.18); armR.userData.elbow.rotation.set(-0.95, 0, -0.15);
      } else if (animation === 'swing') {
        const swingAngle = Math.sin(t * 2.4) * 0.85;
        mascot.position.x += Math.sin(swingAngle) * 1.45;
        mascot.position.y += (1 - Math.cos(swingAngle)) * 0.75;
        bodyPivot.rotation.z = -swingAngle * 0.55;
        bodyPivot.rotation.y = Math.cos(t * 2.4) * 0.32;
        headGroup.rotation.z = swingAngle * 0.25;
        armR.rotation.set(-0.25, 0, 2.75); armR.userData.elbow.rotation.set(0, 0, -0.15);
        armL.rotation.set(0.35, 0, -1.05); armL.userData.elbow.rotation.set(0, 0, 0.75);
        legL.rotation.set(0.45 + Math.sin(t * 2.4) * 0.25, 0, -0.18);
        legR.rotation.set(0.25 - Math.sin(t * 2.4) * 0.25, 0, 0.18);
      } else if (animation === 'crawl') {
        const pathT = t * 1.15;
        const vx = Math.cos(pathT) * 1.15;
        const vy = Math.cos(pathT * 2) * 1.10;
        mascot.position.x += Math.sin(pathT) * 1.15;
        mascot.position.y += Math.sin(pathT * 2) * 0.55;
        mascot.position.z += 0.4;
        const step = Math.sin(t * 7.5);
        bodyPivot.rotation.set(-0.08, 0, (Math.atan2(vy, vx) - Math.PI / 2) + step * 0.06);
        headGroup.rotation.set(0.10, 0, -step * 0.05);
        armL.rotation.set(0.25, 0, -1.75 + step * 0.48); armL.userData.elbow.rotation.set(0, 0, 0.85 - step * 0.25);
        armR.rotation.set(0.25, 0, 1.75 + step * 0.48); armR.userData.elbow.rotation.set(0, 0, -0.85 - step * 0.25);
        legL.rotation.set(0.20, 0, -0.58 - step * 0.40);
        legR.rotation.set(0.20, 0, 0.58 - step * 0.40);
      } else if (animation === 'hang') {
        const sway = Math.sin(t * 2.0) * 0.10;
        mascot.position.x += Math.sin(sway) * 0.40;
        mascot.position.y += (1 - Math.cos(sway)) * 0.16;
        bodyPivot.rotation.z = Math.PI + sway;
        bodyPivot.rotation.y = Math.sin(t * 1.1) * 0.20 + lookX * 0.25;
        headGroup.rotation.x = 0.18 - lookY * 0.45;
        headGroup.rotation.y = -lookX * 0.55;
        legL.rotation.set(-1.20, 0, -0.50); legL.userData.knee.rotation.set(1.50, 0, 0.36);
        legR.rotation.set(-1.20, 0, 0.50); legR.userData.knee.rotation.set(1.50, 0, -0.36);
        armL.rotation.set(-1.15, 0, 0.65); armL.userData.elbow.rotation.set(0.25, 0, 0.25);
        armR.rotation.set(-1.15, 0, -0.65); armR.userData.elbow.rotation.set(0.25, 0, -0.25);
      } else if (animation === 'thwip') {
        const recoil = Math.max(0, Math.sin(t * 5.0)) * 0.12;
        mascot.position.y -= 0.12;
        bodyPivot.rotation.set(-0.22, -0.35, 0.06);
        headGroup.rotation.set(0.18, 0.32 + lookX * 0.4, 0);
        legL.rotation.set(-0.65, 0, -0.38); legL.userData.knee.rotation.set(0.85, 0, 0);
        legR.rotation.set(0.35, 0, 0.32); legR.userData.knee.rotation.set(0.45, 0, 0);
        armR.rotation.set(-1.45 + recoil, 0, 0.15); armR.userData.elbow.rotation.set(0, 0, 0);
        armL.rotation.set(0.55, 0, -0.75); armL.userData.elbow.rotation.set(-0.65, 0, 0.35);
      } else if (animation === 'backflip') {
        const cycle = (t * 0.75) % 1;
        if (cycle < 0.65) {
          const u = cycle / 0.65; const jump = Math.sin(u * Math.PI);
          mascot.position.y += jump * 1.35; bodyPivot.rotation.x = u * Math.PI * 2;
          legL.rotation.set(-0.95 * jump, 0, -0.15); legL.userData.knee.rotation.set(1.4 * jump, 0, 0);
          legR.rotation.set(-0.95 * jump, 0, 0.15); legR.userData.knee.rotation.set(1.4 * jump, 0, 0);
          armL.rotation.set(-0.85 * jump, 0, 0.45); armR.rotation.set(-0.85 * jump, 0, -0.45);
        } else {
          mascot.position.y -= 0.14; bodyPivot.rotation.x = -0.25; headGroup.rotation.x = 0.22;
          legL.rotation.set(-0.65, 0, -0.32); legL.userData.knee.rotation.set(0.95, 0, 0);
          legR.rotation.set(-0.65, 0, 0.32); legR.userData.knee.rotation.set(0.95, 0, 0);
        }
      } else if (animation === 'hero') {
        // Full animated drop-from-above -> Heavy Three-Point Landing -> Shockwave!
        const cyc = (t * 0.65) % 1;
        if (cyc < 0.32) {
          const d = 1 - cyc / 0.32;
          mascot.position.y += d * 1.45 - 0.16;
          bodyPivot.rotation.set(-0.18, 0.35, 0);
          legL.rotation.set(-0.85, 0, -0.22); legL.userData.knee.rotation.set(1.15, 0, 0);
          legR.rotation.set(0.45, 0, 0.25); legR.userData.knee.rotation.set(0.35, 0, 0);
          armR.rotation.set(-1.35, 0, -0.2); armL.rotation.set(0.75, 0, -0.95);
        } else {
          const p = (cyc - 0.32) / 0.68;
          const bounce = p < 0.18 ? Math.sin((p / 0.18) * Math.PI) * 0.06 : 0;
          mascot.position.y -= 0.16 + bounce;
          bodyPivot.rotation.set(-0.32, 0.35, -0.05);
          headGroup.rotation.set(0.32 + lookY * 0.35, -0.28 + lookX * 0.45, 0.06);
          legL.rotation.set(-0.95, 0, -0.25); legL.userData.knee.rotation.set(1.15, 0, 0);
          legR.rotation.set(0.55, 0, 0.28); legR.userData.knee.rotation.set(0.35, 0, 0);
          armR.rotation.set(-0.95, 0, -0.25); armR.userData.elbow.rotation.set(0.15, 0, 0);
          armL.rotation.set(0.75, 0, -0.95); armL.userData.elbow.rotation.set(-0.45, 0, 0.35);
          if (p < 0.65) {
            shockRing.visible = true;
            shockRing.rotation.x = Math.PI / 2;
            shockRing.position.set(0, -0.72, 0);
            shockRing.scale.setScalar(1 + (p / 0.65) * 3.2);
            shockRing.material.opacity = Math.max(0, 1 - p / 0.65);
          }
        }
      } else if (animation === 'cheer') {
        const jump = Math.abs(Math.sin(t * 7.0));
        mascot.position.y += jump * 0.35;
        bodyPivot.rotation.z = Math.sin(t * 7.0) * 0.10;
        headGroup.rotation.z = -Math.sin(t * 7.0) * 0.12;
        armL.rotation.set(-0.35, 0, -2.45 + Math.sin(t * 14) * 0.22); armL.userData.elbow.rotation.set(0, 0, 0.35);
        armR.rotation.set(-0.35, 0, 2.45 - Math.sin(t * 14) * 0.22); armR.userData.elbow.rotation.set(0, 0, -0.35);
        legL.rotation.set(-jump * 0.35, 0, -0.25); legL.userData.knee.rotation.set(jump * 0.45, 0, 0);
        legR.rotation.set(-jump * 0.35, 0, 0.25); legR.userData.knee.rotation.set(jump * 0.45, 0, 0);
      } else if (animation === 'salute') {
        bodyPivot.rotation.y = -0.15;
        headGroup.rotation.set(Math.sin(t * 2.4) * 0.06, 0.15 + lookX * 0.3, 0);
        armR.rotation.set(-0.85, 0, 2.15); armR.userData.elbow.rotation.set(0, 0, 1.35);
      } else if (animation === 'dance') {
        const beat = t * 6.5; const sway = Math.sin(beat);
        mascot.position.y += Math.abs(sway) * 0.14;
        bodyPivot.rotation.set(-0.06, sway * 0.45, sway * 0.14);
        headGroup.rotation.set(Math.sin(beat * 2) * 0.10, -sway * 0.2, -sway * 0.12);
        armL.rotation.set(-0.85 - Math.max(0, sway) * 0.75, 0, -0.45); armL.userData.elbow.rotation.set(-0.35, 0, 0.45);
        armR.rotation.set(-0.85 - Math.max(0, -sway) * 0.75, 0, 0.45); armR.userData.elbow.rotation.set(-0.35, 0, -0.45);
        legL.rotation.set(-Math.max(0, -sway) * 0.35, 0, -0.18); legL.userData.knee.rotation.set(Math.max(0, -sway) * 0.45, 0, 0);
        legR.rotation.set(-Math.max(0, sway) * 0.35, 0, 0.18); legR.userData.knee.rotation.set(Math.max(0, sway) * 0.45, 0, 0);
      } else if (animation === 'sneak') {
        const patrol = t * 1.1; const dir = Math.cos(patrol) >= 0 ? 1 : -1; const step = Math.sin(t * 5.2);
        mascot.position.x += Math.sin(patrol) * 1.15; mascot.position.y += Math.abs(step) * 0.06 - 0.08;
        bodyPivot.rotation.set(-0.26, dir * 0.75, step * 0.06);
        headGroup.rotation.set(0.24, -dir * 0.35 + Math.sin(t * 2.2) * 0.25, 0);
        legL.rotation.set(-step * 0.55 - 0.25, 0, -0.14); legL.userData.knee.rotation.set(0.65, 0, 0);
        legR.rotation.set(step * 0.55 - 0.25, 0, 0.14); legR.userData.knee.rotation.set(0.65, 0, 0);
        armL.rotation.set(-0.55 + step * 0.3, 0, -0.45); armL.userData.elbow.rotation.set(-0.75, 0, 0.55);
        armR.rotation.set(-0.55 - step * 0.3, 0, 0.45); armR.userData.elbow.rotation.set(-0.75, 0, -0.55);
      } else if (animation === 'moonwalk') {
        const slide = t * 1.3; const dir = Math.cos(slide) >= 0 ? 1 : -1; const step = Math.sin(t * 6.5);
        mascot.position.x += Math.sin(slide) * 1.25; mascot.position.y += Math.abs(step) * 0.05;
        bodyPivot.rotation.set(0.08, -dir * 0.95, 0);
        headGroup.rotation.set(Math.sin(t * 6.5) * 0.08, dir * 0.25, 0);
        legL.rotation.set(-step * 0.52, 0, -0.06); legL.userData.knee.rotation.set(Math.max(0, -step * 0.55), 0, 0);
        legR.rotation.set(step * 0.52, 0, 0.06); legR.userData.knee.rotation.set(Math.max(0, step * 0.55), 0, 0);
        armL.rotation.set(step * 0.35, 0, -0.35); armL.userData.elbow.rotation.set(-0.65, 0, 0.25);
        armR.rotation.set(-step * 0.35, 0, 0.35); armR.userData.elbow.rotation.set(-0.65, 0, -0.25);
      } else if (animation === 'sit') {
        mascot.position.y -= 0.12;
        bodyPivot.rotation.set(0.14, Math.sin(t * 1.1) * 0.15, 0);
        headGroup.rotation.set(-0.10 + lookY * 0.4, lookX * 0.6, Math.sin(t * 2) * 0.06);
        legL.rotation.set(-1.35 + Math.sin(t * 4.5) * 0.18, 0, -0.18); legL.userData.knee.rotation.set(0.35, 0, 0);
        legR.rotation.set(-1.35 - Math.sin(t * 4.5) * 0.18, 0, 0.18); legR.userData.knee.rotation.set(0.35, 0, 0);
        armL.rotation.set(0.35, 0, -0.48); armL.userData.elbow.rotation.set(-0.25, 0, 0.2);
        armR.rotation.set(0.35, 0, 0.48); armR.userData.elbow.rotation.set(-0.25, 0, -0.2);
      } else if (animation === 'peek') {
        const tilt = Math.sin(t * 1.8) * 0.24;
        mascot.position.z += 0.35; mascot.position.y += Math.sin(t * 3.0) * 0.06;
        bodyPivot.rotation.set(-0.18, Math.sin(t * 1.2) * 0.22, tilt * 0.45);
        headGroup.rotation.set(0.15 + lookY * 0.5, lookX * 0.75, tilt);
        armL.rotation.set(-0.45, 0, -0.35); armL.userData.elbow.rotation.set(-0.65, 0, 0.85);
        armR.rotation.set(-0.45, 0, 0.35); armR.userData.elbow.rotation.set(-0.65, 0, -0.85);
      } else if (animation === 'nap') {
        const breath = Math.sin(t * 1.6);
        mascot.position.y += breath * 0.05 - 0.10;
        bodyPivot.rotation.set(0.10, 0.12, Math.sin(t * 0.9) * 0.05);
        headGroup.rotation.set(-0.28 + breath * 0.06, 0, Math.sin(t * 0.9) * 0.08);
        armL.rotation.set(-0.25, 0, -0.25); armL.userData.elbow.rotation.set(-0.25, 0, 0.25);
        armR.rotation.set(-0.25, 0, 0.25); armR.userData.elbow.rotation.set(-0.25, 0, -0.25);
        legL.rotation.set(-1.15, 0, -0.22); legL.userData.knee.rotation.set(0.45, 0, 0);
        legR.rotation.set(-1.15, 0, 0.22); legR.userData.knee.rotation.set(0.45, 0, 0);
      } else if (animation === 'think') {
        const hum = Math.sin(t * 2.2);
        const tap = Math.max(0, Math.sin(t * 5.5));
        mascot.position.y += hum * 0.03;
        bodyPivot.rotation.set(0.04, -0.16 + hum * 0.05, 0.02);
        headGroup.rotation.set(0.10 + lookY * 0.35, 0.16 + lookX * 0.4, -0.10 + hum * 0.04);
        if (hero === 'hulk') {
          // Scratching his head, one eyebrow raised.
          armL.rotation.set(-1.25 - tap * 0.30, 0, -1.00);
          armL.userData.elbow.rotation.set(-0.75, 0, 0.55);
          armR.rotation.set(0.18, 0, 0.45);
          headGroup.rotation.z = -0.18;
        } else if (hero === 'batman' || hero === 'legobatman') {
          // Arms crossed, brooding it over.
          armL.rotation.set(-0.10, 0, 0.82); armL.userData.elbow.rotation.set(-0.55, 0, 1.85);
          armR.rotation.set(-0.10, 0, -0.82); armR.userData.elbow.rotation.set(-0.55, 0, -1.85);
          headGroup.rotation.x += 0.10;
        } else if (hero === 'ironman') {
          // Taps the arc reactor while the suit runs diagnostics.
          armR.rotation.set(-0.80, 0, 0.45); armR.userData.elbow.rotation.set(-0.55, 0, -1.25);
          armL.rotation.set(0.25, 0, -0.50);
        } else {
          // Spidey taps his temple.
          armR.rotation.set(-1.10 - tap * 0.22, 0, 0.18);
          armR.userData.elbow.rotation.set(-1.30, 0, -0.30);
          armL.rotation.set(0.30, 0, -0.55);
          headGroup.rotation.z = 0.10;
        }
        thinkGroup.visible = true;
        thinkGroup.position.y = 0.14 + Math.sin(t * 2.6) * 0.05;
        thinkGroup.rotation.y = t * 0.7;
        thinkGroup.children.forEach((child, i) => {
          child.scale.setScalar(0.85 + Math.sin(t * 4 + i * 1.3) * 0.16);
        });
      } else if (animation === 'search') {
        const sweep = Math.sin(t * 1.6);
        mascot.position.y += Math.sin(t * 3.0) * 0.03;
        bodyPivot.rotation.set(0.06, sweep * 0.32, 0);
        headGroup.rotation.set(0.10 + lookY * 0.3, sweep * 0.62 + lookX * 0.25, 0);
        if (hero === 'hulk') {
          // Leans down and sniffs for clues.
          bodyPivot.rotation.x = -0.22;
          headGroup.rotation.x = 0.38;
          armL.rotation.set(-0.55, 0, -0.42);
          armR.rotation.set(-0.90, 0, 0.42); armR.userData.elbow.rotation.set(-0.35, 0, -0.45);
        } else if (hero === 'ironman') {
          // Palm repulsor sweeps the room.
          armR.rotation.set(-1.35, 0, 0.30); armR.userData.elbow.rotation.set(0, 0, -0.15);
          armL.rotation.set(0.30, 0, -0.55);
        } else if (hero === 'batman' || hero === 'legobatman') {
          // Detective lens held up near the cowl.
          armR.rotation.set(-1.20, 0, 0.35); armR.userData.elbow.rotation.set(-1.15, 0, -0.55);
          armL.rotation.set(0.30, 0, -0.55);
        } else {
          // Spidey crouches and scans with his web lens.
          bodyPivot.rotation.x = -0.12;
          legL.rotation.set(-0.55, 0, -0.28); legL.userData.knee.rotation.set(0.70, 0, 0);
          legR.rotation.set(0.25, 0, 0.24);
          armR.rotation.set(-1.25, 0, 0.25); armR.userData.elbow.rotation.set(-0.55, 0, -0.35);
          armL.rotation.set(0.35, 0, -0.60);
        }
        searchGroup.visible = true;
        searchGroup.position.set(0.62 + sweep * 0.16, 0.04 + Math.sin(t * 2.4) * 0.03, 0.60);
        searchGroup.rotation.set(0.05, -0.30 + sweep * 0.35, -0.26);
        searchGroup.scale.setScalar((hero === 'hulk' ? 1.25 : 1) * (1 + Math.sin(t * 3.1) * 0.04));
        lensRing.rotation.y = t * 2.4;
        lensGlass.material.opacity = 0.14 + (Math.sin(t * 2.6) + 1) * 0.10;
        scanRing.visible = true;
        scanRing.rotation.z = t * 1.2;
        scanRing.scale.setScalar(1 + Math.sin(t * 2.0) * 0.10);
      } else if (animation === 'wave') {
        const wave = Math.sin(t * 6.5);
        mascot.position.y += Math.abs(Math.sin(t * 3.2)) * 0.04;
        bodyPivot.rotation.set(0.02, -0.12, wave * 0.04);
        headGroup.rotation.set(0.05 + lookY * 0.3, lookX * 0.4, -wave * 0.05);
        armR.rotation.set(-0.25, 0, 2.15 + wave * 0.28);
        armR.userData.elbow.rotation.set(0, 0, -0.55 + wave * 0.30);
        armL.rotation.set(0.15, 0, -0.45);
      } else if (animation === 'spin') {
        const u = (t * 0.8) % 1;
        bodyPivot.rotation.y = u * Math.PI * 2;
        mascot.position.y += Math.sin(u * Math.PI) * 0.10;
        armL.rotation.set(0, 0, -1.35); armR.rotation.set(0, 0, 1.35);
        armL.userData.elbow.rotation.set(0, 0, 0.25); armR.userData.elbow.rotation.set(0, 0, -0.25);
        headGroup.rotation.z = Math.sin(u * Math.PI * 2) * 0.08;
      } else if (animation === 'jump') {
        const u = (t * 1.1) % 1;
        const arc = Math.sin(u * Math.PI);
        mascot.position.y += arc * 0.95;
        bodyPivot.rotation.x = -arc * 0.12;
        headGroup.rotation.x = arc * 0.15;
        legL.rotation.set(-arc * 0.85, 0, -0.16); legL.userData.knee.rotation.set(arc * 1.2, 0, 0);
        legR.rotation.set(-arc * 0.85, 0, 0.16); legR.userData.knee.rotation.set(arc * 1.2, 0, 0);
        armL.rotation.set(-arc * 0.8, 0, -0.70); armR.rotation.set(-arc * 0.8, 0, 0.70);
      } else if (animation === 'stretch') {
        const st = (Math.sin(t * 1.8) + 1) / 2;
        mascot.position.y += st * 0.10;
        bodyPivot.rotation.x = -0.08 * st;
        headGroup.rotation.set(-0.15 * st + lookY * 0.25, lookX * 0.3, 0);
        armL.rotation.set(-0.15, 0, -0.50 - st * 2.0);
        armR.rotation.set(-0.15, 0, 0.50 + st * 2.0);
        armL.userData.elbow.rotation.set(0, 0, 1.40 - st * 1.10);
        armR.userData.elbow.rotation.set(0, 0, -1.40 + st * 1.10);
        legL.rotation.set(0.10 * st, 0, -0.18); legR.rotation.set(0.10 * st, 0, 0.18);
      } else if (animation === 'shrug') {
        const lift = (Math.sin(t * 1.5) + 1) / 2;
        mascot.position.y += lift * 0.03;
        bodyPivot.rotation.set(0.03, 0, 0);
        headGroup.rotation.set(0.08 + lookY * 0.25, lookX * 0.35, 0.16 - lift * 0.10);
        armL.rotation.set(-0.25, 0, -1.05 - lift * 0.15); armL.userData.elbow.rotation.set(0, 0, 1.70);
        armR.rotation.set(-0.25, 0, 1.05 + lift * 0.15); armR.userData.elbow.rotation.set(0, 0, -1.70);
      } else if (animation === 'point') {
        const jab = Math.max(0, Math.sin(t * 3.5));
        mascot.position.y += Math.abs(Math.sin(t * 1.8)) * 0.03;
        bodyPivot.rotation.set(-0.05, 0.18, 0);
        headGroup.rotation.set(0.05 + lookY * 0.3, -0.10 + lookX * 0.4, 0);
        armR.rotation.set(-1.50 - jab * 0.12, 0, 0.25); armR.userData.elbow.rotation.set(0, 0, -0.05);
        armL.rotation.set(0.25, 0, -0.55);
      } else if (animation === 'facepalm') {
        const shake = Math.sin(t * 7.5) * 0.06;
        mascot.position.y -= 0.05;
        bodyPivot.rotation.set(0.06, shake * 0.4, 0);
        headGroup.rotation.set(0.20 + Math.sin(t * 5) * 0.05, -shake, 0.12);
        armR.rotation.set(-1.45, 0, 0.35); armR.userData.elbow.rotation.set(-2.10, 0, -0.50);
        armL.rotation.set(0.20, 0, -0.50);
      } else if (animation === 'taunt') {
        const beckon = Math.sin(t * 5.5);
        mascot.position.y += Math.abs(Math.sin(t * 2.4)) * 0.03;
        bodyPivot.rotation.set(-0.04, -0.12, 0);
        headGroup.rotation.set(0.02 + lookY * 0.25, lookX * 0.3, -0.06);
        armR.rotation.set(-1.05, 0, 0.55); armR.userData.elbow.rotation.set(0, 0, -0.75 + beckon * 0.35);
        armL.rotation.set(0.35, 0, -0.60);
      } else if (animation === 'bow') {
        const u = (Math.sin(t * 1.4) + 1) / 2;
        mascot.position.y -= u * 0.12;
        bodyPivot.rotation.set(-0.75 * u, 0, 0);
        headGroup.rotation.set(0.35 * u, 0, 0);
        legL.rotation.set(-0.10, 0, -0.16); legR.rotation.set(-0.10, 0, 0.16);
        armL.rotation.set(0.55 + u * 0.80, 0, -0.35); armL.userData.elbow.rotation.set(-0.60, 0, 0.90);
        armR.rotation.set(0.55 + u * 0.80, 0, 0.35); armR.userData.elbow.rotation.set(-0.60, 0, -0.90);
      } else if (animation === 'celebrate') {
        const hop = Math.abs(Math.sin(t * 6.0));
        mascot.position.y += hop * 0.42;
        bodyPivot.rotation.set(-0.05, Math.sin(t * 3.0) * 0.30, Math.sin(t * 6.0) * 0.08);
        headGroup.rotation.set(-0.12, -Math.sin(t * 3.0) * 0.20, Math.sin(t * 6.0) * 0.10);
        armL.rotation.set(-0.40, 0, -2.55); armL.userData.elbow.rotation.set(0, 0, 0.25);
        armR.rotation.set(-0.40, 0, 2.55); armR.userData.elbow.rotation.set(0, 0, -0.25);
        legL.rotation.set(-hop * 0.35, 0, -0.20); legL.userData.knee.rotation.set(hop * 0.50, 0, 0);
        legR.rotation.set(-hop * 0.35, 0, 0.20); legR.userData.knee.rotation.set(hop * 0.50, 0, 0);
        // Floating bricks double as confetti.
        legoBricksGroup.visible = true;
        legoBricksGroup.children.forEach((b, idx) => {
          const ang = t * 3.2 + idx * (Math.PI / 2);
          b.position.set(Math.cos(ang) * (0.90 + idx * 0.10), 0.60 + Math.sin(t * 4 + idx) * 0.50, Math.sin(ang) * 0.70);
          b.rotation.set(t * 3, t * 4, 0);
        });
      }

      // Hulk animated roaring/chomping mouth movement
      if (hero === 'hulk' && expression !== 'zipped' && expression !== 'happy' && expression !== 'sad') {
        const isRoar = animation === 'rage' || animation === 'smash' || animation === 'thunderclap' || animation === 'cheer' || animation === 'leap' || animation === 'hero';
        hulkMouthGroup.visible = true;
        const openAmt = isRoar ? (0.85 + Math.abs(Math.sin(t * 11)) * 0.65) : (0.45 + Math.sin(t * 3.5) * 0.18);
        hulkMouthGroup.scale.set(1, openAmt, 1);
      }

      // Expression Accessories
      if (heartsGroup.visible) heartsGroup.position.y = Math.sin(t * 3.2) * 0.06;
      if (senseGroup.visible) senseGroup.scale.setScalar(1 + Math.sin(t * 14) * 0.12);
      if (angelGroup.visible) haloMesh.position.y = 0.82 + Math.sin(t * 3) * 0.04;

      // 3D Web / Grapple / Beam Strand
      if (animation === 'hang' || animation === 'swing' || (animation === 'thwip' && hero === 'spidey') || zipTimer > 0) {
        if (zipTimer > 0 && !paused) zipTimer = Math.max(0, zipTimer - dt);
        const topWorld = screenToWorld(targetScreenX, 0);
        let startPos;
        if (animation === 'hang' || animation === 'thwip') {
          mascot.updateMatrixWorld(true);
          armR.userData.hand.getWorldPosition(hangAnchorWorld);
          startPos = hangAnchorWorld.clone();
        } else if (animation === 'swing') {
          startPos = new THREE.Vector3(mascot.position.x + 0.18, mascot.position.y + 0.45, mascot.position.z);
        } else {
          startPos = new THREE.Vector3(mascot.position.x, mascot.position.y + 0.2, mascot.position.z);
        }
        const endPos = animation === 'thwip'
          ? new THREE.Vector3(startPos.x + lookX * 4.5, startPos.y - lookY * 3.5 + 0.3, startPos.z + 4.5)
          : new THREE.Vector3(topWorld.x, topWorld.y + 1.2, mascot.position.z);
        const dir = endPos.clone().sub(startPos);
        webGroup.position.copy(startPos);
        webGroup.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.normalize());
        webGroup.scale.set(1, Math.max(0.1, endPos.distanceTo(startPos)), 1);
        helixMesh.visible = hero === 'spidey' && animation === 'hang';
        webGroup.visible = true;
      } else {
        webGroup.visible = false;
      }

      captionEl.style.left = `${screenX}px`;
      captionEl.style.top = `${Math.max(12, screenY - 18)}px`;

      renderer.render(scene, camera);
    }


  animate();

  function destroy() {
    if (destroyed) return;
    destroyed = true;
    cancelAnimationFrame(raf);
    if (resizeObserver) resizeObserver.disconnect();
    if (view) view.removeEventListener('resize', resize);
    doc.removeEventListener('pointermove', onPointerMove);
    scene.traverse((obj) => {
      if (obj.geometry) obj.geometry.dispose();
      if (obj.material) {
        const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
        for (const mat of mats) mat.dispose();
      }
    });
    renderer.dispose();
    if (renderer.forceContextLoss) renderer.forceContextLoss();
    canvas.remove();
    captionEl.remove();
  }

  return {
    el: canvas,
    get state() { return { hero, expression, animation, caption }; },
    update(patch = {}) {
      const src = patch && typeof patch === 'object' ? patch : {};
      if (src.theme !== undefined && src.hero === undefined) setHero(hero3dForTheme(src.theme));
      if (src.hero !== undefined) setHero(src.hero);
      if (src.expression !== undefined) setExpression(src.expression);
      if (src.animation !== undefined) setAnimation(src.animation);
      if (src.caption !== undefined) setCaption(src.caption);
      return { hero, expression, animation, caption };
    },
    setHero,
    setExpression,
    setAnimation,
    setCaption,
    moveTo,
    setReducedMotion,
    setPaused,
    destroy,
  };
}
