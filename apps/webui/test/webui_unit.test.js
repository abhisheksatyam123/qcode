import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

import { fuzzyScore, fuzzyFilter, shortPath, formatBytes, formatMs, detectFsLanguage, extractChildSessionId } from '../src/utils.js';

test('fuzzyScore: subsequence + ordering', () => {
  assert.equal(fuzzyScore('', 'anything'), 0);
  assert.equal(fuzzyScore('xyz', 'abc'), -1);
  assert.ok(fuzzyScore('chat', 'chat view') >= fuzzyScore('chat', 'xchat'));
  assert.ok(fuzzyScore('chat', 'chat') > 0);
  assert.ok(fuzzyScore('fb', 'foo bar') > fuzzyScore('fb', 'xfoob'));
});

test('fuzzyFilter: keeps matches, sorts best-first', () => {
  const items = [
    { label: 'Open Chat', category: 'View' },
    { label: 'Open Files', category: 'View' },
    { label: 'Rename Session', category: 'Session' },
  ];
  const r = fuzzyFilter('chat', items);
  assert.equal(r.length, 1);
  assert.equal(r[0].label, 'Open Chat');
  assert.equal(fuzzyFilter('', items).length, 3);
});

test('shortPath truncates long paths', () => {
  assert.equal(shortPath(''), '');
  assert.equal(shortPath('a/b'), 'a/b');
  const long = '/very/long/workspace/path/to/some/deep/dir';
  assert.ok(shortPath(long).length <= 30);
  assert.ok(shortPath(long).startsWith('…'));
});

test('formatBytes boundaries', () => {
  assert.equal(formatBytes(0), '0 B');
  assert.equal(formatBytes(1023), '1023 B');
  assert.equal(formatBytes(2048), '2.0 KB');
  assert.equal(formatBytes(3 * 1024 * 1024), '3.0 MB');
});

test('formatMs boundaries', () => {
  assert.equal(formatMs(500), '500 ms');
  assert.equal(formatMs(1500), '1.5 s');
  assert.match(formatMs(90000), /1m/);
});

test('detectFsLanguage extensions', () => {
  assert.equal(detectFsLanguage('a.js'), 'javascript');
  assert.equal(detectFsLanguage('b.py'), 'python');
  assert.equal(detectFsLanguage('c.md'), 'markdown');
  assert.equal(detectFsLanguage('d.cpp'), 'cpp');
  assert.equal(detectFsLanguage('CMakeLists.txt'), 'cmake');
  assert.equal(detectFsLanguage('noext'), '');
});

test('extractChildSessionId shapes', () => {
  assert.equal(extractChildSessionId(null), '');
  assert.equal(extractChildSessionId({ tool_name: 'bash' }), '');
  const tc = { tool_name: 'task', arguments: JSON.stringify({ sessionId: 'ses_abc123' }), result: '' };
  assert.equal(extractChildSessionId(tc), 'ses_abc123');
});

test('renderMarkdown: tables + task lists (marked fixture)', () => {
  const html = fs.readFileSync(path.join(__dirname, '..', 'src', 'app.js'), 'utf8');
  assert.match(html, /gfm:\s*true/, 'gfm must be on for tables/task-lists');
  assert.match(html, /getMarkedRenderer/, 'custom renderer present');
});

test('markdown fixtures: tables + task lists render', () => {
  const appjs = fs.readFileSync(path.join(__dirname, '..', 'src', 'app.js'), 'utf8');
  const i = appjs.indexOf('function renderMarkdown(');
  assert.ok(i >= 0);
  let depth = 0, j = appjs.indexOf('{', i);
  const start = j;
  for (; j < appjs.length; j++) {
    if (appjs[j] === '{') depth++;
    else if (appjs[j] === '}') { depth--; if (!depth) break; }
  }
  const fnSrc = appjs.slice(i, j + 1);
  // minimal marked stub capturing options
  let seenOpts = null;
  const marked = { parse: (md, opts) => { seenOpts = opts; return 'TABLE:' + md.slice(0, 20); } };
  const esc = (t) => t;
  const prepareMarkdownBody = (t) => ({ frontmatter: null, md: t });
  const getMarkedRenderer = () => ({});
  const renderFrontmatterBox = () => '';
  const render = new Function('marked', 'esc', 'prepareMarkdownBody', 'getMarkedRenderer', 'renderFrontmatterBox',
    fnSrc + '\nreturn renderMarkdown;')(marked, esc, prepareMarkdownBody, getMarkedRenderer, renderFrontmatterBox);
  const tableMd = '| a | b |\n|---|---|\n| 1 | 2 |';
  const taskMd = '- [ ] todo\n- [x] done';
  assert.ok(render(tableMd).startsWith('TABLE:'));
  assert.ok(render(taskMd).startsWith('TABLE:'));
  assert.equal(seenOpts.gfm, true, 'gfm must be on for tables/task-lists');
  assert.equal(seenOpts.breaks, true);
});

test('new UI classes have CSS', () => {
  const css = fs.readFileSync(path.join(__dirname, '..', 'src', 'style.css'), 'utf8');
  for (const c of ['.stopped-early', '.thinking-hint', '.msg-actions', '.msg-action-btn', '.copy-code-btn', '.jump-latest', '.diff-view', '.open-tabs', '.msg-ts', '.error-panel']) {
    assert.ok(css.includes(c), 'missing CSS ' + c);
  }
  assert.ok(appjsIncludes('data-retry="stats"'), 'stats panel needs Retry');
  assert.ok(appjsIncludes('data-retry="sessions"'), 'sessions panel needs Retry');
  assert.ok(appjsIncludes('data-retry="term"'), 'terminal panel needs Retry');
});

function appjsIncludes(t) {
  return fs.readFileSync(path.join(__dirname, '..', 'src', 'app.js'), 'utf8').includes(t);
}

test('a11y hooks present', () => {
  const html = fs.readFileSync(path.join(__dirname, '..', 'src', 'index.html'), 'utf8');
  assert.match(html, /aria-live="polite"/, 'messages needs aria-live');
  assert.match(html, /role="dialog"/, 'modal needs dialog role');
});
