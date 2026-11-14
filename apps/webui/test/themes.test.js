import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const app = fs.readFileSync(path.join(here, '../src/app.js'), 'utf8');
const css = fs.readFileSync(path.join(here, '../src/style.css'), 'utf8');

// Read the THEMES literal out of app.js (it is JSON-shaped, so JSON.parse works on the block).
function themes() {
  const start = app.indexOf('const THEMES = {') + 'const THEMES = '.length;
  let depth = 0, end = -1;
  for (let i = start; i < app.length; i++) {
    if (app[i] === '{') depth++;
    else if (app[i] === '}') { depth--; if (depth === 0) { end = i + 1; break; } }
  }
  // Only the spiddy entries are plain JSON; the others are not parsed here.
  const block = app.slice(start, end);
  const out = {};
  for (const id of ['spiddy', 'spiddy-night', 'spiddy-steel', 'spiddy-jade', 'spiddy-brick']) {
    const m = block.match(new RegExp('"' + id + '": (\\{[^}]*\\})'));
    assert.ok(m, 'theme ' + id + ' is defined');
    out[id] = JSON.parse(m[1]);
  }
  return out;
}

test('theme catalog keeps only the hero themes, QCode Pop, and opencode', () => {
  const start = app.indexOf('const THEMES = {');
  const end = app.indexOf('let slashMenuEl');
  const block = app.slice(start, end);
  const ids = [];
  const re = /^  "([a-z0-9-]+)": \{/gm;
  let m;
  while ((m = re.exec(block))) ids.push(m[1]);
  assert.deepEqual(ids, [
    'qcode', 'qcode-light', 'opencode',
    'spiddy', 'spiddy-night', 'spiddy-steel', 'spiddy-jade', 'spiddy-brick',
  ]);
  for (const gone of ['tokyonight', 'dracula', 'monokai', 'nord', 'gruvbox', 'catppuccin', 'solarized', 'retro']) {
    assert.ok(!block.includes('"' + gone + '":'), gone + ' must be removed from the theme catalog');
  }
});

test('each hero has its own spiddy theme with its own accent and mascot colours', () => {
  const t = themes();
  const ids = Object.keys(t);
  assert.equal(ids.length, 5);
  const accents = new Set(ids.map(id => t[id].accent));
  assert.equal(accents.size, 5, 'accents are distinct');
  for (const id of ids) {
    assert.equal(t[id].hero, 'spiddy', id + ' drives the hero');
    assert.match(t[id].heroSuit, /^#[0-9a-f]{6}$/i, id + ' suit colour');
    assert.match(t[id].heroChest, /^#[0-9a-f]{6}$/i, id + ' chest colour');
  }
});

test('each hero theme is titled after its character, one per hero', () => {
  const t = themes();
  const want = {
    'spiddy': 'Spider-Man', 'spiddy-night': 'Batman', 'spiddy-steel': 'Iron Man',
    'spiddy-jade': 'Hulk', 'spiddy-brick': 'Brick Batman',
  };
  for (const id of Object.keys(want)) assert.equal(t[id].title, want[id], id);
  assert.equal(new Set(Object.values(t).map(x => x.title)).size, 5, 'titles are unique');
});

test('hero CSS reads the theme colours, with the original defaults kept', () => {
  assert.ok(css.includes('var(--hero-suit, #336687)'));
  assert.ok(css.includes('var(--hero-chest, #ff6d36)'));
  assert.ok(!/background: #336687;/.test(css), 'no hard-coded suit left');
});

test('roaming Spiddy is wired: layer, sync, and the in-message stage yields to it', () => {
  assert.ok(app.includes("import { roamModeFor, roamStateFor, planRoam, pickRoamAct, actPoolFor, ROAM_SIZE } from './roam.js';"));
  assert.ok(app.includes('function roamSync()'));
  assert.ok(app.includes('!roamActive() && (liveRunningTools'));
  assert.ok(fs.readFileSync(path.join(here, '../src/index.html'), 'utf8').includes('id="spiddy-roam"'));
  assert.ok(css.includes('.spiddy-roam-layer'));
});

// Every name app.js imports from a local module must be exported by it. A missing export
// fails at module load in the browser, so unit tests on the module alone would not catch it.
test('app.js imports only names that its local modules export', async () => {
  const modules = ['roam.js', 'mascot.js', 'hero.js', 'personality.js', 'emotions.js', 'sentiment-llm.js', 'mirage.js'];
  const re = /import\s*\{([^}]+)\}\s*from\s*'\.\/([\w.-]+)';/g;
  let m; let checked = 0;
  while ((m = re.exec(app))) {
    const file = m[2];
    if (!modules.includes(file)) continue;
    const mod = await import(path.join(here, '../src', file));
    for (const raw of m[1].split(',')) {
      const name = raw.trim().split(/\s+as\s+/)[0];
      if (!name) continue;
      assert.ok(name in mod, 'app.js imports ' + name + ' from ' + file + ' but it is not exported');
      checked++;
    }
  }
  assert.ok(checked > 10, 'checked ' + checked + ' imports');
});
