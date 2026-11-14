import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import {
  normalizeMascotState, mascotMarkup, mascotStateFromEvents, attachMascot,
  MASCOT_CAPTION_MAX, MASCOT_BUBBLE_MAX,
} from '../src/mascot.js';
import { CATALOG, POSES, EMOTIONS, INTENSITIES, MOTIONS } from '../src/emotions.js';

// The rule classifier maps a failure line to 'worried' (emotions.js RULES).
const EXPECTED_FAILURE_EMOTION = 'worried';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../../..');

test('normalizeMascotState falls back on invalid input and never throws', () => {
  for (const bad of [undefined, null, 42, 'x', { pose: 'nope', emotion: 'bogus', intensity: 'huge', motion: 'spin' }]) {
    const s = normalizeMascotState(bad);
    assert.ok(POSES.includes(s.pose), 'pose ' + s.pose);
    assert.ok(EMOTIONS.includes(s.emotion), 'emotion ' + s.emotion);
    assert.ok(INTENSITIES.includes(s.intensity), 'intensity ' + s.intensity);
    assert.ok(MOTIONS.includes(s.motion), 'motion ' + s.motion);
  }
  assert.equal(normalizeMascotState({ pose: 'nope' }).pose, 'think');
});

test('every pose/emotion/intensity/motion combination resolves to a catalog clip', () => {
  assert.equal(CATALOG.length, 864);
  const ids = new Set(CATALOG.map(c => [c.pose, c.emotion, c.intensity, c.motion].join('|')));
  assert.equal(ids.size, 864, 'catalog combinations are unique');
  for (const pose of POSES) for (const emotion of EMOTIONS) for (const intensity of INTENSITIES) for (const motion of MOTIONS) {
    const s = normalizeMascotState({ pose, emotion, intensity, motion });
    assert.ok(ids.has([s.pose, s.emotion, s.intensity, s.motion].join('|')), JSON.stringify(s));
  }
});

test('the same input always resolves to the same clip (stable motion pick)', () => {
  const a = normalizeMascotState({ pose: 'build', emotion: 'proud', text: 'alpha' });
  const b = normalizeMascotState({ pose: 'build', emotion: 'proud', text: 'alpha' });
  assert.deepEqual(a, b, 'same input gives the same clip');
});

test('captions are whitespace-collapsed and clamped; bubbles get their own limit', () => {
  const s = normalizeMascotState({ caption: '  hi\n\n there  ', bubble: 'x'.repeat(500) });
  assert.equal(s.caption, 'hi there');
  assert.equal(s.bubble.length, MASCOT_BUBBLE_MAX);
  assert.ok(s.bubble.endsWith('…'));
  const long = normalizeMascotState({ caption: 'y'.repeat(500) });
  assert.equal(long.caption.length, MASCOT_CAPTION_MAX);
  assert.equal(normalizeMascotState({ caption: '   ' }).caption, null);
});

test('mascotMarkup renders the hero structure and escapes user text', () => {
  const html = mascotMarkup({ pose: 'tools', caption: '<img src=x onerror=1>', bubble: '"a" & <b>' });
  assert.match(html, /class="hero hero-tools /);
  assert.match(html, /hero-emo-/);
  assert.match(html, /hero-motion-/);
  assert.ok(!html.includes('<img src=x'), 'caption is escaped');
  assert.ok(html.includes('&lt;img src=x onerror=1&gt;'));
  assert.ok(html.includes('&quot;a&quot; &amp; &lt;b&gt;'), 'bubble is escaped');
});

test('mascotStateFromEvents: running tool sets pose, failure looks worried', () => {
  const running = mascotStateFromEvents({ text: 'Reading.', toolEvents: [{ status: 'running', tool_name: 'grep' }] });
  assert.equal(running.pose, 'computer');
  const pinned = mascotStateFromEvents({ text: '', toolEvents: [], pose: 'build' });
  assert.equal(pinned.pose, 'build');
  const failed = mascotStateFromEvents({ text: 'Sorry, the build failed with an error.', toolEvents: [] });
  assert.equal(failed.emotion, EXPECTED_FAILURE_EMOTION);
});

// Minimal fake DOM: enough for attachMascot (createElement, appendChild, removeChild, dataset).
function fakeEl(tag, doc) {
  const el = {
    tagName: tag, className: '', dataset: {}, innerHTML: '', children: [], parentNode: null, ownerDocument: doc,
    appendChild(c) { c.parentNode = this; this.children.push(c); return c; },
    removeChild(c) { this.children = this.children.filter(x => x !== c); c.parentNode = null; return c; },
  };
  return el;
}
function fakeContainer() {
  const doc = { createElement: tag => fakeEl(tag, doc) };
  return fakeEl('section', doc);
}

test('attachMascot mounts, updates, says, and destroys', () => {
  const box = fakeContainer();
  const m = attachMascot(box, { state: { pose: 'phone', caption: 'Checking' } });
  assert.equal(box.children.length, 1);
  assert.equal(m.el.dataset.mascot, 'spiddy');
  assert.match(m.el.innerHTML, /hero-phone/);
  assert.match(m.el.innerHTML, /Checking/);

  m.say('Found it');
  assert.match(m.el.innerHTML, /Found it/);
  assert.equal(m.state.pose, 'phone', 'caption-only update keeps pose');

  m.update({ emotion: 'angry' });
  assert.equal(m.state.emotion, 'angry');
  assert.ok(MOTIONS.includes(m.state.motion));
  assert.match(m.el.innerHTML, /hero-emo-angry/);

  m.destroy();
  assert.equal(box.children.length, 0);
  m.destroy();
  assert.equal(m.update({ caption: 'after' }).caption, 'Found it', 'update after destroy is a no-op');
});

test('attaching twice to one container replaces the first mount', () => {
  const box = fakeContainer();
  attachMascot(box, { state: { pose: 'intro' } });
  attachMascot(box, { state: { pose: 'build' } });
  assert.equal(box.children.length, 1);
  assert.match(box.children[0].innerHTML, /hero-build/);
});

test('attachMascot rejects a non-element container', () => {
  assert.throws(() => attachMascot(null), TypeError);
  assert.throws(() => attachMascot({}), TypeError);
});

test('docs/mascot.md and the fetch script pin the same sha256 values', () => {
  const doc = fs.readFileSync(path.join(repo, 'docs/mascot.md'), 'utf8');
  const script = fs.readFileSync(path.join(repo, 'scripts/fetch_mirage_wasm.sh'), 'utf8');
  const hashes = [
    '707341e44776ab0eb8dfc8f6a579ed5ac41e2ffc6419a18012f61619a6dfc70c',
    '6fb6cb41145cd51fd8af97644cd684fbdd434d4d3ce261881df58cbaef5b67f7',
    '4249a0f7515272ff76b9414bec17d7e7daf84b15b66c7fb4f17fce1e66f9f237',
  ];
  for (const h of hashes) {
    assert.ok(doc.includes(h), 'doc has ' + h);
    assert.ok(script.includes(h), 'script has ' + h);
  }
});

test('fetch script: syntax ok, --check fails on an empty directory without creating it', () => {
  const script = path.join(repo, 'scripts/fetch_mirage_wasm.sh');
  const syntax = spawnSync('bash', ['-n', script], { encoding: 'utf8' });
  assert.equal(syntax.status, 0, syntax.stderr);
  const dir = path.join(fs.mkdtempSync(path.join(process.env.TMPDIR || '/tmp', 'mw-')), 'absent');
  const run = spawnSync('bash', [script, '--check', dir], { encoding: 'utf8' });
  assert.equal(run.status, 1);
  assert.match(run.stdout, /MISSING\s+cactus\.js/);
  assert.equal(fs.existsSync(dir), false, '--check must not create the directory');
});

test('acts: valid names emit a hero-act class, unknown names emit none', () => {
  assert.equal(normalizeMascotState({ act: 'swing' }).act, 'swing');
  assert.equal(normalizeMascotState({ act: 'hang' }).act, 'hang', 'hang is an act (arms out on the thread)');
  assert.equal(normalizeMascotState({ act: 'bogus' }).act, null);
  assert.match(mascotMarkup({ pose: 'tools', act: 'run' }), /class="hero hero-tools [^"]*hero-act-run"/);
  assert.doesNotMatch(mascotMarkup({ pose: 'tools', act: 'nope' }), /hero-act-/);
  assert.doesNotMatch(mascotMarkup({ pose: 'tools' }), /hero-act-/);
});

test('hero CSS defines every act listed in HERO_ACTS', async () => {
  const { HERO_ACTS } = await import('../src/hero.js');
  const css = fs.readFileSync(path.join(here, '../src/style.css'), 'utf8');
  for (const name of HERO_ACTS) {
    assert.ok(css.includes('.hero-act-' + name + ' '), 'css rule for ' + name);
  }
});
