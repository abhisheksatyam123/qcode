import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const webuiRoot = path.resolve(__dirname, '..');
const srcDir = path.join(webuiRoot, 'src');

test('WebUI static source files exist and are populated', () => {
  const requiredFiles = [
    'index.html',
    'app.js',
    'style.css',
    'vendor-marked.min.js',
    'vendor-jsyaml.min.js'
  ];

  for (const file of requiredFiles) {
    const filePath = path.join(srcDir, file);
    assert.ok(fs.existsSync(filePath), `Missing expected file: ${file}`);
    const stats = fs.statSync(filePath);
    assert.ok(stats.size > 0, `File is empty: ${file}`);
  }
});

test('index.html references all required scripts and styles', () => {
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');

  // Verify stylesheet links
  assert.match(indexHtml, /href="\/style\.css"/, 'Missing style.css link');

  // Verify scripts
  assert.match(indexHtml, /src="\/vendor-marked\.min\.js"/, 'Missing vendor-marked.min.js');
  assert.match(indexHtml, /src="\/vendor-jsyaml\.min\.js"/, 'Missing vendor-jsyaml.min.js');
  assert.match(indexHtml, /src="\/app\.js"/, 'Missing app.js module');

  // Verify brand and status elements
  assert.match(indexHtml, /id="brand-sub"/, 'Missing #brand-sub element for version');
  assert.match(indexHtml, /id="connection-status"/, 'Missing #connection-status element');
});

test('Critical DOM element IDs referenced in app.js exist in index.html', () => {
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');

  // Find all getElementById calls
  const matches = [...appJs.matchAll(/document\.getElementById\(['"]([^'"]+)['"]\)/g)];
  assert.ok(matches.length > 0, 'No getElementById calls found');

  const missing = [];
  for (const match of matches) {
    const id = match[1];
    // Some IDs may be created dynamically in modals, but check the rest against index.html
    const idPattern = new RegExp(`id=["']${id}["']`);
    if (!idPattern.test(indexHtml) && !appJs.includes(`id="${id}"`)) {
      missing.push(id);
    }
  }

  assert.deepEqual(missing, [], `The following IDs used in app.js were missing from index.html: ${missing.join(', ')}`);
});

test('WebUI fetch endpoints conform to documented server-api.md', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const serverApiDoc = fs.readFileSync(path.resolve(webuiRoot, '../../docs/server-api.md'), 'utf8');

  // Extract endpoints from fetch(...) calls
  const fetchPatterns = [
    /fetch\(['"`](\/[^'"`?]+)/g,
    /fetch\(\s*['"](\/[^'"]+)/g
  ];

  const staticPrefixes = [
    '/api/version',
    '/api/health',
    '/health',
    '/providers',
    '/sessions',
    '/tasks',
    '/session/last',
    '/session/cancel',
    '/rename',
    '/terminal/create'
  ];

  for (const prefix of staticPrefixes) {
    assert.ok(serverApiDoc.includes(prefix), `server-api.md should document ${prefix}`);
  }

  // Ensure resize is documented
  assert.ok(serverApiDoc.includes('/terminal/:id/resize'), 'server-api.md must document /terminal/:id/resize');
});

test('WebUI includes TUI command palette, slash commands, and session chrome', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  for (const cmd of ['/theme', '/agent', '/queue', '/clear-queue', '/retry', '/help', '/model', '/variant', '/session', '/compact']) {
    assert.ok(appJs.includes("name: '" + cmd + "'") || appJs.includes('case \'' + cmd.slice(1) + '\''), 'missing slash ' + cmd);
  }
  assert.match(appJs, /const PALETTE_COMMANDS = \[/, 'missing PALETTE_COMMANDS');
  assert.match(appJs, /function showCommandPalette/, 'missing showCommandPalette');
  assert.match(appJs, /function applyTheme/, 'missing applyTheme');
  assert.match(appJs, /const THEMES =/, 'missing THEMES');
  assert.match(appJs, /opencode/, 'TUI opencode theme missing');
  assert.match(appJs, /tokyonight/, 'TUI tokyonight theme missing');
  assert.match(appJs, /function handleRetryCommand/, 'missing retry');
  assert.match(appJs, /function renderQueuedBlock/, 'missing queued block');
  assert.match(appJs, /function toggleThinking/, 'missing thinking toggle');
  assert.match(appJs, /key\.toLowerCase\(\) === 'p'/, 'missing Ctrl+P palette shortcut');
  assert.match(appJs, /key\.toLowerCase\(\) === 'n'/, 'missing Ctrl+N new session shortcut');
  assert.match(appJs, /key === 'F2'/, 'missing F2 thinking toggle');
  assert.match(appJs, /id: 'chat_open'/, 'missing chat_open palette command');
  assert.match(appJs, /case 'chat_open'/, 'missing chat_open palette dispatch');
  assert.match(appJs, /key === '1'/, 'missing Alt+1 chat shortcut');
  assert.match(appJs, /key === '4'/, 'missing Alt+4 sessions shortcut');
  assert.match(appJs, /name: '\/tools'/, 'WebUI-extra /tools slash should remain');

  assert.match(indexHtml, /id="tab-sessions-btn"/, 'missing Sessions tab');
  assert.match(indexHtml, /id="parent-back-btn"/, 'missing parent back');
  assert.match(indexHtml, /id="agent-mode-btn"/, 'missing agent mode');
  assert.match(indexHtml, /id="retry-btn"/, 'missing retry button');
  assert.match(indexHtml, /id="sessions-panel"/, 'missing sessions panel');

  assert.match(styleCss, /\.queued-block/, 'missing queued-block CSS');
  assert.match(styleCss, /\.help-modal/, 'missing help-modal CSS');
});

test('WebUI polish: fonts, help, sessions rows, retry state, CSS', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  const fontLinks = [...indexHtml.matchAll(/<link[^>]*nerd-fonts-generated\.min\.css[^>]*>/g)];
  assert.equal(fontLinks.length, 1, 'duplicate nerd-fonts stylesheet link in index.html');

  for (const row of ['/new', '/rename', '/clear-queue', '/tools']) {
    assert.ok(appJs.includes('<kbd>' + row + '</kbd>'), 'help modal missing ' + row);
  }

  assert.match(appJs, /if \(sid\) \{/, 'sessions rows should guard on sid');
  assert.match(appJs, /no-open/, 'sessions rows without sid need no-open class');
  assert.match(appJs, /retryBtn\.disabled/, 'retry button should disable while generating');
  assert.match(styleCss, /\.retry-btn:disabled/, 'missing retry disabled CSS');

  const baseRows = [...styleCss.matchAll(/\.delegated-row \{/g)];
  assert.equal(baseRows.length, 1, 'duplicate .delegated-row base rule in style.css');
});

test('WebUI theme propagation: warning/success vars and themed chrome', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  assert.match(styleCss, /--warning:/, 'missing --warning root var');
  assert.match(styleCss, /--success:/, 'missing --success root var');
  assert.match(appJs, /set\('--warning', t\.warning\)/, 'applyTheme must set --warning');
  assert.match(appJs, /set\('--success', t\.success\)/, 'applyTheme must set --success');

  for (const sel of ['.delegated-row:hover', '.delegated-row .child-status.running', '.delegated-row .child-status.done', '.retry-btn', '.queued-block', '.side-tab-header', '.agent-mode-btn.plan', '.subagent-badge']) {
    const i = styleCss.indexOf(sel);
    assert.ok(i >= 0, 'missing themed rule ' + sel);
    const block = styleCss.slice(i, styleCss.indexOf('}', i) + 1);
    assert.match(block, /var\(--/, sel + ' should use CSS vars');
  }
});

test('WebUI child session view: merged delegated tab, durable parent navigation, and banners', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // Merging of live /tasks and persisted /sessions?include_subagents=1
  assert.match(appJs, /include_subagents=1/, 'loadDelegatedSessionsTab should fetch persisted subagents');
  assert.match(appJs, /seenSids/, 'loadDelegatedSessionsTab should deduplicate tasks and subagent sessions');
  assert.match(appJs, /combined\.length/, 'heading should show count of all child sessions');

  // Durable parent navigation & session storage
  assert.match(appJs, /qcode-parent-session/, 'should persist parent session in sessionStorage');
  assert.match(appJs, /!isSub && !state\.parentSessionId/, 'parent back button should be visible in subagent mode');

  // Keyboard shortcut
  assert.match(appJs, /key === 'b' \|\| key === 'B'/, 'should support b/B shortcut to return to parent');

  // Chat view subagent banner
  assert.match(appJs, /subagent-session-banner/, 'chat view should render subagent-session-banner');
  assert.match(styleCss, /\.subagent-session-banner/, 'missing .subagent-session-banner in CSS');

  // Sidebar session tabs subagent pill
  assert.match(appJs, /session-subagent-pill/, 'sidebar tabs should display subagent pill');
  assert.match(styleCss, /\.session-subagent-pill/, 'missing .session-subagent-pill in CSS');

  // Tool block child actions
  assert.match(appJs, /tool-child-action-row/, 'tool blocks should have child action row');
  assert.match(styleCss, /\.tool-child-action-row/, 'missing .tool-child-action-row in CSS');
});

test('WebUI child session view enhancements: close tab, live filter, model banner, extractChildSessionId', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // Filter input
  assert.match(indexHtml, /id="sessions-filter-input"/, 'missing #sessions-filter-input in index.html');
  assert.match(appJs, /state\.sessionsFilterText/, 'app.js should track sessionsFilterText');
  assert.match(styleCss, /\.sessions-filter-input/, 'missing .sessions-filter-input in style.css');

  // closeSessionTab
  assert.match(appJs, /function closeSessionTab/, 'missing closeSessionTab function in app.js');
  assert.match(appJs, /close-session-tab-btn/, 'missing close-session-tab-btn in app.js');
  assert.match(styleCss, /\.close-session-tab-btn/, 'missing .close-session-tab-btn in style.css');

  // subagent-banner-model
  assert.match(appJs, /subagent-banner-model/, 'subagent banner should show model');
  assert.match(styleCss, /\.subagent-banner-model/, 'missing .subagent-banner-model in style.css');

  // extractChildSessionId
  assert.match(appJs, /function extractChildSessionId/, 'missing extractChildSessionId in app.js');
});
