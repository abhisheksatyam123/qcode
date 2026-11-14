import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

import {
  HERO3D_HEROES,
  HERO3D_EXPRESSIONS,
  HERO3D_MOVES,
  hero3dForTheme,
  normalizeHero3DState,
  expressionForEmotion,
  hero3dAvailable,
  HERO3D_MODE_POOLS,
  HERO3D_EXPRESSION_CYCLE,
  HERO3D_VOICE,
  hero3dPoolFor,
  hero3dAnimationFor,
  hero3dHasSearchTool,
  hero3dExpressionFor,
  hero3dLineFor,
  hero3dQuipFor,
} from '../src/hero3d.js';

test('hero3d: each hero theme maps to its 3D model', () => {
  assert.equal(hero3dForTheme('spiddy'), 'spidey');
  assert.equal(hero3dForTheme('spiddy-night'), 'batman');
  assert.equal(hero3dForTheme('spiddy-steel'), 'ironman');
  assert.equal(hero3dForTheme('spiddy-jade'), 'hulk');
  assert.equal(hero3dForTheme('spiddy-brick'), 'legobatman');
  assert.equal(hero3dForTheme('qcode'), 'spidey');
  assert.equal(hero3dForTheme(undefined), 'spidey');
});

test('hero3d: every hero has the demo move set and all five models are exposed', () => {
  assert.deepEqual(HERO3D_HEROES, ['spidey', 'batman', 'ironman', 'hulk', 'legobatman']);
  for (const hero of HERO3D_HEROES) {
    assert.ok(HERO3D_MOVES[hero].includes('idle'), hero + ' has an idle');
    assert.ok(HERO3D_MOVES[hero].includes('hero'), hero + ' has a landing');
  }
  assert.ok(HERO3D_MOVES.batman.includes('batglide'));
  assert.ok(HERO3D_MOVES.ironman.includes('unibeam'));
  assert.ok(HERO3D_MOVES.hulk.includes('thunderclap'));
  assert.ok(HERO3D_MOVES.legobatman.includes('masterbuild'));
});

test('hero3d: state normalization falls back, aliases glide, and clamps captions', () => {
  assert.deepEqual(normalizeHero3DState({}), { hero: 'spidey', expression: 'neutral', animation: 'idle', caption: '' });
  assert.equal(normalizeHero3DState({ hero: 'nope' }).hero, 'spidey');
  assert.equal(normalizeHero3DState({ theme: 'spiddy-jade' }).hero, 'hulk');
  assert.equal(normalizeHero3DState({ expression: 'nope' }).expression, 'neutral');
  assert.equal(normalizeHero3DState({ animation: 'glide' }).animation, 'batglide');
  assert.equal(normalizeHero3DState({ animation: 'nope' }).animation, 'idle');
  const long = normalizeHero3DState({ caption: '  one   two  ' + 'x'.repeat(300) });
  assert.ok(long.caption.startsWith('one two'));
  assert.ok(long.caption.length <= 140);
  assert.equal(normalizeHero3DState({ caption: '   ' }).caption, '');
});

test('hero3d: chat emotions map onto the nine 3D expressions', () => {
  assert.equal(expressionForEmotion('happy'), 'happy');
  assert.equal(expressionForEmotion('proud'), 'happy');
  assert.equal(expressionForEmotion('worried'), 'sad');
  assert.equal(expressionForEmotion('angry'), 'angry');
  assert.equal(expressionForEmotion('surprised'), 'spideysense');
  assert.equal(expressionForEmotion('sleepy'), 'zipped');
  assert.equal(expressionForEmotion('focused'), 'neutral');
  assert.equal(expressionForEmotion('unknown'), 'neutral');
  for (const emotion of ['happy', 'sad', 'angry', 'surprised', 'sleepy', 'focused']) {
    assert.ok(HERO3D_EXPRESSIONS.includes(expressionForEmotion(emotion)));
  }
});

test('hero3d: each hero has 20-30 moves, including think and search', () => {
  for (const hero of HERO3D_HEROES) {
    const moves = HERO3D_MOVES[hero];
    assert.ok(moves.length >= 20 && moves.length <= 30, hero + ' has 20-30 moves (' + moves.length + ')');
    assert.equal(new Set(moves).size, moves.length, hero + ' move ids are unique');
    for (const move of ['think', 'search', 'wave', 'celebrate', 'facepalm']) {
      assert.ok(moves.includes(move), hero + ' has ' + move);
    }
  }
});

test('hero3d: each hero has its own mode pools made of real moves', () => {
  for (const hero of HERO3D_HEROES) {
    assert.deepEqual(Object.keys(HERO3D_MODE_POOLS[hero]).sort(), ['error', 'search', 'success', 'think', 'tools']);
    for (const mode of ['think', 'search', 'tools', 'success', 'error']) {
      const pool = hero3dPoolFor(mode, hero);
      assert.ok(pool.length > 0, hero + '/' + mode + ' has acts');
      for (const act of pool) assert.ok(HERO3D_MOVES[hero].includes(act), hero + '/' + mode + ': ' + act + ' is a real move');
    }
    assert.deepEqual(hero3dPoolFor('idle', hero).sort(), HERO3D_MOVES[hero].slice().sort(), hero + ' idle uses the full move list');
  }
  // Pools are hero-specific, not one shared list.
  const signatures = new Set(HERO3D_HEROES.map(h => JSON.stringify(HERO3D_MODE_POOLS[h])));
  assert.equal(signatures.size, HERO3D_HEROES.length);
});

test('hero3d: thinking and file/web searching pick their own animations', () => {
  assert.equal(hero3dAnimationFor({ mode: 'think', theme: 'spiddy-night' }), 'think');
  assert.equal(hero3dAnimationFor({ mode: 'think', theme: 'spiddy-jade', rand: () => 0.9 }), 'think');
  assert.equal(hero3dHasSearchTool([{ status: 'running', tool_name: 'grep' }]), true);
  assert.equal(hero3dHasSearchTool([{ status: 'running', tool_name: 'web_search' }]), true);
  assert.equal(hero3dHasSearchTool([{ status: 'running', tool_name: 'bash' }]), false);
  assert.equal(hero3dHasSearchTool([{ status: 'success', tool_name: 'grep' }]), false);
  for (const theme of ['spiddy', 'spiddy-night', 'spiddy-steel', 'spiddy-jade', 'spiddy-brick']) {
    assert.equal(hero3dAnimationFor({ mode: 'tools', theme, toolEvents: [{ status: 'running', tool_name: 'read' }] }), 'search');
  }
  // Non-search tools use the hero's tool pool.
  const toolActs = hero3dPoolFor('tools', 'spiddy-brick');
  for (let i = 0; i < 50; i++) {
    assert.ok(toolActs.includes(hero3dAnimationFor({ mode: 'tools', theme: 'spiddy-brick', toolEvents: [{ status: 'running', tool_name: 'bash' }] })));
  }
});

test('hero3d: success and error modes pick celebratory / facepalm acts', () => {
  for (const theme of ['spiddy', 'spiddy-night', 'spiddy-steel', 'spiddy-jade', 'spiddy-brick']) {
    const hero = hero3dForTheme(theme);
    assert.ok(hero3dPoolFor('error', theme).includes('facepalm'), hero + ' facepalms on errors');
    const success = hero3dPoolFor('success', theme);
    assert.ok(success.some(act => ['celebrate', 'cheer', 'hero', 'salute', 'bow'].includes(act)), hero + ' celebrates success');
    for (let i = 0; i < 30; i++) {
      const act = hero3dAnimationFor({ mode: 'success', theme, prev: null, rand: Math.random });
      assert.ok(success.includes(act));
    }
  }
});

test('hero3d: idle cycles through all nine faces; busy modes use mood subsets', () => {
  const seen = new Set();
  for (let i = 0; i < HERO3D_EXPRESSION_CYCLE.length; i++) {
    seen.add(hero3dExpressionFor({ mode: 'idle', index: i }));
  }
  assert.deepEqual([...seen].sort(), HERO3D_EXPRESSION_CYCLE.slice().sort(), 'idle eventually shows every emotion');
  assert.equal(hero3dExpressionFor({ mode: 'idle', index: 9 }), hero3dExpressionFor({ mode: 'idle', index: 0 }));
  assert.equal(hero3dExpressionFor({ mode: 'success', index: 0 }), 'happy');
  assert.equal(hero3dExpressionFor({ mode: 'error', index: 0 }), 'sad');
  for (const mode of ['think', 'search', 'tools', 'success', 'error']) {
    for (let i = 0; i < 12; i++) {
      assert.ok(HERO3D_EXPRESSIONS.includes(hero3dExpressionFor({ mode, index: i })), mode + ' expression is valid');
    }
  }
  assert.ok(HERO3D_EXPRESSIONS.includes(hero3dExpressionFor({ mode: 'bogus', index: 3 })));
});

test('hero3d: each hero speaks only its own dialogue', () => {
  const forbidden = {
    spidey: /Gotham|JARVIS|HULK|brick/i,
    batman: /thwip|JARVIS|HULK|brick/i,
    ironman: /Gotham|thwip|HULK|brick/i,
    hulk: /Gotham|JARVIS|thwip|brick/i,
    legobatman: /Gotham|JARVIS|thwip|HULK/i,
  };
  for (const hero of HERO3D_HEROES) {
    for (const mode of ['idle', 'think', 'tools', 'search', 'success', 'error']) {
      const lines = HERO3D_VOICE[hero][mode];
      assert.ok(Array.isArray(lines) && lines.length >= 3, hero + '/' + mode + ' has its own lines');
      for (let i = 0; i < 20; i++) {
        const line = hero3dLineFor(hero, mode, mode + ':' + i);
        assert.ok(lines.includes(line), '"' + line + '" belongs to ' + hero);
        assert.doesNotMatch(line, forbidden[hero], '"' + line + '" leaks another hero');
      }
    }
  }
  assert.equal(hero3dLineFor('spiddy', 'idle', 'x'), hero3dLineFor('spidey', 'idle', 'x'));
  assert.ok(HERO3D_VOICE.spidey.idle.includes(hero3dLineFor('no-such-theme', 'idle', 'x')), 'unknown theme falls back to Spidey');
});

test('hero3d: greetings and classic quips use the current hero voice', () => {
  for (const hero of HERO3D_HEROES) {
    assert.ok(HERO3D_VOICE[hero].greeting.length >= 3, hero + ' has greetings');
    for (let i = 0; i < 12; i++) {
      assert.ok(HERO3D_VOICE[hero].greeting.includes(hero3dLineFor(hero, 'greeting', 'g:' + i)));
    }
    const cases = [
      [{ pose: 'intro' }, 'greeting'],
      [{ pose: 'think', emotion: 'focused' }, 'think'],
      [{ pose: 'computer', emotion: 'focused' }, 'search'],
      [{ pose: 'phone', emotion: 'focused' }, 'search'],
      [{ pose: 'build', emotion: 'focused' }, 'tools'],
      [{ pose: 'tools', emotion: 'focused' }, 'tools'],
      [{ pose: 'build', emotion: 'proud' }, 'success'],
      [{ pose: 'tools', emotion: 'worried' }, 'error'],
    ];
    for (const [clip, mode] of cases) {
      const line = hero3dQuipFor(hero, { ...clip, text: 'same text' });
      assert.ok(HERO3D_VOICE[hero][mode].includes(line), hero + ' ' + mode + ' quip came from another pool: ' + line);
      assert.equal(line, hero3dQuipFor(hero, { ...clip, text: 'same text' }), 'same clip is stable');
    }
  }
});

test('hero3d: WebGL is reported unavailable when three.js is not loaded (Node)', () => {
  assert.equal(hero3dAvailable(), false);
});

test('hero3d: WebUI wires the renderer, vendor script and settings toggle', () => {
  const root = path.join(__dirname, '..');
  const app = fs.readFileSync(path.join(root, 'src', 'app.js'), 'utf8');
  const html = fs.readFileSync(path.join(root, 'src', 'index.html'), 'utf8');
  const css = fs.readFileSync(path.join(root, 'src', 'style.css'), 'utf8');
  const hero3dSrc = fs.readFileSync(path.join(root, 'src', 'hero3d.js'), 'utf8');

  assert.match(app, /import \{ attachHero3D, hero3dForTheme, hero3dAnimationFor, hero3dPoolFor, hero3dExpressionFor, hero3dLineFor, hero3dQuipFor, hero3dHasSearchTool \} from '\.\/hero3d\.js'/);
  assert.match(app, /hero3dAnimationFor\(\{ mode, theme: state\.theme, toolEvents/);
  assert.match(app, /hero3dPoolFor\(mode, state\.theme\)/);
  assert.match(app, /attachHero3D\(layer/);
  assert.match(app, /roamCtlKind === '3d'/);
  assert.match(app, /HERO3D_ANCHOR = \{ right: \d+, lift: \d+ \}/);
  assert.match(app, /input\.getBoundingClientRect\(\)\.top - main\.getBoundingClientRect\(\)\.top/, '3D mascot anchors just above the composer');
  assert.match(app, /messages\.getBoundingClientRect\(\)\.right - mainLeft/, '3D mascot sits in the right gutter beside the message column');
  assert.match(hero3dSrc, /camera\.position\.set\(0, 0, 64\)/, '3D mascot renders at quarter of the demo size');
  assert.match(app, /roamCtl\.moveTo\(\{ left: anchor\.left/);
  assert.match(app, /mascotStyle === '3d'/);
  assert.match(html, /src="\/vendor-three\.min\.js"/);
  assert.match(html, /id="mascot-style-select"/);
  assert.match(css, /\.hero3d-canvas/);
  assert.match(css, /\.hero3d-caption/);
  assert.ok(fs.statSync(path.join(root, 'src', 'vendor-three.min.js')).size > 100000);
});
