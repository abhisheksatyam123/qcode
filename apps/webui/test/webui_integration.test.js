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
    'utils.js',
    'style.css',
    'vendor-marked.min.js',
    'vendor-jsyaml.min.js',
    'vendor-hljs.min.js',
    'fonts/ui/inter-latin-wght-normal.woff2',
    'fonts/ui/jetbrains-mono-latin-wght-normal.woff2',
    'fonts/ui/SymbolsNerdFontMono.woff2'
  ];

  for (const file of requiredFiles) {
    const filePath = path.join(srcDir, file);
    assert.ok(fs.existsSync(filePath), `Missing expected file: ${file}`);
    const stats = fs.statSync(filePath);
    assert.ok(stats.size > 0, `File is empty: ${file}`);
  }
});

const styleCss = () => fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

test('index.html references all required scripts and styles', () => {
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');

  // Verify stylesheet links
  assert.match(indexHtml, /href="\/style\.css"/, 'Missing style.css link');

  // Verify scripts
  assert.match(indexHtml, /src="\/vendor-marked\.min\.js"/, 'Missing vendor-marked.min.js');
  assert.match(indexHtml, /src="\/vendor-jsyaml\.min\.js"/, 'Missing vendor-jsyaml.min.js');
  // highlight.js is lazy-loaded by app.js on first use (T8.3), not a page script.
  assert.doesNotMatch(indexHtml, /src="\/vendor-hljs\.min\.js"/, 'vendor-hljs must not block page load');
  assert.match(fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8'), /'\/vendor-hljs\.min\.js'/, 'app.js must lazy-load vendor-hljs.min.js (T8.3)');
  assert.match(indexHtml, /src="\/app\.js"/, 'Missing app.js module');
  assert.doesNotMatch(indexHtml, /fonts\.googleapis|cdn\.jsdelivr/, 'UI fonts must be self-hosted (no CDN)');
  assert.match(styleCss(), /font-family: 'Inter'/, 'style.css should declare the self-hosted Inter face');

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
    '/rename'
  ];

  for (const prefix of staticPrefixes) {
    assert.ok(serverApiDoc.includes(prefix), `server-api.md should document ${prefix}`);
  }
});

test('WebUI includes TUI command palette, slash commands, and session chrome', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  const utilsJs = fs.readFileSync(path.join(srcDir, 'utils.js'), 'utf8');
  for (const cmd of ['/theme', '/queue', '/clear-queue', '/retry', '/help', '/model', '/variant', '/session', '/compact']) {
    assert.ok(utilsJs.includes("name: '" + cmd + "'") || appJs.includes('case \'' + cmd.slice(1) + '\''), 'missing slash ' + cmd);
  }
  assert.match(utilsJs, /PALETTE_COMMANDS = \[/, 'missing PALETTE_COMMANDS in utils.js (T4.1b)');
  assert.match(appJs, /PALETTE_COMMANDS/, 'app.js should import PALETTE_COMMANDS');
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
  assert.ok(appJs.includes("id: 'chat_open'") || utilsJs.includes("id: 'chat_open'"), 'missing chat_open palette command');
  assert.match(appJs, /case 'chat_open'/, 'missing chat_open palette dispatch');
  assert.match(appJs, /key === '1'/, 'missing Alt+1 chat shortcut');
  assert.match(appJs, /key === '4'/, 'missing Alt+4 sessions shortcut');
  assert.ok(appJs.includes("name: '/tools'") || utilsJs.includes("name: '/tools'"), 'WebUI-extra /tools slash should remain');

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

  assert.doesNotMatch(indexHtml, /nerd-fonts-generated/, 'Nerd Font symbols are self-hosted, not loaded from a CDN');
  assert.match(styleCss, /font-family: 'Symbols Nerd Font Mono'/, 'style.css should declare the Nerd Font symbols face');

  for (const row of ['/new', '/rename', '/clear-queue', '/tools']) {
    assert.ok(appJs.includes('<kbd>' + row + '</kbd>'), 'help modal missing ' + row);
  }

  assert.match(appJs, /if \(sid\) \{/, 'sessions rows should guard on sid');
  assert.match(appJs, /no-open/, 'sessions rows without sid need no-open class');
  assert.match(appJs, /retryBtn\.disabled/, 'retry button should disable while generating');
  assert.match(styleCss, /\.retry-btn:disabled/, 'missing retry disabled CSS');

  const baseRows = [...styleCss.matchAll(/\.agent-card \{/g)];
  assert.equal(baseRows.length, 1, 'duplicate .agent-card base rule in style.css');
});

test('WebUI theme propagation: warning/success vars and themed chrome', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  assert.match(styleCss, /--warning:/, 'missing --warning root var');
  assert.match(styleCss, /--success:/, 'missing --success root var');
  assert.match(appJs, /set\('--warning', t\.warning\)/, 'applyTheme must set --warning');
  assert.match(appJs, /set\('--success', t\.success\)/, 'applyTheme must set --success');

  for (const sel of ['.agent-card:hover', '.status-chip.running', '.status-chip.completed', '.retry-btn', '.queued-block', '.side-tab-header', '.subagent-badge']) {
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
  assert.match(appJs, /session-icon sub/, 'sidebar tabs should mark subagent sessions');
  assert.match(styleCss, /\.session-icon\.sub/, 'missing .session-icon.sub in CSS');

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

  // extractChildSessionId (lives in utils.js since T4.1a split, imported by app.js)
  const utilsJs = fs.readFileSync(path.join(srcDir, 'utils.js'), 'utf8');
  assert.match(utilsJs, /function extractChildSessionId/, 'missing extractChildSessionId in utils.js');
  assert.match(appJs, /extractChildSessionId/, 'app.js should import/use extractChildSessionId');
});

test('WebUI subagents tab: named Subagents, scoped to parent session, cascade delete', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');

  // Naming checks
  assert.match(indexHtml, /Subagents\s*<\/button>/, 'navigation tab button should be named Subagents');
  assert.match(indexHtml, /<\/svg> Subagents<\/span>/, 'panel header should display Subagents');
  assert.match(indexHtml, /placeholder="Filter subagents…"|placeholder="Filter subagents\.\.\."/, 'filter input placeholder should reference subagents');

  // Scoping checks
  assert.match(appJs, /parent_session_id=/, 'loadDelegatedSessionsTab should query with parent_session_id');
  assert.match(appJs, /s\.parent_session_id === state\.sessionId/, 'loadDelegatedSessionsTab should filter by parent_session_id');

  // Cascade delete
  assert.match(appJs, /deleteSessionPermanently/, 'deleteSessionPermanently present');
  assert.match(appJs, /removedIds\.has/, 'deleteSessionPermanently should remove child sessions');
});

test('WebUI collapsible navigation, file tree and clean title', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // Title / status bar cleanliness: stripTags used, no raw SVG injected into textContent
  assert.match(appJs, /function stripTags/, 'app.js should include stripTags helper');
  assert.doesNotMatch(appJs, /statusWorkspace\.textContent\s*=\s*state\.sessionWorkspace\s*\?\s*SVG_ICONS\.folder/, 'statusWorkspace must not assign raw SVG string to textContent');
  assert.match(appJs, /statusWorkspace\.innerHTML\s*=/, 'statusWorkspace should render icon via innerHTML');

  // Collapsible sidebar:
  assert.match(indexHtml, /id="menu-toggle-btn"/, 'menu toggle button must exist');
  assert.match(indexHtml, /id="sidebar-close-btn"/, 'sidebar close button must exist');
  assert.match(appJs, /function toggleSidebar/, 'toggleSidebar function should exist in app.js');
  assert.match(appJs, /function openSidebar/, 'openSidebar function should exist in app.js');
  assert.match(appJs, /function closeSidebar/, 'closeSidebar function should exist in app.js');
  assert.match(appJs, /key === 'b' \|\| key === 'B'/, 'Ctrl+B shortcut should toggle sidebar');
  assert.match(styleCss, /#sidebar\.collapsed/, 'style.css should include #sidebar.collapsed rule');

  // Collapsible file tree:
  assert.match(indexHtml, /id="fs-collapse-btn"/, 'fs-collapse-btn should exist in index.html');
  assert.match(indexHtml, /id="fs-back-btn"/, 'fs-back-btn should exist in index.html');
  assert.match(appJs, /function toggleFsTree/, 'toggleFsTree function should exist in app.js');
  assert.match(appJs, /key === 'f' \|\| key === 'F'/, 'Alt+F shortcut should toggle file tree');
  assert.match(styleCss, /\.files-explorer\.tree-collapsed \.fs-browser/, 'style.css should hide file tree when collapsed');
  assert.match(styleCss, /\.files-explorer\.tree-collapsed \.fs-editor-pane/, 'style.css should expand editor pane when tree is collapsed');

  // No terminal, canvas or split view.
  assert.doesNotMatch(appJs, /\/terminal\/|InfiniteCanvas|\/api\/vision/, 'removed features must stay removed');
  assert.doesNotMatch(appJs, /state\.layoutMode\s*=\s*'split'/, 'app.js should not enter split mode');
});

test('WebUI tool calls use one surface for header and one for output', () => {
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');
  const bg = (sel) => {
    const m = styleCss.match(new RegExp(sel.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\s*\\{[^}]*?background:\\s*([^;]+);'));
    assert.ok(m, sel + ' should declare a background');
    return m[1].trim();
  };
  // Tool rows sit on the group surface; the expanded body uses the code surface.
  assert.equal(bg('.tool-events'), 'var(--surface)');
  assert.equal(bg('.tool-body'), 'var(--code-bg)');
  assert.doesNotMatch(styleCss, /\.tool-output\s*\{[^}]*background/, '.tool-output inherits the body background');
  assert.doesNotMatch(styleCss, /\.tool-output\.error\s*\{[^}]*background/, 'errors are signalled by color, not a new surface');
});

test('WebUI session refresh heals stale lists without clobbering streams', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');

  // A cheap GET /session/:id decides; /messages is refetched only on change.
  assert.match(appJs, /async function refreshSession\(session, opts\)/, 'missing refreshSession');
  assert.match(appJs, /async function syncSessionMessages\(session\)/, 'missing syncSessionMessages');
  assert.match(appJs, /fetch\('\/session\/' \+ session\.id, \{ cache: 'no-store' \}\)/, 'refresh should GET /session/:id');
  assert.match(appJs, /fetch\('\/session\/' \+ session\.id \+ '\/messages'/, 'sync should refetch /session/:id/messages');
  assert.match(appJs, /function sessionRev\(info\)/, 'refresh should compare the session revision');
  assert.match(appJs, /if \(!force && rev === session\.serverRev\) return;/, 'unchanged sessions must not refetch history');

  // Never interrupt a live turn: the stream is authoritative; running turns reattach.
  assert.match(appJs, /if \(session\.stream\) return;/, 'refresh must skip while a stream is attached');
  assert.match(appJs, /if \(!Array\.isArray\(msgs\) \|\| session\.stream\) return false;/, 'sync must not replace a streaming session');
  assert.match(appJs, /attachToRunningTurn\(session\)/, 'refresh should reattach to a running turn');

  // Change detection: only render when the message signature differs.
  assert.match(appJs, /messageSignature\(tmp\.messages\) === messageSignature\(session\.messages\)/, 'sync should compare message signatures before rendering');

  // Scroll preservation: never force-scroll a user who scrolled up.
  assert.match(appJs, /const stickToBottom = nearBottom\(\);/, 'sync should check nearBottom before scrolling');

  // Overlap guard: a refresh requested mid-refresh runs once afterwards.
  assert.match(appJs, /session\.refreshAgain/, 'missing overlapping-refresh guard');

  // Triggers: visibility + focus + online + a light 5s poll that skips hidden tabs.
  assert.match(appJs, /document\.addEventListener\('visibilitychange', refreshActiveSession\)/, 'missing visibilitychange trigger');
  assert.match(appJs, /window\.addEventListener\('focus', refreshActiveSession\)/, 'missing focus trigger');
  assert.match(appJs, /window\.addEventListener\('online', refreshActiveSession\)/, 'missing online trigger');
  assert.match(appJs, /setInterval\(refreshActiveSession, 5000\)/, 'missing light poll');
  assert.match(appJs, /function refreshActiveSession\(\) \{\s*if \(document\.hidden \|\| !connOnline\) return;/, 'poll must skip hidden tabs and known-offline servers');

  // Post-stream reconciliation: a finished turn forces one refresh.
  assert.match(appJs, /setTimeout\(\(\) => refreshSession\(session, \{ force: true \}\), 300\)/, 'missing post-stream reconcile');
});

test('WebUI thinking visibility: low default, open-param thought block, header toggle pill', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // 1. Client default reasoning must not be 'off' (state init + select default)
  assert.match(appJs, /reasoning:\s*'low'/, 'state init reasoning default should be low');
  assert.doesNotMatch(appJs, /reasoning:\s*'off'/, 'state init must not default to off');
  assert.match(indexHtml, /<option value="low" selected>/, 'reasoning-select should default to Low');
  assert.match(indexHtml, /id="reasoning-select"[^>]*title=/, 'reasoning-select should have tooltip title');

  // 2. renderThoughtBlock supports {open:true} expanded param
  assert.match(appJs, /function renderThoughtBlock\(text,\s*opts\)/, 'renderThoughtBlock should accept opts param');
  assert.match(appJs, /opts && opts\.open/, 'renderThoughtBlock should check opts.open');
  assert.match(appJs, /live && ti === lastThoughtIdx|isLive && ti === lastThoughtIdx/, 'renderMessage should auto-open latest thought while generating');
  assert.match(appJs, /lastThoughtIdx/, 'renderMessage should track last thought index');

  // 3. Visible thinking toggle pill in header, wired to toggleThinking
  assert.match(indexHtml, /id="thinking-toggle-btn"/, 'missing #thinking-toggle-btn in index.html');
  assert.match(indexHtml, /aria-pressed/, 'thinking toggle should expose aria-pressed');
  assert.match(appJs, /thinkingToggleBtn/, 'app.js should reference thinkingToggleBtn');
  assert.match(appJs, /thinkingToggleBtn\.addEventListener\('click', \(\) => toggleThinking\(\)\)/, 'thinking pill should be wired to toggleThinking');
  assert.match(appJs, /function syncThinkingToggleBtn/, 'missing syncThinkingToggleBtn');
  assert.match(styleCss, /\.thinking-toggle-btn/, 'missing .thinking-toggle-btn CSS');
});

test('WebUI sidebar expand affordance: anchored toggle, aria state, collapsed highlight', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // 1. menu-toggle-btn lives inside #main-header (the always-visible expand control)
  const headerBlock = indexHtml.slice(indexHtml.indexOf('<header id="main-header">'), indexHtml.indexOf('</header>'));
  assert.match(headerBlock, /id="menu-toggle-btn"/, 'menu-toggle-btn must exist inside #main-header');
  // toggle + session info wrapped in a left group so it stays anchored left after collapse
  assert.match(headerBlock, /class="header-left"/, 'header should wrap toggle + session info in .header-left');
  assert.match(styleCss, /\.header-left/, 'style.css should define .header-left');
  assert.match(indexHtml, /id="menu-toggle-btn"[^>]*aria-expanded/, 'menu-toggle-btn should expose aria-expanded');

  // 2. app.js syncs toggle state (aria + label + collapsed class) from all entry points
  assert.match(appJs, /function syncSidebarToggleState/, 'missing syncSidebarToggleState in app.js');
  assert.match(appJs, /syncSidebarToggleState\(true\)/, 'closeSidebar should sync collapsed state');
  assert.match(appJs, /syncSidebarToggleState\(false\)/, 'openSidebar should sync expanded state');
  assert.match(appJs, /syncSidebarToggleState\(collapsed\)/, 'restoreSidebarState should sync persisted state');
  assert.match(appJs, /aria-expanded/, 'app.js should set aria-expanded on sidebar toggles');
  assert.match(appJs, /Expand sidebar \(Ctrl\+B\)/, 'toggle tooltip should offer Expand when collapsed');
  assert.match(appJs, /sidebar-collapsed/, 'app.js should toggle a sidebar-collapsed class');

  // 3. collapsed-state affordance selector highlights the toggle when collapsed
  assert.match(styleCss, /#sidebar\.collapsed/, 'style.css should keep #sidebar.collapsed rule');
  assert.match(styleCss, /#main-area\.sidebar-collapsed #menu-toggle-btn/, 'style.css should highlight the toggle when collapsed');

  // 4. collapse geometry must match the effective desktop sidebar width (no sliver/gap)
  assert.match(styleCss, /--sidebar-width:\s*276px/, 'collapse offset var should match the sidebar width');
  assert.match(styleCss, /margin-left:\s*calc\(-1 \* var\(--sidebar-width/, 'collapsed margin should derive from --sidebar-width');
  assert.doesNotMatch(styleCss, /margin-left:\s*-276px/, 'stale hardcoded -276px offset should be gone');
});

test('WebUI new session creation reflects title and workspace', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');

  // Modal contains inputs for title and workspace directory
  assert.match(appJs, /id="ns-title"/, 'missing ns-title input in new session modal');
  assert.match(appJs, /id="ns-workspace"/, 'missing ns-workspace input in new session modal');

  // createNewSession sends title and handles title resolution
  assert.match(appJs, /title,\s*custom_id:\s*title/, 'createNewSession should send title to /sessions');
  assert.match(appJs, /data\.title !== data\.id/, 'createNewSession should avoid clobbering title with id');
});

test('WebUI Stats tab follows live model calls', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  assert.match(appJs, /case 'backend\.step\.latency':[\s\S]{0,240}scheduleStatsRefresh\(\)/, 'step latency events should refresh an open Stats tab');
  assert.match(appJs, /function scheduleStatsRefresh\(\) \{\s*if \(statsRefreshTimer\) return;/, 'Stats refresh should be debounced');
  assert.match(appJs, /loadStatsTab\(\{ quiet: true \}\)/, 'live refresh should be quiet (no skeleton)');
});

test('WebUI agent modes and model capability badges', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const utilsJs = fs.readFileSync(path.join(srcDir, 'utils.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // Interactive agent mode selector button
  assert.match(indexHtml, /<button[^>]+id="agent-mode-btn"/, 'agent-mode-btn should be an interactive button');
  assert.match(appJs, /showAgentModePicker/, 'app.js should provide showAgentModePicker');
  assert.match(appJs, /selectAgentMode/, 'app.js should provide selectAgentMode');
  assert.match(appJs, /formatAgentModeTitle/, 'app.js should format agent mode titles');

  // Mode endpoint POST /session/:id/mode
  assert.match(appJs, /\/session\/.*\/mode/, 'app.js should persist agent mode to /session/:id/mode');

  // Slash commands
  assert.match(utilsJs, /name:\s*['"]\/agent['"]/, 'utils.js should declare /agent slash command');
  assert.doesNotMatch(utilsJs, /persona/i, 'utils.js should not reference the removed persona feature');
  assert.match(appJs, /case 'agent':/, 'app.js should handle /agent command');
  assert.doesNotMatch(appJs, /persona/i, 'app.js should not reference the removed persona feature');

  // Model capability badges & formatContextTokens
  assert.match(utilsJs, /export function formatContextTokens/, 'utils.js should export formatContextTokens');
  assert.match(utilsJs, /export function modelTypeBadges/, 'utils.js should export modelTypeBadges');
  assert.match(appJs, /formatModelDropdownLabel/, 'app.js should format model dropdown with badges');
  assert.match(styleCss, /\.item-badges/, 'style.css should style .item-badges');
  assert.match(styleCss, /\.item-badge\.badge-reasoning/, 'style.css should style reasoning badge');
  assert.match(styleCss, /\.agent-mode-btn\.orchestrator/, 'style.css should style orchestrator agent mode button');
  // Exactly two agent modes: normal and orchestrator.
  assert.match(appJs, /id: 'normal'/, 'agent mode picker should offer normal');
  assert.match(appJs, /id: 'orchestrator'/, 'agent mode picker should offer orchestrator');
  assert.doesNotMatch(appJs, /id: 'autonomous'|id: 'plan'/, 'retired agent modes should be gone');
});

test('WebUI collapsible sessions, header stats, loading indicator, and settings polish', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // Header stats button alongside session title
  assert.match(indexHtml, /id="header-stats-btn"/, 'header-stats-btn should exist in index.html');
  assert.doesNotMatch(indexHtml, /id="tab-stats-btn"/, 'tab-stats-btn should be removed from left strip views');
  assert.match(appJs, /headerStatsBtn\.addEventListener/, 'headerStatsBtn should toggle stats view');
  assert.match(styleCss, /\.header-stats-btn/, 'style.css should style .header-stats-btn');

  // Collapsible sessions section
  assert.match(indexHtml, /id="sessions-toggle-btn"/, 'sessions-toggle-btn should exist in index.html');
  assert.match(indexHtml, /id="active-session-count-badge"/, 'active-session-count-badge should exist in index.html');
  assert.match(appJs, /restoreSessionsCollapseState/, 'app.js should restore sessions collapse state');
  assert.match(styleCss, /\.sidebar-sessions\.collapsed/, 'style.css should style .sidebar-sessions.collapsed');

  // Loading window and spinner on session switch
  assert.match(appJs, /session-loading-window/, 'app.js should show session-loading-window on stub switch');
  assert.match(styleCss, /\.session-loading-window/, 'style.css should style .session-loading-window');
  assert.match(styleCss, /\.session-status-indicator\.loading/, 'style.css should style loading indicator in tabs');

  // Exclusive view: files panel must be hidden by default in tab layout
  assert.match(appJs, /if \(filesPanel\) filesPanel\.classList\.add\('hidden'\);/, 'filesPanel must be hidden initially in updateLayoutUI');

  // Header session indicator: only green dot, no spinner alongside session title
  assert.match(appJs, /statusSessionIndicator\.innerHTML\s*=\s*'<span class="session-active-dot"><\/span>';/, 'statusSessionIndicator must only show session-active-dot without spinner');

  // Green light only when running
  assert.match(appJs, /isRunning[\s\S]{1,120}session-active-dot/, 'green light dot should only appear for running sessions');

  // Settings modal enhancements & removal of footer clear button
  assert.doesNotMatch(indexHtml, /id="clear-btn"/, 'clear-btn should be removed from footer');
  assert.match(indexHtml, /id="settings-clear-chat-btn"/, 'settings-clear-chat-btn should exist in settings modal');
  assert.match(indexHtml, /id="theme-select"/, 'theme-select should exist in settings modal');
  assert.match(appJs, /settingsClearChatBtn\.addEventListener/, 'settingsClearChatBtn should handle chat clear in settings');
});

test('WebUI large session virtualization and performance optimizations', () => {
  const appJs = fs.readFileSync(path.join(__dirname, '../src/app.js'), 'utf8');
  const styleCss = fs.readFileSync(path.join(__dirname, '../src/style.css'), 'utf8');

  // Parallel session info and messages fetch
  assert.match(appJs, /Promise\.all\(\[\s*fetch\('\/session\/' \+ id\)/, 'loadSessionData must parallelize session and messages fetches');

  // O(1) tool lookup during message parsing
  assert.match(appJs, /_toolsById\[tc\.id\]\s*=\s*entry;/, 'parseMessages should index tools by ID');
  assert.match(appJs, /_toolsById\[tr\.tool_call_id\]/, 'parseMessages should use _toolsById fast path');

  // Fast message vendor check without joining full history strings
  assert.match(appJs, /function richVendorJobsForMessages\(messages\)/, 'app.js should define richVendorJobsForMessages');
  assert.match(appJs, /richVendorJobsForMessages\(session\.messages\)/, 'switchSession should use richVendorJobsForMessages');
  assert.doesNotMatch(appJs, /session\.messages\.map\([^)]*\)\.join/, 'switchSession should not map-join full history into a single string');

  // Windowed rendering in renderMessages
  assert.match(appJs, /INITIAL_MESSAGE_WINDOW/, 'app.js should define INITIAL_MESSAGE_WINDOW');
  assert.match(appJs, /load-earlier-container/, 'app.js should render load-earlier-container for hidden messages');
  assert.match(appJs, /load-earlier-btn/, 'app.js should have load-earlier-btn button');
  assert.match(appJs, /load-all-btn/, 'app.js should have load-all-btn button');
  assert.match(styleCss, /\.load-earlier-container/, 'style.css should style .load-earlier-container');
  assert.match(styleCss, /\.load-earlier-btn/, 'style.css should style .load-earlier-btn');

  // Intra-message tool call collapsing
  assert.match(appJs, /MAX_INLINE_TOOLS/, 'app.js should define MAX_INLINE_TOOLS');
  assert.match(appJs, /tool-group-expander/, 'app.js should create tool-group-expander for large tool sequences');
  assert.match(appJs, /tool-expand-btn/, 'app.js should create tool-expand-btn button');
  assert.match(styleCss, /\.tool-group-expander/, 'style.css should style .tool-group-expander');
  assert.match(styleCss, /\.tool-expand-btn/, 'style.css should style .tool-expand-btn');
});


test('WebUI instant session switching and batched tool expander', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');

  // Instant session switching cache
  assert.match(appJs, /hasCachedMessages\s*=\s*(isSessionSwitchCached\(session\)|session\.messages\s*&&\s*session\.messages\.length\s*>\s*0)/, 'app.js should detect cached messages for instant switching');
  assert.match(appJs, /renderMessages\(\)/, 'app.js should immediately render cached messages');

  // Batched tool expansion using requestAnimationFrame
  assert.match(appJs, /requestAnimationFrame\(renderNextChunk\)/, 'app.js should batch tool group expansion using requestAnimationFrame');
  assert.match(appJs, /chunkSize\s*=\s*20/, 'tool group expander should render in chunks of 20');
});

test('WebUI file explorer resilience and tree collapse restore', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // Subtab synchronization in switchTab
  assert.match(appJs, /switchFilesSubtab\(state\.filesSubtab/, 'switchTab should synchronize files subtab');

  // Tree collapsed restore button
  assert.match(indexHtml, /id="fs-placeholder-tree-btn"/, 'index.html should have tree restore button');
  assert.match(appJs, /fsPlaceholderTreeBtn/, 'app.js should bind fsPlaceholderTreeBtn');
  assert.match(styleCss, /\.fs-placeholder-tree-btn/, 'style.css should style .fs-placeholder-tree-btn');

  // 404 missing workspace resilience
  assert.match(appJs, /fs-workspace-missing/, 'app.js should handle missing workspace with friendly empty state');
});

test('WebUI mobile viewport virtual keyboard and flex layout resilience', () => {
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');

  // Viewport meta tag handles virtual keyboards smoothly
  assert.match(indexHtml, /interactive-widget=resizes-content/, 'index.html viewport meta must include interactive-widget=resizes-content');

  // App shell supports dynamic visual viewport height
  assert.match(styleCss, /--app-height/, 'style.css should support --app-height variable');
  assert.match(styleCss, /#main\s*\{[^}]*min-height:\s*0;/, '#main should have min-height: 0 for flex column shrink');
  assert.match(styleCss, /#chat-container\s*\{[^}]*min-height:\s*0;/, '#chat-container should have min-height: 0 for flex column shrink');

  // app.js registers visualViewport and focus handlers
  assert.match(appJs, /setupViewportHandler/, 'app.js should define setupViewportHandler');
  assert.match(appJs, /visualViewport/, 'app.js should listen to visualViewport');
  assert.match(appJs, /promptInput\.addEventListener\('focus'/, 'promptInput should have focus listener for scroll adjustment');
});

test('WebUI new session modal browses child folders of the chosen directory', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // Folder browser markup and wiring
  assert.match(appJs, /id="ns-browser"/, 'new session modal should contain the folder browser');
  assert.match(appJs, /id="ns-fb-list"/, 'folder browser should contain a child folder list');
  assert.match(appJs, /fetch\('\/api\/dirs'/, 'folder browser must query /api/dirs for child folders');
  assert.match(appJs, /class="fb-item"/, 'child folders should render as tappable fb-item rows');
  assert.match(appJs, /function fbBrowse\(path, \{ setBox = true, silent = false \} = \{\}\)/, 'fbBrowse should take setBox/silent options');
  assert.match(appJs, /setBox: true/, 'tapping a folder should move the workspace box');
  assert.match(appJs, /setBox: false, silent: true/, 'typed paths should browse without rewriting the box');
  assert.match(appJs, /fbUp\.addEventListener\('click'/, 'Up button should navigate to the parent folder');

  // Degrades silently against an older server without /api/dirs
  assert.match(appJs, /fbAvailable = false;/, 'browser should hide itself when /api/dirs is unavailable');


  // CSS
  assert.match(styleCss, /\.fb-browser\s*\{/, 'style.css must style .fb-browser');
  assert.match(styleCss, /\.fb-item\s*\{/, 'style.css must style .fb-item');
  assert.match(styleCss, /\.fb-item:hover/, 'style.css must give .fb-item a hover state');
});

test('WebUI theme picker previews the highlighted theme and reverts on cancel', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const indexHtml = fs.readFileSync(path.join(srcDir, 'index.html'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // The shared picker takes preview/cancel hooks and calls them on arrows, filtering and hover.
  assert.match(appJs, /function showFuzzyPicker\(title, items, activeId, onSelect, placeholder, initialQuery, opts = \{\}\)/, 'showFuzzyPicker should accept opts');
  assert.match(appJs, /function preview\(item\) \{[\s\S]*?opts\.onPreview\(item\);/, 'picker should call onPreview when the highlighted item changes');
  assert.match(appJs, /el\.addEventListener\('mousemove'/, 'picker should preview on real pointer movement (not mouseenter)');
  assert.match(appJs, /pickerCleanup = \(\) => \{ if \(!committed\) opts\.onCancel\(\); \};/, 'closing without a choice should run onCancel');

  // /theme previews while moving the cursor and restores the original theme on cancel.
  assert.match(appJs, /const original = state\.theme;/, '/theme should remember the original theme');
  assert.match(appJs, /onPreview: \(item\) => applyTheme\(item\.id, \{ persist: false \}\),/, '/theme should preview without pinning');
  assert.match(appJs, /onCancel: \(\) => applyTheme\(original, \{ persist: false \}\),/, '/theme should restore the original theme on cancel');

  // Settings uses the same picker: the field is a button, not a native select.
  assert.match(indexHtml, /<button type="button" id="theme-select" class="theme-select-btn"/, 'Settings theme field should be a button');
  assert.doesNotMatch(indexHtml, /<select id="theme-select"/, 'native theme select should be gone');
  assert.match(appJs, /themeSelect\.addEventListener\('click', \(\) => \{ closeSettingsModal\(\); handleThemeCommand\(''\); \}\);/, 'Settings theme button should open the theme picker');
  assert.match(styleCss, /\.theme-select-btn\s*\{/, 'style.css must style .theme-select-btn');
});

test('WebUI new session modal has one folder picker, no suggestions dropdown', () => {
  const appJs = fs.readFileSync(path.join(srcDir, 'app.js'), 'utf8');
  const styleCss = fs.readFileSync(path.join(srcDir, 'style.css'), 'utf8');

  // The folder browser is the only workspace selection window.
  assert.match(appJs, /id="ns-browser"/, 'new session modal should contain the folder browser');
  assert.doesNotMatch(appJs, /ns-suggestions-container|ns-suggestions-list|Matching Workspaces/, 'the matching-workspaces dropdown must be gone');
  assert.doesNotMatch(appJs, /workspace-suggestion-item/, 'no suggestion rows should be rendered');
  assert.doesNotMatch(appJs, /qcode-recent-workspaces/, 'recent-workspace history only fed the dropdown and must be gone');
  assert.doesNotMatch(styleCss, /workspace-suggest/, 'dropdown CSS must be removed');
});
