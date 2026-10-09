import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

import { fuzzyScore, fuzzyFilter, shortPath, formatBytes, formatMs, formatNumber, formatUsd, usageCostSummary, usageModelRows, detectFsLanguage, extractChildSessionId, extractFrontmatter, stripFrontmatter, transformWikilinks, SLASH_COMMANDS, PALETTE_COMMANDS } from '../src/utils.js';

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

test('formatNumber formatting', () => {
  assert.equal(formatNumber(0), '0');
  assert.equal(formatNumber(1000), (1000).toLocaleString());
  assert.equal(formatNumber(1234567), (1234567).toLocaleString());
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
});

function appjsIncludes(t) {
  return fs.readFileSync(path.join(__dirname, '..', 'src', 'app.js'), 'utf8').includes(t);
}

test('a11y hooks present', () => {
  const html = fs.readFileSync(path.join(__dirname, '..', 'src', 'index.html'), 'utf8');
  assert.match(html, /aria-live="polite"/, 'messages needs aria-live');
  assert.match(html, /role="dialog"/, 'modal needs dialog role');
});

test('extractFrontmatter: fence parsing', () => {
  const r1 = extractFrontmatter('---\ntitle: hi\n---\nbody text');
  assert.equal(r1.frontmatter.trim(), 'title: hi');
  assert.equal(r1.body, 'body text');
  const r2 = extractFrontmatter('no fence here');
  assert.equal(r2.frontmatter, null);
  assert.equal(r2.body, 'no fence here');
  const r3 = extractFrontmatter('---\njust a rule\n---\nmore');
  assert.equal(r3.frontmatter, null, 'separator without keys is not frontmatter');
});

test('stripFrontmatter delegates', () => {
  const r = stripFrontmatter('---\na: 1\n---\nx');
  assert.equal(r.body, 'x');
});

test('transformWikilinks: links + code fences', () => {
  const out = transformWikilinks('see [[target|Alias]] now');
  assert.match(out, /Alias/, 'alias preserved');
  assert.ok(!out.includes('[['), 'wikilinks consumed');
  const fenced = transformWikilinks('```\n[[keep]]\n```');
  assert.match(fenced, /\[\[keep\]\]/, 'fenced wikilinks untouched');
});

test('command catalogs: data present', () => {
  assert.ok(SLASH_COMMANDS.length >= 8, 'slash commands present');
  assert.ok(SLASH_COMMANDS.some(c => c.name === '/model'));
  assert.ok(PALETTE_COMMANDS.length >= 10, 'palette commands present');
  assert.ok(PALETTE_COMMANDS.some(c => c.id === 'thinking_toggle'));
});

test('formatUsd precision', () => {
  assert.equal(formatUsd(0), '$0.0000');
  assert.equal(formatUsd(0.01234), '$0.0123');
  assert.equal(formatUsd(123.456), '$123.46');
  assert.equal(formatUsd(undefined), '$0.0000');
});

test('usageCostSummary: per-call total, notes for unpriced/legacy/estimated', () => {
  const priced = usageCostSummary({
    model_calls: 3,
    session_cost: { available: true, estimated: false, total: 0.5, input: 0.1, cache_read: 0.05,
      cache_write: 0.05, output: 0.3, unpriced_calls: 1, legacy_calls: 0 },
  });
  assert.equal(priced.available, true);
  assert.equal(priced.total, 0.5);
  assert.deepEqual(priced.parts.map((p) => p.value), [0.1, 0.05, 0.05, 0.3]);
  assert.equal(priced.notes.length, 1);
  assert.match(priced.notes[0], /1 call on models without a price is not included/);

  const legacy = usageCostSummary({
    model_calls: 2,
    session_cost: { available: true, estimated: true, total: 0.2, legacy_calls: 2 },
  });
  assert.equal(legacy.estimated, true);
  assert.match(legacy.notes[0], /Estimate at the current model's price/);
  assert.equal(legacy.notes.length, 1, 'estimated sessions do not repeat the legacy note');

  const unconfigured = usageCostSummary({ model_calls: 1, session_cost: { available: false } });
  assert.equal(unconfigured.available, false);
  assert.match(unconfigured.notes[0], /No price configured/);

  assert.equal(usageCostSummary(undefined).available, false);
});

test('usageModelRows: sorted by cost, unpriced kept', () => {
  const rows = usageModelRows({
    by_model: {
      'anthropic/claude-haiku-5-5': { calls: 4, input_tokens: 10, output_tokens: 5,
        cost: { priced: true, total: 0.001 } },
      'anthropic/claude-opus-5-5': { calls: 1, input_tokens: 100, output_tokens: 50,
        cost: { priced: true, total: 0.2 } },
      'antigravity/gemini-3.8-flash': { calls: 2, unpriced_calls: 2, cost: { priced: false, total: 0 } },
    },
  });
  assert.deepEqual(rows.map((r) => r.key),
    ['anthropic/claude-opus-5-5', 'anthropic/claude-haiku-5-5', 'antigravity/gemini-3.8-flash']);
  assert.equal(rows[2].priced, false);
  assert.equal(rows[2].unpricedCalls, 2);
  assert.deepEqual(usageModelRows({}), []);
});

test('renderUsageSections: cost, cache, latency and per-model rows', async () => {
  const { renderUsageSections } = await import('../src/utils.js');
  const html = renderUsageSections({
    usage: {
      model_calls: 2, input_tokens: 2000, cache_read_tokens: 1400, cache_write_tokens: 0,
      uncached_input_tokens: 600, output_tokens: 1000, reasoning_tokens: 300,
      cache_hit_pct: 70, avg_call_ms: 1000, avg_ttft_ms: 100, model_ms_last: 900,
      ttft_ms_last: 80, model_ms_max: 1100, model_ms_total: 2000, output_tok_per_s: 500,
      last_effort: 'max', last_variant: 'ultra',
      session_cost: { available: true, estimated: false, total: 0.0123, input: 0.002,
        cache_read: 0.0003, cache_write: 0, output: 0.01, unpriced_calls: 0, legacy_calls: 0 },
      by_model: { 'anthropic/claude-opus-5-5': { calls: 2, input_tokens: 2000, output_tokens: 1000,
        cost: { priced: true, total: 0.0123 } } },
    },
    model_info: { id: 'claude-opus-5-5', name: 'Claude <Opus>', context_window: 1000000,
      output_limit: 128000, max_tokens: 32000, cost: { input: 4, output: 20, cache_read: 0.2,
      cache_write: 5 }, thinking: { type: 'adaptive', display: 'summarized', allow_off: false },
      variants: ['low', 'ultra'] },
    context: { used: 1000, window: 1000000 },
  });
  assert.match(html, /\$0\.0123/);
  assert.match(html, /70% hit/);
  assert.match(html, /ultra → effort max/);
  assert.match(html, /anthropic\/claude-opus-5-5/);
  assert.match(html, /32,000|32000/);
  assert.match(html, /Claude &lt;Opus&gt;/, 'model names are escaped');
  assert.ok(!html.includes('Claude <Opus>'));

  const empty = renderUsageSections({ usage: { model_calls: 0 }, context: {} });
  assert.match(empty, /start with this session's next model call/);
  assert.match(empty, /window unknown: set limit.context/);
});

test('renderUsageSections: subagent cost line', async () => {
  const { renderUsageSections } = await import('../src/utils.js');
  const html = renderUsageSections({
    usage: { model_calls: 2, session_cost: { available: true, total: 0.5 } },
    subagents: { sessions: 2, model_calls: 5, session_cost: { available: true, total: 0.25 } },
  });
  assert.match(html, /\+ \$0\.2500 in 2 subagent sessions/);
  const none = renderUsageSections({ usage: { model_calls: 1 }, subagents: { sessions: 0 } });
  assert.doesNotMatch(none, /subagent session/);
});
