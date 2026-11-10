function formatPlantUmlTheme(text, theme = '') {
  let clean = (text || '').trim();
  if (!clean.startsWith('@start')) {
    clean = '@startuml\n' + clean + '\n@enduml';
  }
  if (theme === 'cyborg' || theme === 'dark') {
    if (/^!theme\s+\w+/m.test(clean)) {
      clean = clean.replace(/^!theme\s+\w+/m, '!theme cyborg');
    } else {
      clean = clean.replace(/^(@start[a-z]*\b[^\n]*)/m, '$1\n!theme cyborg');
    }
  } else if (theme === 'light') {
    clean = clean.replace(/^!theme\s+cyborg\r?\n?/m, '');
  }
  return clean;
}

function encodePlantUmlHex(text, theme = '') {
  const clean = formatPlantUmlTheme(text, theme);
  let hex = '';
  const bytes = new TextEncoder().encode(clean);
  for (let i = 0; i < bytes.length; i++) {
    hex += bytes[i].toString(16).padStart(2, '0');
  }
  return 'https://www.plantuml.com/plantuml/svg/~h' + hex;
}

window.togglePlantUmlDiagramMode = function(diagramId) {
  const container = document.getElementById(diagramId);
  if (!container) return;
  const currentMode = container.getAttribute('data-puml-mode') || 'dark';
  const newMode = currentMode === 'dark' ? 'light' : 'dark';
  container.setAttribute('data-puml-mode', newMode);

  const rawCode = decodeURIComponent(container.getAttribute('data-puml-code') || '');
  const newUrl = encodePlantUmlHex(rawCode, newMode);

  const img = container.querySelector('.plantuml-image-wrapper img');
  if (img) {
    img.src = newUrl;
  }
  const wrapper = container.querySelector('.plantuml-image-wrapper');
  if (wrapper) {
    if (newMode === 'dark') {
      wrapper.classList.remove('light-mode');
      wrapper.classList.add('dark-mode');
    } else {
      wrapper.classList.remove('dark-mode');
      wrapper.classList.add('light-mode');
    }
  }

  const modeBtnLabel = container.querySelector('.plantuml-mode-label');
  if (modeBtnLabel) {
    modeBtnLabel.innerText = newMode === 'dark' ? 'Light' : 'Dark';
  }

  const link = container.querySelector('.plantuml-link');
  if (link) {
    link.href = newUrl;
  }
};

window.qcodeCopyCode = function(btn) {
  const block = btn && btn.closest('.code-block-container');
  const code = block && block.querySelector('code');
  if (!code) return;
  copyText(code.innerText, 'Code copied');
  const label = btn.querySelector('span');
  btn.classList.add('done');
  if (label) label.textContent = 'Copied';
  setTimeout(() => { btn.classList.remove('done'); if (label) label.textContent = 'Copy'; }, 1500);
};

window.copyPlantUmlCode = function(diagramId, btn) {
  const container = document.getElementById(diagramId);
  if (!container) return;
  const rawCode = decodeURIComponent(container.getAttribute('data-puml-code') || '');
  const mode = container.getAttribute('data-puml-mode') || 'dark';
  const codeToCopy = formatPlantUmlTheme(rawCode, mode);

  navigator.clipboard.writeText(codeToCopy).then(() => {
    const targetBtn = btn || (typeof event !== 'undefined' ? event.target.closest('button') : null);
    if (targetBtn) {
      const orig = targetBtn.innerText;
      targetBtn.innerText = 'Copied!';
      setTimeout(() => targetBtn.innerText = orig, 2000);
    }
  });
};

import { fuzzyScore, fuzzyFilter, shortPath, formatBytes, formatMs, formatNumber, formatUsd, usageCostSummary, renderUsageSections, detectFsLanguage, parseToolValue, sessionIdFromTaskResult, extractChildSessionId, esc, capitalize, relTime, extractFrontmatter, stripFrontmatter, transformWikilinks, findStableMarkdownCut, keepInterruptedPartials, formatContextTokens, modelTypeBadges, SLASH_COMMANDS, PALETTE_COMMANDS, healthRetryDelayMs, isRetryableHttpStatus, isSessionSwitchCached, shouldShowSessionLoading } from './utils.js';
// ── SVG Icons ──
const SVG_ICONS = {
  chat: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/></svg>`,
  folder: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/></svg>`,
  file: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"/><polyline points="13 2 13 9 20 9"/></svg>`,
  user: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>`,
  assistant: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="11" width="18" height="10" rx="2"/><circle cx="12" cy="5" r="2"/><path d="M12 7v4"/><line x1="8" y1="16" x2="8.01" y2="16"/><line x1="16" y1="16" x2="16.01" y2="16"/></svg>`,
  tool: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z"/></svg>`,
  reasoning: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 11.5a8.38 8.38 0 0 1-.9 3.8 8.5 8.5 0 0 1-7.6 4.7 8.38 8.38 0 0 1-3.8-.9L3 21l1.9-5.7a8.38 8.38 0 0 1-.9-3.8 8.5 8.5 0 0 1 4.7-7.6 8.38 8.38 0 0 1 3.8-.9h.5a8.48 8.48 0 0 1 8 8v.5z"/></svg>`,
  rename: `<svg class="ui-icon" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20h9"/><path d="M16.5 3.5a2.121 2.121 0 0 1 3 3L7 19l-4 1 1-4L16.5 3.5z"/></svg>`,
  delete: `<svg class="ui-icon" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>`,
  split: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><line x1="12" y1="3" x2="12" y2="21"/></svg>`,
  tab: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><line x1="3" y1="9" x2="21" y2="9"/></svg>`,
  system: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>`
};

// Compact line icons (24px grid, stroke = currentColor) for chat chrome.
const ICON_PATHS = {
  copy: '<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>',
  check: '<polyline points="20 6 9 17 4 12"/>',
  x: '<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>',
  retry: '<path d="M21 12a9 9 0 1 1-2.64-6.36"/><polyline points="21 3 21 9 15 9"/>',
  code: '<polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/>',
  terminal: '<polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/>',
  search: '<circle cx="11" cy="11" r="7"/><line x1="21" y1="21" x2="16.65" y2="16.65"/>',
  fileText: '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="8" y1="13" x2="16" y2="13"/><line x1="8" y1="17" x2="13" y2="17"/>',
  pencil: '<path d="M12 20h9"/><path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z"/>',
  bot: '<rect x="4" y="8" width="16" height="12" rx="3"/><path d="M12 8V4"/><circle cx="12" cy="3" r="1"/><line x1="9" y1="14" x2="9" y2="14.01"/><line x1="15" y1="14" x2="15" y2="14.01"/>',
  globe: '<circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 0 20a15.3 15.3 0 0 1 0-20z"/>',
  wrench: '<path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94z"/>',
  chevron: '<polyline points="9 6 15 12 9 18"/>',
  sparkle: '<path d="M12 3l1.9 5.6L19.5 10.5 13.9 12.4 12 18l-1.9-5.6L4.5 10.5l5.6-1.9z"/>',
  brain: '<path d="M9.5 2A2.5 2.5 0 0 0 7 4.5v.1A3 3 0 0 0 4.6 9 3 3 0 0 0 5 14.8 3 3 0 0 0 9.5 19.5V22"/><path d="M14.5 2A2.5 2.5 0 0 1 17 4.5v.1A3 3 0 0 1 19.4 9a3 3 0 0 1-.4 5.8 3 3 0 0 1-4.5 4.7V22"/><path d="M12 4v16"/>',
  alert: '<path d="M10.29 3.86 1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/>',
  info: '<circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/>',
  arrowDown: '<line x1="12" y1="5" x2="12" y2="19"/><polyline points="19 12 12 19 5 12"/>',
  external: '<path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/>',
  raw: '<path d="M4 7V4h16v3"/><line x1="9" y1="20" x2="15" y2="20"/><line x1="12" y1="4" x2="12" y2="20"/>'
};
function emptyState(iconName, title, desc) {
  return '<div class="empty-state"><div class="empty-state-icon">' + icon(iconName, 22) + '</div>'
    + '<div class="empty-state-title">' + esc(title) + '</div>'
    + (desc ? '<div class="empty-state-desc">' + esc(desc) + '</div>' : '') + '</div>';
}
function icon(name, size = 14) {
  return `<svg class="ui-icon" width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICON_PATHS[name] || ''}</svg>`;
}


function stripTags(html) {
  if (!html || typeof html !== 'string') return '';
  return html.replace(/<[^>]*>/g, '').trim();
}

function isMobileLayout() {
  return window.innerWidth <= 1024;
}

function syncSidebarToggleState(collapsed) {
  const menuToggleBtn = document.getElementById('menu-toggle-btn');
  const sidebarCloseBtn = document.getElementById('sidebar-close-btn');
  const mainArea = document.getElementById('main-area');
  const label = collapsed ? 'Expand sidebar (Ctrl+B)' : 'Collapse sidebar (Ctrl+B)';
  if (menuToggleBtn) {
    menuToggleBtn.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
    menuToggleBtn.setAttribute('aria-label', collapsed ? 'Expand sidebar' : 'Collapse sidebar');
    menuToggleBtn.setAttribute('title', label);
  }
  if (sidebarCloseBtn) {
    sidebarCloseBtn.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
  }
  if (mainArea) {
    mainArea.classList.toggle('sidebar-collapsed', collapsed);
  } else {
    document.body.classList.toggle('sidebar-collapsed', collapsed);
  }
}

function openSidebar() {
  const sidebar = document.getElementById('sidebar');
  const overlay = document.getElementById('sidebar-overlay');
  if (isMobileLayout()) {
    if (sidebar) sidebar.classList.add('active');
    if (overlay) overlay.classList.add('active');
    syncSidebarToggleState(false);
  } else {
    if (sidebar) sidebar.classList.remove('collapsed');
    try { localStorage.setItem('qcode-sidebar-collapsed', 'false'); } catch (_) {}
    syncSidebarToggleState(false);
  }
  handleSidebarResize();
}

function closeSidebar() {
  const sidebar = document.getElementById('sidebar');
  const overlay = document.getElementById('sidebar-overlay');
  if (isMobileLayout()) {
    if (sidebar) sidebar.classList.remove('active');
    if (overlay) overlay.classList.remove('active');
    syncSidebarToggleState(true);
  } else {
    if (sidebar) sidebar.classList.add('collapsed');
    try { localStorage.setItem('qcode-sidebar-collapsed', 'true'); } catch (_) {}
    syncSidebarToggleState(true);
  }
  handleSidebarResize();
}

function toggleSidebar() {
  const sidebar = document.getElementById('sidebar');
  if (isMobileLayout()) {
    if (sidebar && sidebar.classList.contains('active')) {
      closeSidebar();
    } else {
      openSidebar();
    }
  } else {
    if (sidebar && sidebar.classList.contains('collapsed')) {
      openSidebar();
    } else {
      closeSidebar();
    }
  }
}

// Dismiss the off-canvas drawer after navigating on narrow screens. On
// desktop the sidebar is docked and must stay as the user left it.
function closeMobileSidebar() {
  if (isMobileLayout()) closeSidebar();
}

function handleSidebarResize() {}

function restoreSidebarState() {
  if (isMobileLayout()) return;
  try {
    const saved = localStorage.getItem('qcode-sidebar-collapsed');
    const sidebar = document.getElementById('sidebar');
    const collapsed = saved === 'true';
    if (sidebar && collapsed) {
      sidebar.classList.add('collapsed');
    }
    syncSidebarToggleState(collapsed);
  } catch (_) {}
}

function updateActiveViewBadge() {
  const badge = document.getElementById('active-view-badge');
  if (!badge) return;
  const map = { chat: 'Chat', files: 'Files', stats: 'Stats', sessions: 'Subagents' };
  badge.textContent = map[state.activeTab] || 'Chat';
}

function restoreViewsCollapseState() {
  const section = document.getElementById('sidebar-views-section');
  const toggleBtn = document.getElementById('views-toggle-btn');
  if (!section) return;
  let collapsed = false;
  try {
    const saved = localStorage.getItem('qcode-views-collapsed');
    if (saved !== null) {
      collapsed = saved === 'true';
    } else if (isMobileLayout()) {
      collapsed = true;
    }
  } catch (_) {}
  if (collapsed) {
    section.classList.add('collapsed');
  } else {
    section.classList.remove('collapsed');
  }
  if (toggleBtn) toggleBtn.setAttribute('aria-expanded', String(!collapsed));
  updateActiveViewBadge();
}

function updateActiveSessionCountBadge() {
  const badge = document.getElementById('active-session-count-badge');
  if (!badge) return;
  badge.textContent = String(state.openSessions.length || 0);
}

function restoreSessionsCollapseState() {
  const section = document.getElementById('sidebar-sessions-section');
  const toggleBtn = document.getElementById('sessions-toggle-btn');
  if (!section) return;
  let collapsed = false;
  try {
    const saved = localStorage.getItem('qcode-sessions-collapsed');
    if (saved !== null) {
      collapsed = saved === 'true';
    }
  } catch (_) {}
  if (collapsed) {
    section.classList.add('collapsed');
  } else {
    section.classList.remove('collapsed');
  }
  if (toggleBtn) toggleBtn.setAttribute('aria-expanded', String(!collapsed));
  updateActiveSessionCountBadge();
}

function updateSettingsModalModelInfo() {
  const display = document.getElementById('settings-active-model-display');
  if (!display) return;
  const p = (state.providers || []).find(x => x.id === state.provider);
  const pName = p ? p.name : (state.provider || 'None');
  let mName = state.model || '';
  if (p && Array.isArray(p.models)) {
    const m = p.models.find(x => x.id === state.model);
    if (m && m.name) mName = m.name;
  }
  display.textContent = '';
  if (!mName) {
    display.textContent = pName || 'No model selected';
    return;
  }
  const nameEl = document.createElement('span');
  nameEl.className = 'settings-model-name';
  nameEl.textContent = mName;
  const provEl = document.createElement('span');
  provEl.className = 'settings-model-provider';
  provEl.textContent = pName;
  display.append(nameEl, provEl);
}

function openSettingsModal() {
  const modal = document.getElementById('settings-modal');
  const btn = document.getElementById('settings-btn');
  if (!modal) return;
  updateSettingsModalModelInfo();
  if (themeSelect) themeSelect.value = state.theme || 'qcode';
  modal.classList.remove('hidden');
  modal.classList.add('active');
  if (btn) btn.setAttribute('aria-expanded', 'true');
  const closeBtn = document.getElementById('settings-modal-close');
  if (closeBtn) closeBtn.focus();
}

function closeSettingsModal() {
  const modal = document.getElementById('settings-modal');
  const btn = document.getElementById('settings-btn');
  if (!modal) return;
  modal.classList.remove('active');
  modal.classList.add('hidden');
  if (btn) btn.setAttribute('aria-expanded', 'false');
  if (btn && typeof btn.focus === 'function') btn.focus();
}

function toggleFsTree() {
  if (window.innerWidth <= 768) {
    setFsMobileView(state.fsMobileView === 'viewer' ? 'browser' : 'viewer');
    return;
  }
  if (!filesExplorer) return;
  const isCollapsed = filesExplorer.classList.toggle('tree-collapsed');
  try {
    localStorage.setItem('qcode-fs-tree-collapsed', isCollapsed ? 'true' : 'false');
  } catch (_) {}
}

function restoreFsTreeState() {
  if (window.innerWidth > 768 && filesExplorer) {
    try {
      const saved = localStorage.getItem('qcode-fs-tree-collapsed');
      if (saved === 'true') {
        filesExplorer.classList.add('tree-collapsed');
      }
    } catch (_) {}
  }
}

function resizePromptInput() {
  if (!promptInput) return;
  promptInput.style.height = 'auto';
  const maxHeight = window.innerWidth <= 768 ? 120 : 180;
  promptInput.style.height = Math.min(promptInput.scrollHeight, maxHeight) + 'px';
}

// ── QCode Web UI ──────────────────────────────────────────────────
const state = {
  provider: '',
  model: '',
  reasoning: 'low',
  toolsEnabled: true,
  openSessions: [], // Array of { id, title, workspace, messages, generating, stream, provider, model }
  providers: [],
  sessionId: null,  // Active session ID
  sessionTitle: '',
  sessionWorkspace: '',
  // UI Tabs & layout state
  activeTab: 'chat', // 'chat' | 'files' | 'stats' | 'sessions'
  parentSessionId: null,
  agentMode: 'normal',
  layoutMode: 'tab', // 'tab' or 'split'
  // Files tab: explorer + editor
  filesSubtab: 'explorer', // 'explorer' | 'git'
  fsDir: '',               // relative path of current directory
  fsOpenPath: null,        // relative path of open file
  fsSavedContent: '',      // last loaded/saved content
  fsDirty: false,
  fsViewMode: 'placeholder', // 'placeholder' | 'code' | 'markdown' | 'image' | 'binary' | 'editor'
  fsMdMode: 'preview',       // 'preview' | 'code'
  fsLineWrap: false,
  fsFilterText: '',
  fsMobileView: 'browser',   // 'browser' | 'viewer'
  fsRawEntries: [],
  fsReady: false,
  fsCache: {},
  showThinking: true,
  theme: 'qcode',
  sessionsFilterText: ''
};

// ── DOM refs ──
const messagesEl = document.getElementById('messages');
const promptInput = document.getElementById('prompt-input');
const sendBtn = document.getElementById('send-btn');
const pauseBtn = document.getElementById('pause-btn');
const retryBtn = document.getElementById('retry-btn');
const settingsClearChatBtn = document.getElementById('settings-clear-chat-btn');
const providerSelect = document.getElementById('provider-select');
const reasoningSelect = document.getElementById('reasoning-select');
const statusSessionIndicator = document.getElementById('header-session-status');
const settingsBtn = document.getElementById('settings-btn');
const settingsModal = document.getElementById('settings-modal');
const settingsModalClose = document.getElementById('settings-modal-close');
const settingsSwitchModelBtn = document.getElementById('settings-switch-model-btn');
const settingsActiveModelDisplay = document.getElementById('settings-active-model-display');
const viewsToggleBtn = document.getElementById('views-toggle-btn');
const activeViewBadge = document.getElementById('active-view-badge');
const sessionsToggleBtn = document.getElementById('sessions-toggle-btn');
const activeSessionCountBadge = document.getElementById('active-session-count-badge');
const headerStatsBtn = document.getElementById('header-stats-btn');
const themeSelect = document.getElementById('theme-select');
const modalOverlay = document.getElementById('modal-overlay');
// One backdrop-click handler for every modal. Modals used to add their own
// listener on each open and only remove it on a backdrop click, so every
// modal closed by Escape or a button left one more listener behind.
modalOverlay.addEventListener('click', (e) => { if (e.target === modalOverlay) closeModal(); });
const streamStatusEl = document.getElementById('stream-status');
// Last element focused outside the modal, restored when the modal closes.
let modalReturnFocus = null;
document.addEventListener('focusin', (e) => {
  if (!modalOverlay.contains(e.target)) modalReturnFocus = e.target;
}, true);
// Keep Tab / Shift+Tab cycling inside an open modal (focus trap), and let Esc
// close the modal from any control inside it, not only from its text input.
// stopPropagation keeps the global Esc (pause generation) from firing too.
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    if (settingsModal && !settingsModal.classList.contains('hidden')) {
      e.preventDefault();
      e.stopPropagation();
      closeSettingsModal();
      return;
    }
    if (isModalOpen()) {
      e.preventDefault();
      e.stopPropagation();
      closeModal();
      return;
    }
  }
  if (e.key !== 'Tab' || !isModalOpen()) return;
  const focusables = [...modalOverlay.querySelectorAll('button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), a[href], [tabindex]:not([tabindex="-1"])')];
  if (!focusables.length) { e.preventDefault(); return; }
  const first = focusables[0];
  const last = focusables[focusables.length - 1];
  const inside = modalOverlay.contains(document.activeElement);
  if (e.shiftKey && (!inside || document.activeElement === first)) { e.preventDefault(); last.focus(); }
  else if (!e.shiftKey && (!inside || document.activeElement === last)) { e.preventDefault(); first.focus(); }
}, true);
const statusSession = document.getElementById('header-session-title');
const statusWorkspace = document.getElementById('header-session-workspace');
const newSessionBtn = document.getElementById('new-session-btn');
const filesPanel = document.getElementById('files-panel');
const filesContent = document.getElementById('files-content');
const filesRefreshBtn = document.getElementById('files-refresh-btn');
const filesExplorer = document.getElementById('files-explorer');
const filesGit = document.getElementById('files-git');
const filesSubtabExplorer = document.getElementById('files-subtab-explorer');
const filesSubtabGit = document.getElementById('files-subtab-git');
const fsBreadcrumb = document.getElementById('fs-breadcrumb');
const fsListing = document.getElementById('fs-listing');
const fsFilterInput = document.getElementById('fs-filter-input');
const fsEditorPane = document.getElementById('fs-editor-pane');
const fsBackBtn = document.getElementById('fs-back-btn');
const fsCollapseBtn = document.getElementById('fs-collapse-btn');
const fsFileIcon = document.getElementById('fs-file-icon');
const fsEditorPath = document.getElementById('fs-editor-path');
const fsFileBadge = document.getElementById('fs-file-badge');
const fsCopyPathBtn = document.getElementById('fs-copy-path-btn');
const fsCopyContentBtn = document.getElementById('fs-copy-content-btn');
const fsMdToggle = document.getElementById('fs-md-toggle');

const fsMdPreviewBtn = document.getElementById('fs-md-preview-btn');
const fsMdCodeBtn = document.getElementById('fs-md-code-btn');
const fsWrapBtn = document.getElementById('fs-wrap-btn');
const fsEditBtn = document.getElementById('fs-edit-btn');
const fsRawBtn = document.getElementById('fs-raw-btn');
const fsSaveBtn = document.getElementById('fs-save-btn');
const fsCloseBtn = document.getElementById('fs-close-btn');
const fsPlaceholderTreeBtn = document.getElementById('fs-placeholder-tree-btn');
const fsDirtyBadge = document.getElementById('fs-dirty-badge');

const fsViewPlaceholder = document.getElementById('fs-view-placeholder');
const fsViewCode = document.getElementById('fs-view-code');
const fsLineNumbers = document.getElementById('fs-line-numbers');
const fsCodeWrapper = document.getElementById('fs-code-wrapper');
const fsCodeHljs = document.getElementById('fs-code-hljs');
const fsViewMarkdown = document.getElementById('fs-view-markdown');
const fsViewImage = document.getElementById('fs-view-image');
const fsImagePreview = document.getElementById('fs-image-preview');
const fsImageInfo = document.getElementById('fs-image-info');
const fsViewBinary = document.getElementById('fs-view-binary');
const fsBinaryText = document.getElementById('fs-binary-text');
const fsBinaryDownloadLink = document.getElementById('fs-binary-download-link');
const fsViewEditor = document.getElementById('fs-view-editor');
const fsEditor = document.getElementById('fs-editor');
const fsHighlight = document.getElementById('fs-highlight');
const fsHighlightCode = document.getElementById('fs-highlight-code');
const fsEditorStatus = document.getElementById('fs-editor-status');
let fsHighlightTimer = null;
let fsLang = '';
const statsPanel = document.getElementById('stats-panel');
const statsContent = document.getElementById('stats-content');
const statsRefreshBtn = document.getElementById('stats-refresh-btn');
const statsCopyBtn = document.getElementById('stats-copy-btn');
const tabFilesBtn = document.getElementById('tab-files-btn');
const tabSessionsBtn = document.getElementById('tab-sessions-btn');
const sessionsPanel = document.getElementById('sessions-panel');
const sessionsContent = document.getElementById('sessions-content');
const sessionsRefreshBtn = document.getElementById('sessions-refresh-btn');
const parentBackBtn = document.getElementById('parent-back-btn');
const subagentBadge = document.getElementById('subagent-badge');
const agentModeBtn = document.getElementById('agent-mode-btn');
if (agentModeBtn) {
  agentModeBtn.addEventListener('click', () => {
    if (state.agentMode === 'subagent') return;
    showAgentModePicker();
  });
}
const thinkingToggleBtn = document.getElementById('thinking-toggle-btn');

// New DOM refs for tabs & layout toggle
const mainEl = document.getElementById('main');
const mainContentWrapperEl = document.getElementById('main-content-wrapper');
const tabChatBtn = document.getElementById('tab-chat-btn');
const sessionTabsContainer = document.getElementById('session-tabs-container');

// ── Modal state ──
let pickerCleanup = null;

// ── Slash commands ──




const THEMES = {
  "qcode": {
    "title": "QCode Web",
    "bg": "#0B0D12",
    "surface": "#11141B",
    "raised": "#181C25",
    "text": "#E6E9F0",
    "muted": "#9AA2B2",
    "accent": "#8EA2FF",
    "accent2": "#6D84FB",
    "danger": "#F87171",
    "warning": "#FBBF24",
    "success": "#4ADE80"
  },
  "qcode-light": {
    "title": "QCode Light",
    "scheme": "light",
    "bg": "#F6F7FA",
    "surface": "#FFFFFF",
    "raised": "#EDF0F5",
    "text": "#171B26",
    "muted": "#545C70",
    "accent": "#2F45C9",
    "accent2": "#2437A8",
    "danger": "#B42318",
    "warning": "#92400E",
    "success": "#166534"
  },
  "opencode": {
    "title": "OpenCode",
    "bg": "#0A0A0A",
    "surface": "#141414",
    "raised": "#1E1E1E",
    "text": "#EEEEEE",
    "muted": "#808080",
    "accent": "#FAB283",
    "accent2": "#5C9CF5",
    "danger": "#E06C75",
    "warning": "#F5A742",
    "success": "#7FD88F"
  },
  "tokyonight": {
    "title": "Tokyo Night",
    "bg": "#1A1B26",
    "surface": "#1E2030",
    "raised": "#222436",
    "text": "#C8D3F5",
    "muted": "#828BB8",
    "accent": "#82AAFF",
    "accent2": "#C099FF",
    "danger": "#FF757F",
    "warning": "#FF966C",
    "success": "#C3E88D"
  },
  "nord": {
    "title": "Nord",
    "bg": "#2E3440",
    "surface": "#3B4252",
    "raised": "#434C5E",
    "text": "#D8DEE9",
    "muted": "#8B95A7",
    "accent": "#88C0D0",
    "accent2": "#81A1C1",
    "danger": "#BF616A",
    "warning": "#D08770",
    "success": "#A3BE8C"
  },
  "gruvbox": {
    "title": "Gruvbox",
    "bg": "#282828",
    "surface": "#3C3836",
    "raised": "#504945",
    "text": "#EBDBB2",
    "muted": "#928374",
    "accent": "#83A598",
    "accent2": "#D3869B",
    "danger": "#FB4934",
    "warning": "#FE8019",
    "success": "#B8BB26"
  },
  "catppuccin": {
    "title": "Catppuccin Mocha",
    "bg": "#1E1E2E",
    "surface": "#181825",
    "raised": "#11111B",
    "text": "#CDD6F4",
    "muted": "#9399B2",
    "accent": "#89B4FA",
    "accent2": "#CBA6F7",
    "danger": "#F38BA8",
    "warning": "#F9E2AF",
    "success": "#A6E3A1"
  },
  "rosepine": {
    "title": "Ros\u00e9 Pine",
    "bg": "#191724",
    "surface": "#1F1D2E",
    "raised": "#26233A",
    "text": "#E0DEF4",
    "muted": "#6E6A86",
    "accent": "#9CCFD8",
    "accent2": "#C4A7E7",
    "danger": "#EB6F92",
    "warning": "#F6C177",
    "success": "#31748F"
  },
  "vesper": {
    "title": "Vesper",
    "bg": "#101010",
    "surface": "#101010",
    "raised": "#101010",
    "text": "#FFFFFF",
    "muted": "#A0A0A0",
    "accent": "#FFC799",
    "accent2": "#99FFE4",
    "danger": "#FF8080",
    "warning": "#FFC799",
    "success": "#99FFE4"
  },
  "one-dark": {
    "title": "One Dark",
    "bg": "#282C34",
    "surface": "#21252B",
    "raised": "#353B45",
    "text": "#ABB2BF",
    "muted": "#5C6370",
    "accent": "#61AFEF",
    "accent2": "#C678DD",
    "danger": "#E06C75",
    "warning": "#E5C07B",
    "success": "#98C379"
  },
  "aura": {
    "title": "Aura",
    "bg": "#0F0F0F",
    "surface": "#15141B",
    "raised": "#15141B",
    "text": "#EDECEE",
    "muted": "#6D6D6D",
    "accent": "#A277FF",
    "accent2": "#F694FF",
    "danger": "#FF6767",
    "warning": "#FFCA85",
    "success": "#61FFCA"
  },
  "zenburn": {
    "title": "Zenburn",
    "bg": "#3F3F3F",
    "surface": "#4F4F4F",
    "raised": "#5F5F5F",
    "text": "#DCDCCC",
    "muted": "#9F9F9F",
    "accent": "#8CD0D3",
    "accent2": "#DC8CC3",
    "danger": "#CC9393",
    "warning": "#F0DFAF",
    "success": "#7F9F7F"
  },
  "cobalt2": {
    "title": "Cobalt2",
    "bg": "#193549",
    "surface": "#122738",
    "raised": "#1F4662",
    "text": "#FFFFFF",
    "muted": "#ADB7C9",
    "accent": "#0088FF",
    "accent2": "#9A5FEB",
    "danger": "#FF0088",
    "warning": "#FFC600",
    "success": "#9EFF80"
  },
  "synthwave84": {
    "title": "SynthWave '84",
    "bg": "#262335",
    "surface": "#1E1A29",
    "raised": "#2A2139",
    "text": "#FFFFFF",
    "muted": "#848BBD",
    "accent": "#36F9F6",
    "accent2": "#FF7EDB",
    "danger": "#FE4450",
    "warning": "#FEDE5D",
    "success": "#72F1B8"
  },
  "osaka-jade": {
    "title": "Osaka Jade",
    "bg": "#111C18",
    "surface": "#1A2520",
    "raised": "#23372B",
    "text": "#C1C497",
    "muted": "#53685B",
    "accent": "#2DD5B7",
    "accent2": "#D2689C",
    "danger": "#FF5345",
    "warning": "#E5C736",
    "success": "#549E6A"
  },
  "matrix": {
    "title": "Matrix",
    "bg": "#0A0E0A",
    "surface": "#0E130D",
    "raised": "#141C12",
    "text": "#62FF94",
    "muted": "#8CA391",
    "accent": "#2EFF6A",
    "accent2": "#00EFFF",
    "danger": "#FF4B4B",
    "warning": "#E6FF57",
    "success": "#62FF94"
  },
  "flexoki": {
    "title": "Flexoki Dark",
    "bg": "#100F0F",
    "surface": "#1C1B1A",
    "raised": "#282726",
    "text": "#CECDC3",
    "muted": "#6F6E69",
    "accent": "#DA702C",
    "accent2": "#4385BE",
    "danger": "#D14D41",
    "warning": "#DA702C",
    "success": "#879A39"
  },
  "material": {
    "title": "Material Ocean",
    "bg": "#263238",
    "surface": "#1E272C",
    "raised": "#37474F",
    "text": "#EEFFFF",
    "muted": "#546E7A",
    "accent": "#82AAFF",
    "accent2": "#C792EA",
    "danger": "#F07178",
    "warning": "#FFCB6B",
    "success": "#C3E88D"
  },
  "ayu": {
    "title": "Ayu Dark",
    "bg": "#0B0E14",
    "surface": "#0F131A",
    "raised": "#0D1017",
    "text": "#BFBDB6",
    "muted": "#565B66",
    "accent": "#59C2FF",
    "accent2": "#D2A6FF",
    "danger": "#D95757",
    "warning": "#E6B673",
    "success": "#7FD962"
  },
  "everforest": {
    "title": "Everforest",
    "bg": "#2D353B",
    "surface": "#333C43",
    "raised": "#343F44",
    "text": "#D3C6AA",
    "muted": "#7A8478",
    "accent": "#A7C080",
    "accent2": "#7FBBB3",
    "danger": "#E67E80",
    "warning": "#E69875",
    "success": "#A7C080"
  },
  "kanagawa": {
    "title": "Kanagawa",
    "bg": "#1F1F28",
    "surface": "#2A2A37",
    "raised": "#363646",
    "text": "#DCD7BA",
    "muted": "#727169",
    "accent": "#7E9CD8",
    "accent2": "#957FB8",
    "danger": "#E82424",
    "warning": "#D7A657",
    "success": "#98BB6C"
  },
  "monokai": {
    "title": "Monokai",
    "bg": "#272822",
    "surface": "#1E1F1C",
    "raised": "#3E3D32",
    "text": "#F8F8F2",
    "muted": "#75715E",
    "accent": "#66D9EF",
    "accent2": "#AE81FF",
    "danger": "#F92672",
    "warning": "#E6DB74",
    "success": "#A6E22E"
  },
  "github": {
    "title": "GitHub Dark",
    "bg": "#0D1117",
    "surface": "#010409",
    "raised": "#161B22",
    "text": "#C9D1D9",
    "muted": "#8B949E",
    "accent": "#58A6FF",
    "accent2": "#BC8CFF",
    "danger": "#F85149",
    "warning": "#E3B341",
    "success": "#3FB950"
  },
  "solarized": {
    "title": "Solarized Dark",
    "bg": "#002B36",
    "surface": "#073642",
    "raised": "#073642",
    "text": "#839496",
    "muted": "#586E75",
    "accent": "#268BD2",
    "accent2": "#6C71C4",
    "danger": "#DC322F",
    "warning": "#B58900",
    "success": "#859900"
  },
  "dracula": {
    "title": "Dracula",
    "bg": "#282A36",
    "surface": "#21222C",
    "raised": "#44475A",
    "text": "#F8F8F2",
    "muted": "#6272A4",
    "accent": "#BD93F9",
    "accent2": "#FF79C6",
    "danger": "#FF5555",
    "warning": "#F1FA8C",
    "success": "#50FA7B"
  },
  "carbonfox": {
    "title": "Carbonfox",
    "bg": "#161616",
    "surface": "#1A1A1A",
    "raised": "#1E1E1E",
    "text": "#F2F4F8",
    "muted": "#7D848F",
    "accent": "#33B1FF",
    "accent2": "#78A9FF",
    "danger": "#EE5396",
    "warning": "#F1C21B",
    "success": "#25BE6A"
  },
  "catppuccin-frappe": {
    "title": "Catppuccin Frapp\\u00e9",
    "bg": "#303446",
    "surface": "#292C3C",
    "raised": "#232634",
    "text": "#C6D0F5",
    "muted": "#949CB8",
    "accent": "#8DA4E2",
    "accent2": "#CA9EE6",
    "danger": "#E78284",
    "warning": "#E5C890",
    "success": "#A6D189"
  },
  "catppuccin-macchiato": {
    "title": "Catppuccin Macchiato",
    "bg": "#24273A",
    "surface": "#1E2030",
    "raised": "#181926",
    "text": "#CAD3F5",
    "muted": "#939AB7",
    "accent": "#8AADF4",
    "accent2": "#C6A0F6",
    "danger": "#ED8796",
    "warning": "#EED49F",
    "success": "#A6DA95"
  },
  "cursor": {
    "title": "Cursor Dark",
    "bg": "#181818",
    "surface": "#141414",
    "raised": "#262626",
    "text": "#E4E4E4",
    "muted": "#E4E4E4",
    "accent": "#88C0D0",
    "accent2": "#81A1C1",
    "danger": "#E34671",
    "warning": "#F1B467",
    "success": "#3FA266"
  },
  "lucent-orng": {
    "title": "Lucent Orange",
    "bg": "#000000",
    "surface": "#000000",
    "raised": "#000000",
    "text": "#EEEEEE",
    "muted": "#808080",
    "accent": "#EC5B2B",
    "accent2": "#EE7948",
    "danger": "#E06C75",
    "warning": "#EC5B2B",
    "success": "#6BA1E6"
  },
  "mercury": {
    "title": "Mercury",
    "bg": "#171721",
    "surface": "#10101A",
    "raised": "#272735",
    "text": "#DDDDE5",
    "muted": "#9D9DA8",
    "accent": "#8DA4F5",
    "accent2": "#A7B6F8",
    "danger": "#FC92B4",
    "warning": "#FC9B6F",
    "success": "#77C599"
  },
  "nightowl": {
    "title": "Night Owl",
    "bg": "#011627",
    "surface": "#0B253A",
    "raised": "#0B253A",
    "text": "#D6DEEB",
    "muted": "#5F7E97",
    "accent": "#82AAFF",
    "accent2": "#7FDBCA",
    "danger": "#EF5350",
    "warning": "#ECC48D",
    "success": "#C5E478"
  },
  "orng": {
    "title": "Orange",
    "bg": "#0A0A0A",
    "surface": "#141414",
    "raised": "#1E1E1E",
    "text": "#EEEEEE",
    "muted": "#808080",
    "accent": "#EC5B2B",
    "accent2": "#EE7948",
    "danger": "#E06C75",
    "warning": "#EC5B2B",
    "success": "#6BA1E6"
  },
  "palenight": {
    "title": "Palenight",
    "bg": "#292D3E",
    "surface": "#1E2132",
    "raised": "#32364A",
    "text": "#A6ACCD",
    "muted": "#676E95",
    "accent": "#82AAFF",
    "accent2": "#C792EA",
    "danger": "#F07178",
    "warning": "#FFCB6B",
    "success": "#C3E88D"
  },
  "vercel": {
    "title": "Vercel",
    "bg": "#000000",
    "surface": "#1A1A1A",
    "raised": "#292929",
    "text": "#EDEDED",
    "muted": "#878787",
    "accent": "#0070F3",
    "accent2": "#52A8FF",
    "danger": "#E5484D",
    "warning": "#FFB224",
    "success": "#46A758"
  },
  "orange": {
    "title": "Classic Orange",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#FAB283",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "green": {
    "title": "Forest Green",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#7FD88F",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "blue": {
    "title": "Deep Blue",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#5C9CF5",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "purple": {
    "title": "Cyberpunk Purple",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#9D7CD8",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "monochrome": {
    "title": "Monochrome",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#EEEEEE",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "pastel": {
    "title": "Pastel Blush Pink",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#F5A9C0",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "mint": {
    "title": "Pastel Mint",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#98E4C8",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "lavender": {
    "title": "Pastel Lavender",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#C5B4E3",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "peach": {
    "title": "Pastel Peach",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#F5C6A0",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "sky": {
    "title": "Pastel Sky",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#A8D8F0",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "rose": {
    "title": "Pastel Rose",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#E8A0B0",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "butter": {
    "title": "Pastel Butter",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#F0E0A0",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "coral": {
    "title": "Pastel Coral",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#F08070",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "lilac": {
    "title": "Pastel Lilac",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#C8A8E0",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "sage": {
    "title": "Pastel Sage",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#A8C8A0",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  },
  "retro": {
    "title": "Retro Terminal",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#00FF66",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
  }
};

let slashMenuEl = null;
let slashActiveIdx = -1;

// Helper to extract session ID from window.location.hash
function getSessionIdFromHash() {
  const hash = window.location.hash;
  if (hash && hash.startsWith('#/session/')) {
    const parts = hash.split('/');
    if (parts.length >= 3) {
      return parts[2];
    }
  }
  return null;
}

async function handleHashChange() {
  const hash = window.location.hash;
  
  const sid = getSessionIdFromHash();
  if (sid) {
    if (sid !== state.sessionId) {
      let session = state.openSessions.find(s => s.id === sid);
      if (session) {
        switchSession(sid);
      } else {
        session = await loadSessionData(sid);
        if (session && (session.title || session.messages.length > 0 || session.id)) {
          state.openSessions.push(session);
          switchSession(sid);
        } else {
          window.location.hash = '';
        }
      }
    }
  }
}

// ── Init ──
async function init() {
  try {
    // Only an explicit choice (qcode-theme) is kept; otherwise follow the OS setting.
    const savedTheme = localStorage.getItem('qcode-theme');
    applyTheme(savedTheme && THEMES[savedTheme] ? savedTheme : systemThemeName(), { persist: false });
  } catch (_) {}
  restorePrefs();
  try {
    window.matchMedia('(prefers-color-scheme: light)').addEventListener('change', () => {
      if (!localStorage.getItem('qcode-theme')) applyTheme(systemThemeName(), { persist: false });
    });
  } catch (_) {}
  if (state.reasoning && reasoningSelect) reasoningSelect.value = state.reasoning;
  if (state.provider && providerSelect) providerSelect.value = state.provider;
  // Startup requests run together rather than one round trip after another.
  // Each keeps its own failure handling; the rejection is observed where it is awaited.
  const versionReq = fetch("/api/version");
  versionReq.catch(() => {});
  const sessionsReq = fetch('/sessions');
  sessionsReq.catch(() => {});
  const providersReady = loadProviders();
  try {
    const verRes = await versionReq;
    if (verRes.ok) {
      const verData = await verRes.json();
      const sub = document.getElementById("brand-sub");
      if (sub && verData.version) {
        sub.textContent = "v" + verData.version;
        sub.title = (verData.name || "qcode-server") + " v" + verData.version;
      }
    }
  } catch (_) {}

  setupViewportHandler();
  await providersReady;
  setupEventListeners();
  restoreSidebarState();
  restoreViewsCollapseState();
  restoreSessionsCollapseState();
  restoreFsTreeState();
  updateSettingsModalModelInfo();
  window.addEventListener('resize', () => {
    const sidebar = document.getElementById('sidebar');
    const overlay = document.getElementById('sidebar-overlay');
    if (!isMobileLayout()) {
      if (sidebar) sidebar.classList.remove('active');
      if (overlay) overlay.classList.remove('active');
      restoreSidebarState();
    } else {
      if (sidebar) sidebar.classList.remove('collapsed');
    }
  });
  updateStatusBar();
  
  try {
    const sessionsRes = await sessionsReq;
    if (sessionsRes.ok) {
      const sessions = await sessionsRes.json();
      if (Array.isArray(sessions) && sessions.length > 0) {
        let lastId = getSessionIdFromHash();
        if (!lastId) {
          const lastRes = await fetch('/session/last?messages=0');
          if (lastRes.ok) {
            const lastData = await lastRes.json();
            if (lastData && lastData.id) {
              lastId = lastData.id;
            }
          }
        }

        if (!lastId || !sessions.some(s => s.id === lastId)) {
          lastId = sessions[0].id;
        }

        // Sidebar entries come from the list rows; only the active session loads history.
        for (const s of sessions) {
          state.openSessions.push(makeSessionStub(s));
        }
        renderSessionTabs();

        switchSession(lastId);
        return;
      }
    }
  } catch (e) {
    console.error("Failed to load sessions from SQLite:", e);
  }

  await createNewSession();
}
init();

async function loadProviders() {
  try {
    const res = await fetch('/providers');
    state.providers = await res.json();
    if (state.providers.length === 0) {
      showToast('No providers configured on server');
      return;
    }
    if (providerSelect) {
      providerSelect.innerHTML = state.providers.map((p, i) =>
        `<option value="${i}">${p.name}</option>`
      ).join('');
      providerSelect.addEventListener('change', () => {
        onProviderChange(parseInt(providerSelect.value));
      });
    }
    onProviderChange(0);
  } catch (e) {
    showToast('Failed to load providers: ' + e.message);
  }
}

function formatModelDropdownLabel(m) {
  const parts = [m.name];
  if (m.context_window > 0) parts.push(formatContextTokens(m.context_window));
  if (m.is_free) parts.push('free');
  return parts.join(' · ');
}

function onProviderChange(idx) {
  const p = state.providers[idx];
  if (!p) return;
  state.provider = p.id;
  if (p.models && p.models.length > 0 && !p.models.some(m => m.id === state.model)) {
    state.model = p.models[0].id;
  }
  updateSettingsModalModelInfo();
}

// Resolve a provider/model pair against the loaded provider list.
// Falls back to the first configured provider/model when the stored
// selection is empty or stale (common for older sessions).
function resolveProviderModel(providerId, modelId) {
  if (!state.providers.length) {
    return { provider: providerId || '', model: modelId || '', ok: false };
  }
  let pidx = state.providers.findIndex(p => p.id === providerId || p.name === providerId);
  if (pidx < 0) pidx = 0;
  const p = state.providers[pidx];
  let midx = p.models.findIndex(m => m.id === modelId || m.name === modelId);
  if (midx < 0) midx = 0;
  const model = p.models[midx] ? p.models[midx].id : (modelId || '');
  return { provider: p.id, model, ok: !!p.id && !!model };
}

// Apply a provider/model selection to both the dropdowns and state.
// Used by the model picker and when restoring a session.
function applyProviderModel(providerId, modelId) {
  const resolved = resolveProviderModel(providerId, modelId);
  if (!resolved.ok) return false;
  const pidx = state.providers.findIndex(p => p.id === resolved.provider);
  if (pidx < 0) return false;
  if (providerSelect) providerSelect.value = pidx;
  state.provider = resolved.provider;
  state.model = resolved.model;
  updateSettingsModalModelInfo();
  return resolved.provider === providerId ||
    state.providers[pidx].name === providerId;
}

function setupViewportHandler() {
  let lastH = 0;
  const update = () => {
    let h = window.innerHeight;
    if (window.visualViewport) {
      h = Math.round(window.visualViewport.height);
    }
    if (h !== lastH) {
      lastH = h;
      document.documentElement.style.setProperty('--app-height', `${h}px`);
    }
    if (window.scrollY !== 0 || window.scrollX !== 0) {
      window.scrollTo(0, 0);
    }
    if (document.documentElement.scrollTop !== 0) {
      document.documentElement.scrollTop = 0;
    }
    if (document.body.scrollTop !== 0) {
      document.body.scrollTop = 0;
    }
  };
  if (window.visualViewport) {
    window.visualViewport.addEventListener('resize', () => {
      update();
      if (document.activeElement === promptInput) {
        scrollToBottom(true);
      }
    });
    window.visualViewport.addEventListener('scroll', () => {
      if (window.scrollY !== 0 || window.scrollX !== 0) {
        window.scrollTo(0, 0);
      }
      if (document.documentElement.scrollTop !== 0) {
        document.documentElement.scrollTop = 0;
      }
      if (document.body.scrollTop !== 0) {
        document.body.scrollTop = 0;
      }
    });
  }
  window.addEventListener('resize', update);
  window.addEventListener('orientationchange', () => {
    setTimeout(update, 100);
    setTimeout(update, 300);
  });
  update();
}

function setupEventListeners() {

  sendBtn.addEventListener('click', sendMessage);
  promptInput.addEventListener('keydown', handleInputKeydown);
  promptInput.addEventListener('focus', () => {
    const keepInView = () => {
      if (window.scrollY !== 0) window.scrollTo(0, 0);
      if (document.documentElement.scrollTop !== 0) document.documentElement.scrollTop = 0;
      if (document.body.scrollTop !== 0) document.body.scrollTop = 0;
      scrollToBottom(true);
    };
    keepInView();
    setTimeout(keepInView, 80);
    setTimeout(keepInView, 200);
    setTimeout(keepInView, 350);
  });
  promptInput.addEventListener('blur', () => {
    setTimeout(() => {
      if (window.scrollY !== 0) window.scrollTo(0, 0);
      if (window.visualViewport) {
        document.documentElement.style.setProperty('--app-height', `${Math.round(window.visualViewport.height)}px`);
      }
    }, 100);
  });
  if (settingsClearChatBtn) {
    settingsClearChatBtn.addEventListener('click', async () => {
      if (!state.sessionId) { showToast('No active session'); return; }
      if (!confirm('Clear all messages in this session? This cannot be undone.')) return;
      try {
        const res = await fetch('/session/' + state.sessionId + '/clear', {
          method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}'
        });
        if (!res.ok) { showToast('Could not clear chat: ' + await responseError(res), 'error'); return; }
      } catch (e) {
        showToast('Could not clear chat: ' + e.message, 'error');
        return;
      }
      const session = state.openSessions.find(s => s.id === state.sessionId);
      if (session) { session.messages = []; }
      renderMessages();
      closeSettingsModal();
      showToast('Chat cleared');
    });
  }
  if (themeSelect) {
    themeSelect.value = state.theme || 'qcode';
    themeSelect.addEventListener('change', () => {
      const chosen = themeSelect.value;
      if (THEMES[chosen]) {
        applyTheme(chosen);
        showToast('Theme: ' + THEMES[chosen].title);
      }
    });
  }
  reasoningSelect.addEventListener('change', () => {
    state.reasoning = reasoningSelect.value;
    persistPrefs();
  });
  if (providerSelect) {
    providerSelect.addEventListener('change', () => {
      onProviderChange(parseInt(providerSelect.value, 10));
      const session = state.openSessions.find(s => s.id === state.sessionId);
      if (session) {
        session.provider = state.provider;
        session.model = state.model;
      }
    });
  }
  promptInput.addEventListener('input', () => {
    updateSlashMenu();
    resizePromptInput();
    persistDraft();
    if (chatStick) {
      scrollToBottom();
    }
  });
    const fsFilter = document.getElementById('fs-filter-input');
  if (fsFilter) fsFilter.addEventListener('input', () => { state.fsFilterText = fsFilter.value; renderFsListingEntries(); });
  const sessFilter = document.getElementById('sessions-filter-input');
  if (sessFilter) sessFilter.addEventListener('input', () => { state.sessionsFilterText = sessFilter.value; renderSessionTabs(); });
  const chatC = document.getElementById('chat-container');
  if (chatC) {
    chatC.addEventListener('scroll', onChatScroll, { passive: true });
    const unstick = () => { chatStick = false; };
    chatC.addEventListener('wheel', (e) => { if (e.deltaY < 0) unstick(); }, { passive: true });
    chatC.addEventListener('touchmove', unstick, { passive: true });
    // Scrollbar drags; selection drags are tracked by chatSelecting.
    chatC.addEventListener('pointerdown', (e) => {
      if (e.target === chatC && e.offsetX >= chatC.clientWidth) chatPointerHeld = true;
    });
    window.addEventListener('pointerup', () => { chatPointerHeld = false; });
    window.addEventListener('pointercancel', () => { chatPointerHeld = false; });
    document.addEventListener('keydown', (e) => {
      if (/^(PageUp|ArrowUp|Home)$/.test(e.key)
          && (e.target === document.body || chatC.contains(e.target))) unstick();
    });
  }
  messagesEl.addEventListener('pointerdown', (e) => {
    if (e.button === 0 && e.pointerType === 'mouse' && !e.target.closest('button, a, input, textarea, select, [role="button"]')) chatSelecting = true;
  });
  window.addEventListener('pointerup', () => { if (chatSelecting) flushDeferredRenders(); });
  window.addEventListener('pointercancel', () => { if (chatSelecting) flushDeferredRenders(); });
  window.addEventListener('blur', () => { if (chatSelecting) flushDeferredRenders(); });
  document.addEventListener('click', (e) => {
    if (slashMenuEl && !slashMenuEl.contains(e.target) && e.target !== promptInput) {
      closeSlashMenu();
    }
  });
  document.addEventListener('click', onMarkdownLinkClick);
  if (newSessionBtn) {
    newSessionBtn.addEventListener('click', showNewSessionModal);
  }
  

  if (pauseBtn) {
    pauseBtn.addEventListener('click', () => pauseActiveGeneration());
  }
  
  // Tab and layout event listeners
  if (tabChatBtn) {
    tabChatBtn.addEventListener('click', () => switchTab('chat'));
  }
  if (tabFilesBtn) {
    tabFilesBtn.addEventListener('click', () => switchTab('files'));
  }
  if (headerStatsBtn) {
    headerStatsBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      switchTab(state.activeTab === 'stats' ? 'chat' : 'stats');
    });
  }
  if (tabSessionsBtn) {
    tabSessionsBtn.addEventListener('click', () => switchTab('sessions'));
  }
  if (sessionsToggleBtn) {
    sessionsToggleBtn.addEventListener('click', (e) => {
      if (e.target.closest('#new-session-btn')) return;
      const section = document.getElementById('sidebar-sessions-section');
      if (!section) return;
      const isCollapsed = section.classList.toggle('collapsed');
      sessionsToggleBtn.setAttribute('aria-expanded', String(!isCollapsed));
      try { localStorage.setItem('qcode-sessions-collapsed', String(isCollapsed)); } catch (_) {}
    });
    sessionsToggleBtn.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ') {
        if (e.target.closest('#new-session-btn')) return;
        e.preventDefault();
        sessionsToggleBtn.click();
      }
    });
  }
  if (sessionsRefreshBtn) {
    sessionsRefreshBtn.addEventListener('click', () => loadDelegatedSessionsTab());
  }
  const sessionsFilterInput = document.getElementById('sessions-filter-input');
  if (sessionsFilterInput) {
    sessionsFilterInput.addEventListener('input', (e) => {
      state.sessionsFilterText = e.target.value.toLowerCase().trim();
      loadDelegatedSessionsTab();
    });
  }
  if (parentBackBtn) {
    parentBackBtn.addEventListener('click', () => returnToParentSession());
  }
  if (thinkingToggleBtn) {
    thinkingToggleBtn.addEventListener('click', () => toggleThinking());
  }
  const themeToggleBtn = document.getElementById('theme-toggle-btn');
  if (themeToggleBtn) {
    syncThemeToggle();
    themeToggleBtn.addEventListener('click', () => {
      // Manual choice: flip between the dark and light QCode themes and save it.
      const next = THEMES[state.theme] && THEMES[state.theme].scheme === 'light' ? 'qcode' : 'qcode-light';
      applyTheme(next);
      showToast('Theme: ' + THEMES[next].title);
    });
  }
  syncThinkingToggleBtn();
  if (retryBtn) {
    retryBtn.addEventListener('click', () => handleRetryCommand());
  }
  if (filesRefreshBtn) {
    filesRefreshBtn.addEventListener('click', () => loadFilesTab());
  }
  if (filesSubtabExplorer) {
    filesSubtabExplorer.addEventListener('click', () => switchFilesSubtab('explorer'));
  }
  if (filesSubtabGit) {
    filesSubtabGit.addEventListener('click', () => switchFilesSubtab('git'));
  }
  if (fsSaveBtn) {
    fsSaveBtn.addEventListener('click', () => saveOpenFile());
  }
  if (fsCloseBtn) {
    fsCloseBtn.addEventListener('click', () => closeOpenFile());
  }
  if (fsFilterInput) {
    fsFilterInput.addEventListener('input', (e) => {
      state.fsFilterText = e.target.value;
      renderFsListingEntries();
    });
  }
  if (fsPlaceholderTreeBtn) {
    fsPlaceholderTreeBtn.addEventListener("click", () => toggleFsTree());
  }
  if (fsBackBtn) {
    fsBackBtn.addEventListener('click', () => {
      if (window.innerWidth <= 768) {
        setFsMobileView('browser');
      } else {
        toggleFsTree();
      }
    });
  }
  if (fsCopyPathBtn) {
    fsCopyPathBtn.addEventListener('click', () => {
      if (!state.fsOpenPath) return;
      navigator.clipboard.writeText(state.fsOpenPath);
      showToast('Copied path: ' + state.fsOpenPath);
    });
  }
  if (fsCopyContentBtn) {
    fsCopyContentBtn.addEventListener('click', () => {
      const text = (fsEditor && (state.fsViewMode === 'editor' || state.fsDirty)) ? fsEditor.value : state.fsSavedContent;
      if (!text) return;
      navigator.clipboard.writeText(text);
      showToast('Copied file content');
    });
  }
  if (fsMdPreviewBtn) {
    fsMdPreviewBtn.addEventListener('click', () => {
      state.fsMdMode = 'preview';
      if (fsMdPreviewBtn) fsMdPreviewBtn.classList.add('active');
      if (fsMdCodeBtn) fsMdCodeBtn.classList.remove('active');
      if (state.fsOpenPath) {
        const content = (fsEditor && state.fsViewMode === 'editor') ? fsEditor.value : state.fsSavedContent;
        updateMarkdownViewer(content);
      }
    });
  }
  if (fsMdCodeBtn) {
    fsMdCodeBtn.addEventListener('click', () => {
      state.fsMdMode = 'code';
      if (fsMdCodeBtn) fsMdCodeBtn.classList.add('active');
      if (fsMdPreviewBtn) fsMdPreviewBtn.classList.remove('active');
      if (state.fsOpenPath) {
        const content = (fsEditor && state.fsViewMode === 'editor') ? fsEditor.value : state.fsSavedContent;
        updateMarkdownViewer(content);
      }
    });
  }
  if (fsWrapBtn) {
    fsWrapBtn.addEventListener('click', () => {
      state.fsLineWrap = !state.fsLineWrap;
      fsWrapBtn.classList.toggle('active', state.fsLineWrap);
      if (fsViewCode) fsViewCode.classList.toggle('wrap-lines', state.fsLineWrap);
      if (fsCodeWrapper) fsCodeWrapper.classList.toggle('wrap-lines', state.fsLineWrap);
      if (fsViewEditor) fsViewEditor.classList.toggle('wrap-lines', state.fsLineWrap);
      if (fsEditor) fsEditor.wrap = state.fsLineWrap ? 'on' : 'off';
    });
  }
  if (fsEditBtn) {
    fsEditBtn.addEventListener('click', () => toggleFsEditMode());
  }
  if (fsCodeWrapper && fsLineNumbers) {
    fsCodeWrapper.addEventListener('scroll', () => {
      fsLineNumbers.scrollTop = fsCodeWrapper.scrollTop;
    });
  }
  if (fsEditor) {
    fsEditor.addEventListener('input', () => {
      if (!state.fsOpenPath) return;
      const dirty = fsEditor.value !== state.fsSavedContent;
      setFsDirty(dirty);
      updateFsHighlight();
      syncFsHighlightScroll();
      if (fsEditorStatus) {
        const lineCount = (fsEditor.value.split('\n')).length;
        const szStr = formatBytes(fsEditor.value.length);
        fsEditorStatus.textContent = (dirty ? 'Editing • ' : '') + szStr + ' · ' + lineCount + ' lines · ' + (fsLang || 'text');
      }
    });
    fsEditor.addEventListener('scroll', syncFsHighlightScroll);
    fsEditor.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 's') {
        e.preventDefault();
        saveOpenFile();
        return;
      }
      // Insert tab as spaces (keeps caret/highlight in sync).
      if (e.key === 'Tab' && !e.ctrlKey && !e.metaKey && !e.altKey && state.fsOpenPath) {
        e.preventDefault();
        const start = fsEditor.selectionStart;
        const end = fsEditor.selectionEnd;
        const val = fsEditor.value;
        const insert = '  ';
        fsEditor.value = val.slice(0, start) + insert + val.slice(end);
        fsEditor.selectionStart = fsEditor.selectionEnd = start + insert.length;
        fsEditor.dispatchEvent(new Event('input'));
      }
    });
  }
  if (statsRefreshBtn) {
    statsRefreshBtn.addEventListener('click', () => loadStatsTab());
  }
  if (statsCopyBtn) {
    statsCopyBtn.addEventListener('click', copyStatsSummary);
  }

  // Sidebar event listeners
  const menuToggleBtn = document.getElementById('menu-toggle-btn');
  if (menuToggleBtn) {
    menuToggleBtn.addEventListener('click', toggleSidebar);
  }
  const sidebarCloseBtn = document.getElementById('sidebar-close-btn');
  if (sidebarCloseBtn) {
    sidebarCloseBtn.addEventListener('click', closeSidebar);
  }
  const sidebarOverlay = document.getElementById('sidebar-overlay');
  if (sidebarOverlay) {
    sidebarOverlay.addEventListener('click', closeMobileSidebar);
  }
  if (viewsToggleBtn) {
    viewsToggleBtn.addEventListener('click', () => {
      const section = document.getElementById('sidebar-views-section');
      if (!section) return;
      const isCollapsed = section.classList.toggle('collapsed');
      viewsToggleBtn.setAttribute('aria-expanded', String(!isCollapsed));
      try { localStorage.setItem('qcode-views-collapsed', String(isCollapsed)); } catch (_) {}
    });
    viewsToggleBtn.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        viewsToggleBtn.click();
      }
    });
  }
  if (settingsBtn) {
    settingsBtn.addEventListener('click', openSettingsModal);
  }
  if (settingsModalClose) {
    settingsModalClose.addEventListener('click', closeSettingsModal);
  }
  if (settingsModal) {
    settingsModal.addEventListener('click', (e) => {
      if (e.target === settingsModal) closeSettingsModal();
    });
  }
  if (settingsSwitchModelBtn) {
    settingsSwitchModelBtn.addEventListener('click', () => {
      closeSettingsModal();
      handleModelCommand('');
    });
  }
  const headerSessionInfo = document.querySelector('.header-session-info');
  if (headerSessionInfo) {
    headerSessionInfo.addEventListener('click', () => {
      if (isMobileLayout()) {
        toggleSidebar();
      }
    });
  }
  const fsCollapseBtn = document.getElementById('fs-collapse-btn');
  if (fsCollapseBtn) {
    fsCollapseBtn.addEventListener('click', toggleFsTree);
  }

  // Escape key pauses/cancels the active generation from anywhere
  // (document level catches input textarea, FS editor, and unfocused UI).
  document.addEventListener('keydown', (e) => {
    const key = e.key;
    const mod = e.ctrlKey || e.metaKey;
    // "?" opens the shortcut help, but only when the user is not typing into a field.
    if (key === '?' && !mod && !e.altKey && !isTypingTarget(e.target) && !isModalOpen()) {
      e.preventDefault();
      handleHelpCommand();
      return;
    }
    if (mod && key.toLowerCase() === 'p') {
      e.preventDefault();
      showCommandPalette();
      return;
    }
    if (mod && key.toLowerCase() === 'n') {
      e.preventDefault();
      showNewSessionModal();
      return;
    }
    if (mod && (key === 'b' || key === 'B')) {
      e.preventDefault();
      toggleSidebar();
      return;
    }
    if (e.altKey && !mod && (key === 'f' || key === 'F')) {
      e.preventDefault();
      toggleFsTree();
      return;
    }
    if (key === 'F2') {
      e.preventDefault();
      toggleThinking();
      return;
    }
    if (e.altKey && !mod && (key === '1' || key === '2' || key === '3' || key === '4')) {
      e.preventDefault();
      if (key === '1') switchTab('chat');
      else if (key === '2') { switchTab('files'); switchFilesSubtab('git'); }
      else if (key === '3') switchTab('stats');
      else switchTab('sessions');
      return;
    }
    if ((key === 'b' || key === 'B') && !mod && !e.altKey) {
      const tag = (e.target && e.target.tagName) ? e.target.tagName.toLowerCase() : '';
      if (tag !== 'input' && tag !== 'textarea' && !e.target.isContentEditable) {
        if (state.agentMode === 'subagent' || state.parentSessionId) {
          e.preventDefault();
          returnToParentSession();
          return;
        }
      }
    }
    if (key === 'Escape' && e.type === 'keydown') {
      if (isModalOpen()) return;
      const activeSession = state.openSessions.find(s => s.id === state.sessionId);
      if (activeSession && activeSession.generating) {
        pauseActiveGeneration();
      } else if (state.agentMode === 'subagent' || state.parentSessionId) {
        returnToParentSession();
      }
    }
  });

  window.addEventListener('hashchange', handleHashChange);
  window.addEventListener('online', updateConnectionStatus);
  window.addEventListener('offline', updateConnectionStatus);
  updateConnectionStatus();
  setInterval(updateConnectionStatus, 30000);
  // Event-driven session refresh (see refreshSession): visibility, focus and
  // reconnect trigger a cheap check; a light poll covers changes made
  // elsewhere. Full history is fetched only when the session changed.
  document.addEventListener('visibilitychange', refreshActiveSession);
  window.addEventListener('focus', refreshActiveSession);
  window.addEventListener('online', refreshActiveSession);
  setInterval(refreshActiveSession, 5000);
}

// ── Status Bar ──
function updateStatusBar() {
  const cleanTitle = stripTags(state.sessionTitle);
  statusSession.textContent = cleanTitle || (state.sessionId ? 'Session: ' + state.sessionId.substring(0,8) + '…' : 'No session');
  if (statusSessionIndicator) {
    const activeSession = state.openSessions.find(s => s.id === state.sessionId);
    if (activeSession && activeSession.generating) {
      statusSessionIndicator.className = 'header-session-status running';
      statusSessionIndicator.innerHTML = '<span class="session-active-dot"></span>';
      statusSessionIndicator.title = 'Session running: generating…';
      statusSessionIndicator.style.display = 'inline-flex';
    } else {
      statusSessionIndicator.className = 'header-session-status idle';
      statusSessionIndicator.innerHTML = '';
      statusSessionIndicator.title = '';
      statusSessionIndicator.style.display = 'none';
    }
  }
  if (statusWorkspace) {
    if (state.sessionWorkspace) {
      const cleanWs = stripTags(state.sessionWorkspace);
      statusWorkspace.innerHTML = `<span class="ws-icon">${SVG_ICONS.folder}</span><span class="ws-path">${esc(shortPath(cleanWs))}</span>`;
      statusWorkspace.title = cleanWs;
    } else {
      statusWorkspace.innerHTML = '';
      statusWorkspace.title = '';
    }
  }
  document.title = (cleanTitle ? cleanTitle + ' · ' : '') + 'QCode';
  const isSub = state.agentMode === 'subagent';
  if (subagentBadge) subagentBadge.classList.toggle('hidden', !isSub);
  if (parentBackBtn) {
    parentBackBtn.classList.toggle('hidden', !isSub && !state.parentSessionId);
    parentBackBtn.title = 'Return to parent session (Esc or b)';
  }
  if (agentModeBtn) {
    agentModeBtn.classList.toggle('hidden', isSub);
    if (!isSub) {
      agentModeBtn.textContent = formatAgentModeTitle(state.agentMode);
      agentModeBtn.classList.remove('subagent');
      agentModeBtn.classList.toggle('orchestrator', normalizeAgentMode(state.agentMode) === 'orchestrator');
      agentModeBtn.title = `Agent Mode: ${formatAgentModeTitle(state.agentMode)} (Click to switch)`;
    }
  }
  syncThinkingToggleBtn();
}

// The 30 s health check is the happy-path heartbeat. While the server is
// unreachable the check retries with backoff so a restart is noticed within
// seconds; each outage shows one toast, and recovery resyncs open sessions.
const kHealthTimeoutMs = 4000;
let connOnline = true;
let connFailures = 0;
let connTimer = null;
let connProbe = null;

function fetchWithTimeout(url, opts, ms) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), ms);
  return fetch(url, { ...(opts || {}), signal: ctrl.signal }).finally(() => clearTimeout(timer));
}

function updateConnectionStatus() {
  if (!connProbe) connProbe = probeConnection().finally(() => { connProbe = null; });
  return connProbe;
}

async function probeConnection() {
  let online = navigator.onLine;
  if (online) {
    try {
      const res = await fetchWithTimeout('/api/health', { cache: 'no-store' }, kHealthTimeoutMs);
      online = res.ok;
    } catch (_) {
      online = false;
    }
  }
  setConnectionOnline(online);
}

function setConnectionOnline(online) {
  const was = connOnline;
  connOnline = online;
  clearTimeout(connTimer);
  connTimer = null;
  connFailures = online ? 0 : connFailures + 1;
  const status = document.getElementById('connection-status');
  if (status) {
    status.classList.toggle('offline', !online);
    const label = status.querySelector('.connection-label');
    if (label) label.textContent = online ? 'Connected' : 'Offline';
  }
  if (!online) {
    connTimer = setTimeout(updateConnectionStatus, healthRetryDelayMs(connFailures));
    if (was) showToast('Connection lost — retrying', 'warning');
  } else if (!was) {
    showToast('Connection restored', 'success');
    // Catch up on whatever changed while the server was away.
    for (const s of state.openSessions) refreshSession(s);
  }
}

// ═══════════════════════════════════════════════════════════════════
//  NEW SESSION MODAL
// ═══════════════════════════════════════════════════════════════════

// New Session dialog. The folder browser is the only workspace picker: it lists
// the child folders of the directory in the box. Tap a folder to select it and
// list its children, Up goes to the parent, and Create uses the folder shown.
function showNewSessionModal() {
  const defaultWs = state.sessionWorkspace || '';
  modalOverlay.innerHTML = `
    <div class="picker-modal new-session-modal">
      <div class="picker-header">New Session</div>
      <div style="font-size:12px;color:var(--text-muted, #94a3b8);margin-bottom:4px;">Session Title</div>
      <input class="rename-input" id="ns-title" type="text" placeholder="Session title (optional)" style="margin-bottom:10px;" />
      <div style="font-size:12px;color:var(--text-muted, #94a3b8);margin-bottom:4px;">Workspace Directory</div>
      <input class="rename-input" id="ns-workspace" type="text" placeholder="Type a path, e.g. ~/notes" value="${esc(defaultWs)}" autocomplete="off" />
      <div class="fb-browser" id="ns-browser" style="display:none;">
        <div class="fb-head">
          <button class="fb-up" id="ns-fb-up" type="button" title="Parent folder" aria-label="Parent folder">&uarr; Up</button>
          <span class="fb-path" id="ns-fb-path"></span>
        </div>
        <div class="fb-error" id="ns-fb-error" style="display:none;"></div>
        <div class="fb-list" id="ns-fb-list" aria-label="Subfolders"></div>
      </div>
      <div class="modal-actions" style="margin-top:12px;">
        <button class="modal-btn secondary" id="ns-cancel">Cancel</button>
        <button class="modal-btn primary" id="ns-create">Create</button>
      </div>
    </div>`;
  modalOverlay.classList.add('active');

  const titleInput = document.getElementById('ns-title');
  const wsInput = document.getElementById('ns-workspace');
  const fbBox = document.getElementById('ns-browser');
  const fbUp = document.getElementById('ns-fb-up');
  const fbPath = document.getElementById('ns-fb-path');
  const fbError = document.getElementById('ns-fb-error');
  const fbList = document.getElementById('ns-fb-list');
  let fbSeq = 0;
  let fbCurrent = null;
  let fbAvailable = true;

  function fbShowError(msg) {
    fbError.textContent = msg || '';
    fbError.style.display = msg ? 'block' : 'none';
  }

  function fbRender(data) {
    fbCurrent = data;
    fbPath.textContent = data.path;
    fbPath.title = data.path;
    fbUp.disabled = !data.parent;
    const rows = (data.dirs || []).map(d => `
      <button type="button" class="fb-item" data-path="${esc(d.path)}" title="${esc(d.path)}">
        <span class="ws-suggest-icon">${SVG_ICONS.folder}</span>
        <span class="fb-name">${esc(d.name)}</span>
        <span class="fb-chevron" aria-hidden="true">&rsaquo;</span>
      </button>`).join('');
    const note = data.truncated ? '<div class="fb-empty">Showing the first 1000 folders</div>' : '';
    fbList.innerHTML = rows ? rows + note : '<div class="fb-empty">No subfolders</div>';
  }

  // setBox: true when the user picked a folder (the box follows the listing).
  // false when browsing from typed text, so the box is not rewritten mid-typing.
  // silent: suppress error messages (used while typing partial paths).
  async function fbBrowse(path, { setBox = true, silent = false } = {}) {
    const seq = ++fbSeq;
    let res;
    let data = null;
    try {
      res = await fetch('/api/dirs' + (path ? '?path=' + encodeURIComponent(path) : ''));
      try { data = await res.json(); } catch (_) { data = null; }
    } catch (_) {
      if (!silent && seq === fbSeq) fbShowError('Cannot reach the server');
      return false;
    }
    if (seq !== fbSeq) return false;
    if (!data || typeof data !== 'object') {
      // Route not available on this server build: hide the browser, keep typing.
      fbAvailable = false;
      fbBox.style.display = 'none';
      return false;
    }
    if (!res.ok) {
      if (!silent) fbShowError(data.error || 'Cannot open this folder');
      return false;
    }
    fbShowError('');
    fbBox.style.display = 'flex';
    fbRender(data);
    if (setBox) wsInput.value = data.path;
    return true;
  }

  // Typed absolute or ~ paths drive the listing (debounced).
  let browseTimer = null;
  function followBox() {
    const v = wsInput.value.trim();
    if (!fbAvailable) return;
    if (v.startsWith('/') || v.startsWith('~')) {
      fbBrowse(v, { setBox: false, silent: true });
    } else if (v) {
      fbShowError('Type a full path starting with / or ~');
    } else {
      fbShowError('');
    }
  }

  fbList.addEventListener('click', (e) => {
    const btn = e.target.closest('.fb-item');
    if (btn && btn.dataset.path) fbBrowse(btn.dataset.path, { setBox: true });
  });
  fbUp.addEventListener('click', () => {
    if (fbCurrent && fbCurrent.parent) fbBrowse(fbCurrent.parent, { setBox: true });
  });

  // Open on the folder already in the box (or the server default when empty).
  // If that folder cannot be opened, fall back to the server default.
  fbBrowse(wsInput.value.trim(), { setBox: true }).then((ok) => {
    if (!ok && fbAvailable) fbBrowse('', { setBox: true });
  });

  wsInput.addEventListener('input', () => {
    clearTimeout(browseTimer);
    browseTimer = setTimeout(followBox, 250);
  });

  function close() { closeModal(); }
  document.getElementById('ns-cancel').addEventListener('click', close);

  async function doCreate() {
    const title = titleInput.value.trim();
    const workspace = wsInput.value.trim();
    await createNewSession(title, workspace);
    close();
  }

  document.getElementById('ns-create').addEventListener('click', doCreate);
  wsInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); doCreate(); }
    if (e.key === 'Escape') { e.preventDefault(); close(); }
    e.stopPropagation();
  });
  titleInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); wsInput.focus(); }
    if (e.key === 'Escape') { e.preventDefault(); close(); }
    e.stopPropagation();
  });
  titleInput.focus();
}

function switchTab(tab) {
  state.activeTab = tab;
  state.layoutMode = 'tab';
  persistPrefs();
  if (tab === 'files') switchFilesSubtab(state.filesSubtab || 'explorer');
  if (tab === 'stats') loadStatsTab();
  if (tab === 'sessions') loadDelegatedSessionsTab();
  updateLayoutUI();
  updateActiveViewBadge();
  closeMobileSidebar();
}

function updateLayoutUI() {
  state.layoutMode = 'tab';
  if (mainContentWrapperEl) mainContentWrapperEl.classList.remove('split-view');

  // Exclusive tab mode: hide every pane, then show only the active one.
  if (mainEl) mainEl.classList.add('hidden');
  if (filesPanel) filesPanel.classList.add('hidden');
  if (statsPanel) statsPanel.classList.add('hidden');
  if (sessionsPanel) sessionsPanel.classList.add('hidden');
  [tabChatBtn, tabFilesBtn, tabSessionsBtn].forEach(b => {
    if (b) b.classList.remove('active');
  });

  if (headerStatsBtn) {
    if (state.activeTab === 'stats') {
      headerStatsBtn.classList.add('active');
      headerStatsBtn.setAttribute('aria-pressed', 'true');
    } else {
      headerStatsBtn.classList.remove('active');
      headerStatsBtn.setAttribute('aria-pressed', 'false');
    }
  }

  if (state.activeTab === 'chat') {
    if (mainEl) mainEl.classList.remove('hidden');
    if (tabChatBtn) tabChatBtn.classList.add('active');
  } else if (state.activeTab === 'files') {
    if (filesPanel) filesPanel.classList.remove('hidden');
    if (tabFilesBtn) tabFilesBtn.classList.add('active');
  } else if (state.activeTab === 'stats') {
    if (statsPanel) statsPanel.classList.remove('hidden');
  } else if (state.activeTab === 'sessions') {
    if (sessionsPanel) sessionsPanel.classList.remove('hidden');
    if (tabSessionsBtn) tabSessionsBtn.classList.add('active');
  }
  [tabChatBtn, tabFilesBtn, tabSessionsBtn].forEach(b => {
    if (!b) return;
    if (b.classList.contains('active')) b.setAttribute('aria-current', 'page');
    else b.removeAttribute('aria-current');
  });

  renderSessionTabs();
}

// ═══════════════════════════════════════════════════════════════════
//  MODAL / PICKER SYSTEM
// ═══════════════════════════════════════════════════════════════════

function isModalOpen() {
  return modalOverlay.classList.contains('active') || Boolean(settingsModal && !settingsModal.classList.contains('hidden'));
}

// True when keystrokes would go into a text field, so single-key shortcuts must not fire.
function isTypingTarget(el) {
  if (!el || !el.tagName) return false;
  if (el.isContentEditable) return true;
  return el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT';
}

function closeModal() {
  modalOverlay.classList.remove('active');
  modalOverlay.innerHTML = '';
  if (pickerCleanup) { pickerCleanup(); pickerCleanup = null; }
  const back = modalReturnFocus;
  modalReturnFocus = null;
  if (back && back.isConnected && typeof back.focus === 'function') back.focus({ preventScroll: true });
}

// ── Fuzzy finder ──
// Subsequence match with light scoring (consecutive + word-boundary bonuses).




// Command-palette style picker with live fuzzy filtering via a text input.
function showFuzzyPicker(title, items, activeId, onSelect, placeholder, initialQuery) {
  let query = initialQuery || '';
  let filtered = fuzzyFilter(query, items);
  let cursorIdx = Math.max(0, filtered.findIndex(i => i.id === activeId));
  if (cursorIdx < 0) cursorIdx = 0;

  modalOverlay.innerHTML = `
    <div class="picker-modal fuzzy-picker">
      <div class="picker-header">${esc(title)}</div>
      <input class="rename-input fuzzy-input" id="fuzzy-input" type="text" role="combobox" aria-expanded="true" aria-autocomplete="list" aria-controls="fuzzy-body"
             placeholder="${esc(placeholder || 'Type to filter...')}" value="${esc(query)}" autofocus />
      <div class="picker-body" id="fuzzy-body" role="listbox" aria-label="${esc(title)}"></div>
      <div class="picker-footer"><span><kbd>↑</kbd><kbd>↓</kbd> navigate</span><span><kbd>Enter</kbd> select</span><span><kbd>Esc</kbd> close</span></div>
    </div>`;
  modalOverlay.setAttribute('aria-label', title);
  modalOverlay.classList.add('active');
  const input = document.getElementById('fuzzy-input');
  const body = document.getElementById('fuzzy-body');

  function renderBody() {
    filtered = fuzzyFilter(query, items);
    if (cursorIdx >= filtered.length) cursorIdx = Math.max(0, filtered.length - 1);
    if (cursorIdx < 0) cursorIdx = 0;
    if (filtered.length === 0) {
      body.innerHTML = '<div class="picker-empty">' + (query.trim()
        ? 'No matches for “' + esc(query.trim()) + '”'
        : 'Nothing to show') + '</div>';
      return;
    }
    let html = '';
    let lastCat = '';
    for (let i = 0; i < filtered.length; i++) {
      const it = filtered[i];
      if (it.category && it.category !== lastCat) {
        lastCat = it.category;
        html += `<div class="picker-category" role="presentation">${esc(lastCat)}</div>`;
      }
      const cls = i === cursorIdx ? 'picker-item selected' : 'picker-item';
      const marker = it.isActive ? '<span class="active-dot" title="Current"></span>' : '';
      html += `<div class="${cls}" id="fuzzy-opt-${i}" role="option" aria-selected="${i === cursorIdx}" data-idx="${i}">`;
      html += `<span class="marker">${marker}</span>`;
      html += `<span class="item-label">${esc(it.label)}</span>`;
      if (it.sublabel) html += `<span class="item-id">${esc(it.sublabel)}</span>`;
      if (it.workspace) html += `<span class="item-id" title="${esc(it.workspace)}">${esc(shortPath(it.workspace))}</span>`;
      if (it.badges && it.badges.length) {
        html += `<span class="item-badges">` + it.badges.map(b => `<span class="item-badge badge-${esc(b.type)}">${esc(b.text)}</span>`).join('') + `</span>`;
      }
      html += `</div>`;
    }
    body.innerHTML = html;
    const sel = body.querySelector('.picker-item.selected');
    if (sel) sel.scrollIntoView({ block: 'nearest' });
    input.setAttribute('aria-activedescendant', filtered.length ? 'fuzzy-opt-' + cursorIdx : '');
    body.querySelectorAll('.picker-item').forEach(el => {
      el.addEventListener('click', () => { const idx = parseInt(el.dataset.idx); closeModal(); onSelect(filtered[idx]); });
    });
  }

  renderBody();

  input.addEventListener('input', (e) => { query = e.target.value; cursorIdx = 0; renderBody(); });
  input.addEventListener('keydown', (ev) => {
    if (ev.key === 'ArrowDown') { ev.preventDefault(); cursorIdx = Math.min(cursorIdx + 1, Math.max(0, filtered.length - 1)); renderBody(); }
    else if (ev.key === 'ArrowUp') { ev.preventDefault(); cursorIdx = Math.max(cursorIdx - 1, 0); renderBody(); }
    else if (ev.key === 'Enter') { ev.preventDefault(); if (filtered[cursorIdx]) { closeModal(); onSelect(filtered[cursorIdx]); } }
    else if (ev.key === 'Escape') { ev.preventDefault(); ev.stopPropagation(); closeModal(); }
  });
  input.focus();
  input.setSelectionRange(input.value.length, input.value.length);
}



function showRenameModal(targetSessionId, currentTitle) {
  const sessionIdToRename = targetSessionId || state.sessionId;
  const initialTitle = currentTitle !== undefined ? currentTitle : (state.sessionId === sessionIdToRename ? state.sessionTitle : '');

  if (!sessionIdToRename) {
    modalOverlay.innerHTML = `
      <div class="picker-modal">
        <div class="picker-header">Rename Session</div>
        <div class="rename-message">No active session. Send a message first.</div>
        <div class="modal-actions"><button class="modal-btn secondary" id="modal-close-btn">Close</button></div>
      </div>`;
    modalOverlay.classList.add('active');
    document.getElementById('modal-close-btn').addEventListener('click', closeModal);
    return;
  }
  modalOverlay.innerHTML = `
    <div class="picker-modal">
      <div class="picker-header">Rename Session</div>
      <input class="rename-input" id="rename-input" type="text" placeholder="Enter new title..." value="${esc(initialTitle)}" autofocus />
      <div class="modal-actions">
        <button class="modal-btn secondary" id="rename-cancel-btn">Cancel</button>
        <button class="modal-btn primary" id="rename-ok-btn">Rename</button>
      </div>
    </div>`;
  modalOverlay.classList.add('active');
  const input = document.getElementById('rename-input');
  input.focus();
  input.select();
  async function doRename() {
    const title = input.value.trim();
    if (!title) { showToast('Enter a title', 'warning'); return; }
    try {
      const res = await fetch('/rename', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ session_id: sessionIdToRename, title })
      });
      if (res.ok) {
        if (state.sessionId === sessionIdToRename) {
          state.sessionTitle = title;
          updateStatusBar();
        }
        const session = state.openSessions.find(s => s.id === sessionIdToRename);
        if (session) {
          session.title = title;
        }
        renderSessionTabs();
        showToast('Renamed to: ' + title);
      }
      else showToast('Could not rename session: ' + await responseError(res), 'error');
    } catch (e) { showToast('Could not rename session: ' + e.message, 'error'); }
    closeModal();
  }
  document.getElementById('rename-ok-btn').addEventListener('click', doRename);
  document.getElementById('rename-cancel-btn').addEventListener('click', closeModal);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); doRename(); }
    if (e.key === 'Escape') { e.preventDefault(); closeModal(); }
    e.stopPropagation();
  });
}

// ═══════════════════════════════════════════════════════════════════
//  SLASH MENU
// ═══════════════════════════════════════════════════════════════════

function handleInputKeydown(e) {
  if (isModalOpen()) return;
  if (slashMenuEl) {
    const items = slashMenuEl.querySelectorAll('.slash-menu-item');
    if (e.key === 'ArrowDown') { e.preventDefault(); slashActiveIdx = Math.min(slashActiveIdx + 1, items.length - 1); updateSlashMenuSelection(items); return; }
    if (e.key === 'ArrowUp') { e.preventDefault(); slashActiveIdx = Math.max(slashActiveIdx - 1, 0); updateSlashMenuSelection(items); return; }
    if (e.key === 'Tab' || (e.key === 'Enter' && slashActiveIdx >= 0)) {
      e.preventDefault();
      if (slashActiveIdx >= 0 && slashActiveIdx < items.length) {
        const cmd = items[slashActiveIdx].dataset.cmd;
        promptInput.value = '';
        closeSlashMenu();
        handleSlashCommand(cmd);
      }
      return;
    }
    if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      closeSlashMenu();
      return;
    }
  }
  if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendMessage(); }
}

function updateSlashMenu() {
  const val = promptInput.value;
  if (!val.startsWith('/')) { closeSlashMenu(); return; }
  const matches = SLASH_COMMANDS.filter(c => c.name.startsWith(val.toLowerCase()));
  if (matches.length === 0) { closeSlashMenu(); return; }
  showSlashMenu(matches);
}

function showSlashMenu(matches) {
  if (!slashMenuEl) { slashMenuEl = document.createElement('div'); slashMenuEl.className = 'slash-menu'; promptInput.parentElement.appendChild(slashMenuEl); }
  slashActiveIdx = 0;
  slashMenuEl.setAttribute('role', 'listbox');
  slashMenuEl.innerHTML = matches.map((m, i) =>
    `<div class="slash-menu-item${i === 0 ? ' active' : ''}" role="option" aria-selected="${i === 0}" data-cmd="${m.name}"><span class="cmd-name">${m.name}</span><span class="cmd-desc">${m.desc}</span></div>`
  ).join('');
  slashMenuEl.style.display = 'block';
  promptInput.setAttribute('aria-expanded', 'true');
  slashMenuEl.querySelectorAll('.slash-menu-item').forEach(el => {
    el.addEventListener('click', () => { const cmd = el.dataset.cmd; promptInput.value = ''; closeSlashMenu(); handleSlashCommand(cmd); });
  });
}

function updateSlashMenuSelection(items) { items.forEach((el, i) => { const on = i === slashActiveIdx; el.classList.toggle('active', on); el.setAttribute('aria-selected', String(on)); }); }
function closeSlashMenu() { if (slashMenuEl) { slashMenuEl.style.display = 'none'; slashMenuEl.innerHTML = ''; } slashActiveIdx = -1; promptInput.setAttribute('aria-expanded', 'false'); }

function addSystemMessage(text) {
  const session = state.openSessions.find(s => s.id === state.sessionId);
  if (session) {
    session.messages.push({ role: 'system', content: text });
    if (session.id === state.sessionId) {
      renderMessages();
      scrollToBottom();
    }
  }
}

// Two agent modes (server: generation/agent_mode.h). Retired modes map
// onto them: autonomous/monitor delegated (orchestrator), plan/build did not.
function normalizeAgentMode(mode) {
  if (mode === 'subagent') return 'subagent';
  if (mode === 'orchestrator' || mode === 'autonomous' || mode === 'monitor') return 'orchestrator';
  return 'normal';
}

function formatAgentModeTitle(mode) {
  const m = normalizeAgentMode(mode);
  if (m === 'subagent') return 'Subagent';
  return m === 'orchestrator' ? 'Orchestrator' : 'Normal';
}

async function showAgentModePicker() {
  const currentMode = normalizeAgentMode(state.agentMode);
  const modes = [
    {
      id: 'normal',
      label: 'Normal',
      sublabel: 'Works alone: reads, edits and verifies with tools, no subagents',
      isActive: currentMode === 'normal',
      badges: [{ type: 'ctx', text: 'Default' }]
    },
    {
      id: 'orchestrator',
      label: 'Orchestrator',
      sublabel: 'Manages subagent sessions: delegates jobs in parallel, monitors and merges reports',
      isActive: currentMode === 'orchestrator',
      badges: [{ type: 'reasoning', text: 'Lead' }]
    }
  ];

  showFuzzyPicker('Select Agent Mode', modes, currentMode, async (chosen) => {
    if (!chosen) return;
    await selectAgentMode(chosen.id);
  }, 'Choose an agent mode…');
}

async function selectAgentMode(mode) {
  mode = normalizeAgentMode(mode);
  if (mode === 'subagent') return;
  state.agentMode = mode;
  const session = state.openSessions.find(s => s.id === state.sessionId);
  if (session) {
    session.agentMode = mode;
  }
  syncMainHeader();
  try {
    if (state.sessionId) {
      await fetch('/session/' + encodeURIComponent(state.sessionId) + '/mode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ agent_mode: mode })
      });
    }
  } catch (e) {
    console.error('Failed to persist agent mode:', e);
  }
  showToast('Switched to ' + formatAgentModeTitle(mode) + ' mode');
}

function handleAgentCommand(args) {
  const clean = (args || '').trim().toLowerCase();
  if (clean === 'orchestrator' || clean === 'lead') {
    selectAgentMode('orchestrator');
  } else if (clean === 'normal') {
    selectAgentMode('normal');
  } else if (clean === 'toggle') {
    selectAgentMode(normalizeAgentMode(state.agentMode) === 'orchestrator' ? 'normal' : 'orchestrator');
  } else {
    showAgentModePicker();
  }
}

// ── Slash command dispatch ──
function handleSlashCommand(text) {
  const raw = text.slice(1).trim();
  const spaceIdx = raw.indexOf(' ');
  const cmd = (spaceIdx >= 0 ? raw.slice(0, spaceIdx) : raw).toLowerCase();
  const args = spaceIdx >= 0 ? raw.slice(spaceIdx + 1).trim() : '';
  switch (cmd) {
    case 'help': case '?': handleHelpCommand(); break;
    case 'model': case 'models': handleModelCommand(args); break;
    case 'agent': case 'mode': handleAgentCommand(args); break;
    case 'new': handleNewCommand(); break;
    case 'variant': handleVariantCommand(args); break;
    case 'tools': handleToolsCommand(args); break;
    case 'rename': handleRenameCommand(args); break;
    case 'session': case 'load': handleSessionCommand(args); break;
    case 'compact': handleCompactCommand(); break;
    case 'theme': case 'themes': handleThemeCommand(args); break;
    case 'queue': handleQueueCommand(args); break;
    case 'clear-queue': case 'cq': handleClearQueueCommand(); break;
    case 'retry': handleRetryCommand(); break;
    default: addSystemMessage('Unknown command: /' + cmd + '. Type /help for available commands.');
  }
}

function handleHelpCommand() {
  modalOverlay.innerHTML = `
    <div class="picker-modal help-modal">
      <div class="picker-header">Help</div>
      <div class="picker-body help-body">
        <p class="help-lead">Press <kbd>Ctrl</kbd>+<kbd>P</kbd> to see every command.</p>
        <div class="help-section">Session</div>
        <div class="help-row"><kbd>/new</kbd><span>New session</span></div>
        <div class="help-row"><kbd>/session</kbd><span>Switch session</span></div>
        <div class="help-row"><kbd>/compact</kbd><span>Summarize context</span></div>
        <div class="help-section">Model</div>
        <div class="help-row"><kbd>/model</kbd><span>Switch model</span></div>
        <div class="help-row"><kbd>/variant</kbd><span>Reasoning effort</span></div>
        <div class="help-section">View</div>
        <div class="help-row"><kbd>Esc</kbd><span>Stop generation</span></div>
        <div class="help-row"><kbd>/retry</kbd><span>Retry last prompt</span></div>
        <div class="help-row"><kbd>F2</kbd><span>Show / hide thinking</span></div>
        <div class="help-section">Web only</div>
        <div class="help-row"><kbd>Ctrl</kbd>+<kbd>P</kbd><span>Command palette (every command)</span></div>
        <div class="help-row"><kbd>Ctrl</kbd>+<kbd>N</kbd><span>New session</span></div>
        <div class="help-row"><kbd>Ctrl</kbd>+<kbd>B</kbd><span>Toggle sidebar</span></div>
        <div class="help-row"><kbd>Alt</kbd>+<kbd>F</kbd><span>Toggle file tree</span></div>
        <div class="help-row"><kbd>Alt</kbd>+<kbd>1</kbd>…<kbd>4</kbd><span>Chat / Files / Stats / Subagents</span></div>
        <div class="help-row"><kbd>?</kbd><span>Show this help (when not typing)</span></div>
        <div class="help-row"><kbd>/agent</kbd><span>Normal / Orchestrator</span></div>
        <div class="help-row"><kbd>/rename</kbd><span>Rename current session</span></div>
        <div class="help-row"><kbd>/theme</kbd><span>Switch theme</span></div>
        <div class="help-row"><kbd>/queue</kbd><span>List or drop queued prompts</span></div>
        <div class="help-row"><kbd>/clear-queue</kbd><span>Clear all queued prompts</span></div>
        <div class="help-row"><kbd>/tools</kbd><span>Toggle tool use on|off</span></div>
      </div>
      <div class="picker-footer"><span><kbd>Esc</kbd> close</span></div>
    </div>`;
  modalOverlay.classList.add('active');
  const close = (ev) => {
    if (ev.key === 'Escape' || ev.key === 'Enter') {
      ev.preventDefault();
      ev.stopPropagation();
      document.removeEventListener('keydown', close, true);
      closeModal();
    }
  };
  document.addEventListener('keydown', close, true);
}

// Hex colour helpers for theme contrast. Non-hex inputs are passed through.
function mixHex(a, b, p) {
  if (!/^#[0-9a-f]{6}$/i.test(a) || !/^#[0-9a-f]{6}$/i.test(b)) return a;
  return '#' + [1, 3, 5].map(i => {
    const x = parseInt(a.slice(i, i + 2), 16);
    const y = parseInt(b.slice(i, i + 2), 16);
    return Math.round(x + (y - x) * p).toString(16).padStart(2, '0');
  }).join('');
}
function contrastRatio(a, b) {
  const lum = (c) => [1, 3, 5].map(i => {
    const v = parseInt(c.slice(i, i + 2), 16) / 255;
    return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
  }).reduce((sum, v, i) => sum + v * [0.2126, 0.7152, 0.0722][i], 0);
  const [hi, lo] = [lum(a), lum(b)].sort((x, y) => y - x);
  return (hi + 0.05) / (lo + 0.05);
}
// Move `fg` toward `target` until it reaches `min` contrast on every background.
function readableTone(fg, target, bgs, min) {
  if (![fg, target, ...bgs].every(c => typeof c === 'string' && /^#[0-9a-f]{6}$/i.test(c))) return fg;
  for (let p = 0; p <= 1.0001; p += 0.05) {
    const c = mixHex(fg, target, p);
    if (bgs.every(b => contrastRatio(c, b) >= min)) return c;
  }
  return target;
}

// The theme to use when the user has not picked one: follow the OS setting.
function systemThemeName() {
  try {
    return window.matchMedia('(prefers-color-scheme: light)').matches ? 'qcode-light' : 'qcode';
  } catch (_) { return 'qcode'; }
}

// Keeps the header light/dark button in step with the active theme.
function syncThemeToggle() {
  const btn = document.getElementById('theme-toggle-btn');
  if (!btn) return;
  const light = !!(THEMES[state.theme] && THEMES[state.theme].scheme === 'light');
  btn.textContent = light ? 'Dark' : 'Light';
  btn.title = light ? 'Switch to dark theme' : 'Switch to light theme';
  btn.setAttribute('aria-label', btn.title);
}

// persist=false is for automatic choices (startup, OS change) so they do not pin the theme.
function applyTheme(name, { persist = true } = {}) {
  const t = THEMES[name];
  if (!t) return false;
  state.theme = name;
  const root = document.documentElement;
  root.dataset.theme = name;
  const set = (k, v) => root.style.setProperty(k, v);
  root.style.colorScheme = t.scheme === 'light' ? 'light' : 'dark';
  const meta = document.querySelector('meta[name="theme-color"]');
  if (meta) meta.setAttribute('content', t.bg);
  set('--bg', t.bg);
  set('--surface', t.surface);
  set('--surface-raised', t.raised);
  set('--surface-soft', t.surface);
  set('--text', t.text);
  // Muted and faint text must stay readable (WCAG AA 4.5:1) on every surface.
  const surfaces = [t.bg, t.surface, t.raised].filter(Boolean);
  const muted = readableTone(t.muted, t.text, surfaces, 4.5);
  set('--muted', muted);
  set('--faint', readableTone(mixHex(muted, t.bg, 0.72), t.text, surfaces, 4.5));
  set('--accent', t.accent);
  set('--accent-strong', t.accent2);
  set('--blue', t.accent2);
  set('--danger', t.danger);
  set('--warning', t.warning);
  set('--success', t.success);
  // Text on accent buttons: pick black or white, whichever contrasts more with the accent.
  // Buttons and the logo use a gradient that starts 20% lighter than the accent, so check both ends.
  const accentEnds = [t.accent, mixHex(t.accent, '#ffffff', 0.2)];
  const worst = fg => Math.min(...accentEnds.map(c => contrastRatio(fg, c)));
  const onAccent = worst('#000000') >= worst('#ffffff') ? '#000000' : '#ffffff';
  set('--on-accent', onAccent);
  // Code surfaces: slightly darker than the page; a 30% black mix turns light themes muddy grey.
  set('--code-bg', mixHex(t.bg, '#000000', t.scheme === 'light' ? 0.04 : 0.3));
  if (persist) {
    try { localStorage.setItem('qcode-theme', name); } catch (_) {}
  }
  persistPrefs();
  syncThemeToggle();
  return true;
}

function handleThemeCommand(args) {
  const q = (args || '').trim().toLowerCase();
  if (q && THEMES[q]) {
    applyTheme(q);
    showToast('Theme: ' + THEMES[q].title);
    return;
  }
  const items = Object.keys(THEMES).map((id) => ({
    id,
    label: THEMES[id].title,
    sublabel: id,
    category: id === 'qcode' ? 'Web' : 'TUI',
    isActive: id === state.theme
  }));
  showFuzzyPicker('Select Theme', items, state.theme, (chosen) => {
    applyTheme(chosen.id);
    showToast('Theme: ' + chosen.label);
  }, 'Type a theme name…', q);
}

function handleQueueCommand(args) {
  const session = state.openSessions.find(s => s.id === state.sessionId);
  const queue = (session && session.promptQueue) ? session.promptQueue : [];
  const raw = (args || '').trim();
  if (raw.startsWith('rm ')) {
    const n = parseInt(raw.slice(3).trim(), 10);
    if (!session || !n || n < 1 || n > queue.length) {
      showToast('Usage: /queue rm <1-based index>');
      return;
    }
    const [removed] = session.promptQueue.splice(n - 1, 1);
    withdrawQueuedPrompts(session.id, removed);
    updateQueueIndicator();
    renderMessages();
    showToast('Removed queued prompt #' + n);
    return;
  }
  if (queue.length === 0) {
    showToast('No queued prompts');
    return;
  }
  const lines = queue.map((t, i) => (i + 1) + '. ' + t.replace(/\s+/g, ' ').slice(0, 120));
  addSystemMessage('Queued prompts:\n' + lines.join('\n') + '\n\n/queue rm <n>  or  /clear-queue');
}

function handleClearQueueCommand() {
  const session = state.openSessions.find(s => s.id === state.sessionId);
  const n = (session && session.promptQueue) ? session.promptQueue.length : 0;
  if (!n) {
    showToast('No queued prompts to clear');
    return;
  }
  session.promptQueue = [];
  withdrawQueuedPrompts(session.id);
  updateQueueIndicator();
  renderMessages();
  showToast(n === 1 ? 'Cleared 1 queued prompt' : ('Cleared ' + n + ' queued prompts'));
}

async function handleRetryCommand() {
  const session = state.openSessions.find(s => s.id === state.sessionId);
  if (!session) { showToast('No active session'); return; }
  if (session.generating) { showToast('Already generating'); return; }
  let prompt = session.lastUserPrompt || '';
  if (!prompt) {
    for (let i = session.messages.length - 1; i >= 0; i--) {
      if (session.messages[i].role === 'user' && session.messages[i].content) {
        prompt = session.messages[i].content;
        break;
      }
    }
  }
  if (!prompt) { showToast('No user prompt available to retry.'); return; }
  await runGeneration(session, prompt);
}

function toggleThinking() {
  state.showThinking = !state.showThinking;
  persistPrefs();
  syncThinkingToggleBtn();
  renderMessages();
  showToast(state.showThinking ? 'Thinking: shown' : 'Thinking: hidden');
}

function syncThinkingToggleBtn() {
  if (!thinkingToggleBtn) return;
  const on = !!state.showThinking;
  thinkingToggleBtn.setAttribute('aria-pressed', on ? 'true' : 'false');
  thinkingToggleBtn.classList.toggle('off', !on);
  thinkingToggleBtn.title = on ? 'Hide thinking trace (F2)' : 'Show thinking trace (F2)';
}

function showCommandPalette(initialQuery) {
  if (isModalOpen()) {
    closeModal();
    return;
  }
  const items = PALETTE_COMMANDS.map((c) => ({
    id: c.id,
    label: c.label,
    sublabel: c.sublabel,
    category: c.category
  }));
  showFuzzyPicker('Commands', items, '', (chosen) => {
    executePaletteCommand(chosen.id);
  }, 'Type a command…', initialQuery || '');
}

function executePaletteCommand(id) {
  switch (id) {
    case 'session_new': showNewSessionModal(); break;
    case 'session_list': handleSessionCommand(''); break;
    case 'session_rename': handleRenameCommand(''); break;
    case 'session_compact': handleCompactCommand(); break;
    case 'session_clear_queue': handleClearQueueCommand(); break;
    case 'session_retry': handleRetryCommand(); break;
    case 'agent_mode': showAgentModePicker(); break;
    case 'model_select': handleModelCommand(''); break;
    case 'model_variant': handleVariantCommand(''); break;
    case 'thinking_toggle': toggleThinking(); break;
    case 'chat_open': switchTab('chat'); break;
    case 'files_open': switchTab('files'); switchFilesSubtab('git'); break;
    case 'stats_open': switchTab('stats'); break;
    case 'sessions_open': switchTab('sessions'); break;
    case 'theme_select': handleThemeCommand(''); break;
    case 'help': handleHelpCommand(); break;
    default: break;
  }
}

function renderQueuedBlock(queue) {
  const wrap = document.createElement('div');
  wrap.className = 'queued-block';
  let html = '<div class="queued-head"><span class="qcode-spinner"></span><span>Queued · ' + queue.length + '</span><span class="queued-hint">/clear-queue to cancel</span></div>';
  queue.forEach((text) => {
    html += '<div class="queued-body">' + esc(text) + '</div>';
  });
  wrap.innerHTML = html;
  return wrap;
}

// ── /model ──
function handleModelCommand(args) {
  const items = [];
  for (const p of state.providers) {
    for (const m of p.models) {
      items.push({
        id: p.id + '||' + m.id,
        label: m.name,
        sublabel: m.id !== m.name ? m.id : '',
        category: p.name,
        isActive: p.id === state.provider && m.id === state.model,
        badges: modelTypeBadges(m),
        modelData: m
      });
    }
  }
  if (items.length === 0) { addSystemMessage('No models available.'); return; }
  showFuzzyPicker('Select Model', items, state.provider + '||' + state.model, (chosen) => {
    const parts = chosen.id.split('||');
    const pid = parts[0], mid = parts.slice(1).join('||');
    for (const p of state.providers) {
      if (p.id === pid) {
        for (const m of p.models) {
          if (m.id === mid) { selectModel(p, m); return; }
        }
      }
    }
  }, 'Type a model name to filter…', args || '');
}

function selectModel(provider, model) {
  applyProviderModel(provider.id, model.id);
  showToast('Model: ' + provider.name + ' / ' + model.name);
}

// ── /session ──
async function handleSessionCommand(args) {
  let sessions;
  try {
    const res = await fetch('/sessions');
    if (!res.ok) throw new Error(await responseError(res));
    sessions = await res.json();
  } catch (e) { showToast('Could not load sessions: ' + e.message, 'error'); return; }
  if (!sessions || sessions.length === 0) { showToast('No saved sessions'); return; }
  const items = sessions.map(s => ({
    id: s.id, label: s.title || '(untitled)',
    sublabel: s.model ? s.model : s.id.substring(0, 12) + '…',
    workspace: s.workspace || '', isActive: s.id === state.sessionId
  }));
  showFuzzyPicker('Select Session', items, state.sessionId || '', (chosen) => {
    loadSessionById(chosen.id);
  }, 'Type a title or workspace to filter…', args || '');
}

// ── Interleaved thinking timeline ──
// Each assistant message keeps `timeline`: an ordered list of
// { kind: 'thought' } (coalesced thinking chunk) and { kind: 'tool', tool_call_id }
// entries. Live reasoning deltas accumulate into the trailing thought entry so
// thinking renders adjacent to the tool call it preceded. Persisted history
// rebuilds the same order from interleaved Reasoning/ToolCall DB rows.
function appendThoughtChunk(msg, text) {
  if (!text) return;
  if (!msg.reasoning) msg.reasoning = '';
  msg.reasoning += text;
  if (!msg.timeline) msg.timeline = [];
  const last = msg.timeline[msg.timeline.length - 1];
  if (last && last.kind === 'thought' && !last.closed) {
    last.text = (last.text || '') + text;
    return;
  }
  msg.timeline.push({ kind: 'thought', text });
}

// Assistant text is kept both as the full `content` (copy/raw) and as
// timeline segments so prose renders between the tool calls it preceded.
function appendTextChunk(msg, text) {
  if (!text) return;
  msg.content = (msg.content || '') + text;
  if (!msg.timeline) msg.timeline = [];
  const last = msg.timeline[msg.timeline.length - 1];
  if (last && last.kind === 'text') {
    last.text = (last.text || '') + text;
    return;
  }
  msg.timeline.push({ kind: 'text', text });
}

function closeOpenThought(msg) {
  if (!msg.timeline) return;
  const last = msg.timeline[msg.timeline.length - 1];
  if (last && last.kind === 'thought') last.closed = true;
}

function renderThoughtBlock(text, opts) {
  const open = !!(opts && opts.open);
  const live = !!(opts && opts.live);
  const rc = document.createElement('div');
  rc.className = 'reasoning-block' + (open ? '' : ' collapsed') + (live ? ' live' : '');
  const rl = document.createElement('button');
  rl.type = 'button';
  rl.className = 'reasoning-label';
  rl.setAttribute('aria-expanded', String(open));
  const est = Math.max(1, Math.floor((text.length + 3) / 4));
  const compact = est >= 1000 ? (est / 1000).toFixed(1).replace(/\.0$/, '') + 'k' : String(est);
  rl.innerHTML = '<span class="reasoning-chevron">' + icon('chevron', 12) + '</span>'
    + '<span class="reasoning-icon">' + icon('brain', 13) + '</span>'
    + '<span class="reasoning-title">' + (live ? 'Thinking' : 'Thought') + '</span>'
    + '<span class="thought-tokens">' + compact + ' tokens</span>';
  const rt = document.createElement('div'); rt.className = 'reasoning-text';
  // Collapsed thoughts are parsed on first expand, not on every re-render.
  let filled = false;
  const fill = () => {
    if (filled || rt._stream) return;  // the live stream already fills it
    filled = true;
    if (live) renderMarkdownStreaming(rt, text, { decorate: false });
    else { rt.innerHTML = renderMarkdown(text); tagMarkdownLinks(rt); }
  };
  if (open) fill();
  rl.addEventListener('click', () => {
    const collapsed = rc.classList.toggle('collapsed');
    rl.setAttribute('aria-expanded', String(!collapsed));
    if (!collapsed) fill();
  });
  rc.appendChild(rl); rc.appendChild(rt);
  return rc;
}

function parseMessages(msgs, session) {
  session.messages = [];
  let currentAssistantMsg = null;
  for (const m of msgs) {
    if (m.role === 'User' || m.role === 'user') {
      currentAssistantMsg = null;
      session.messages.push({ role: 'user', content: m.content });
    } else if (m.role === 'Assistant' || m.role === 'assistant') {
      if (!currentAssistantMsg) {
        currentAssistantMsg = { role: 'assistant', content: '', toolEvents: [], timeline: [] };
        session.messages.push(currentAssistantMsg);
      }
      if (m.content) {
        const tl = currentAssistantMsg.timeline;
        appendTextChunk(currentAssistantMsg, (currentAssistantMsg.content ? '\n\n' : '') + m.content);
        // A separator belongs to `content` only; a fresh segment starts clean.
        const seg = tl[tl.length - 1];
        if (seg && seg.kind === 'text') seg.text = seg.text.replace(/^\n\n/, '');
      }
    } else if (m.role === 'ToolCall') {
      try {
        const tc = JSON.parse(m.content);
        if (!currentAssistantMsg) {
          currentAssistantMsg = { role: 'assistant', content: '', toolEvents: [], timeline: [] };
          session.messages.push(currentAssistantMsg);
        }
        const entry = {
          type: 'tool_call',
          tool_call_id: tc.id,
          tool_name: tc.name,
          arguments: tc.arguments,
          status: 'running'
        };
        currentAssistantMsg.toolEvents.push(entry);
        if (!currentAssistantMsg._toolsById) currentAssistantMsg._toolsById = {};
        currentAssistantMsg._toolsById[tc.id] = entry;
        if (!currentAssistantMsg.timeline) currentAssistantMsg.timeline = [];
        currentAssistantMsg.timeline.push({ kind: 'tool', tool_call_id: tc.id });
      } catch (e) {}
    } else if (m.role === 'ToolResult') {
      try {
        const tr = JSON.parse(m.content);
        if (!currentAssistantMsg) {
          currentAssistantMsg = { role: 'assistant', content: '', toolEvents: [], timeline: [] };
          session.messages.push(currentAssistantMsg);
        }
        const tc = (currentAssistantMsg._toolsById && currentAssistantMsg._toolsById[tr.tool_call_id])
          || currentAssistantMsg.toolEvents.find(t => t.tool_call_id === tr.tool_call_id);
        if (tc) {
          tc.status = tr.is_error ? 'error' : 'success';
          tc.result = tr.result;
          tc.duration_ms = tr.duration_ms;
        } else {
          const orphan = {
            type: 'tool_call',
            tool_call_id: tr.tool_call_id,
            tool_name: 'tool',
            arguments: null,
            status: tr.is_error ? 'error' : 'success',
            result: tr.result,
            duration_ms: tr.duration_ms
          };
          currentAssistantMsg.toolEvents.push(orphan);
          if (!currentAssistantMsg.timeline) currentAssistantMsg.timeline = [];
          currentAssistantMsg.timeline.push({ kind: 'tool', tool_call_id: tr.tool_call_id });
        }
      } catch (e) {}
    } else if (m.role === 'Reasoning') {
      if (!currentAssistantMsg) {
        currentAssistantMsg = { role: 'assistant', content: '', toolEvents: [], timeline: [] };
        session.messages.push(currentAssistantMsg);
      }
      if (!currentAssistantMsg.timeline) currentAssistantMsg.timeline = [];
      let text = '';
      try {
        const r = JSON.parse(m.content);
        if (r && typeof r === 'object') {
          text = r.text || r.reasoning || r.content || '';
        } else if (typeof r === 'string') {
          text = r;
        } else {
          text = String(m.content);
        }
      } catch (e) {
        text = m.content;
      }
      if (text) {
        appendThoughtChunk(currentAssistantMsg, text);
      }
    } else {
      currentAssistantMsg = null;
      session.messages.push({ role: 'system', content: m.content });
    }
  }
}

async function loadSessionData(id) {
  const session = {
    id: id,
    title: '',
    workspace: '',
    messages: [],
    generating: false,
    stream: null,
    provider: '',
    model: ''
  };

  try {
    const [infoRes, msgsRes] = await Promise.all([
      fetch('/session/' + id).catch(() => null),
      fetch('/session/' + id + '/messages?tail=100').catch(() => null)
    ]);
    if (infoRes && infoRes.ok) {
      const info = await infoRes.json();
      session.title = info.title || '';
      session.workspace = info.workspace || '';
      session.provider = info.provider || '';
      session.model = info.model || '';
      session.agentMode = info.agent_mode ? normalizeAgentMode(info.agent_mode) : (id.indexOf('ses_') === 0 ? 'subagent' : 'normal');
      session.reasoning = info.reasoning_mode || session.reasoning || state.reasoning || 'low';
      session.parentSessionId = info.parent_session_id || state.parentSessionId || '';
      // Taken before /messages, so a write in between still reads as a change.
      session.serverRev = sessionRev(info);
    }
    if (msgsRes && msgsRes.ok) {
      const msgs = await msgsRes.json();
      if (Array.isArray(msgs)) {
        parseMessages(msgs, session);
      }
    }
  } catch (e) {}

  if (!session.title && session.id) {
    session.title = (session.agentMode === 'subagent' ? 'Subagent ' : 'Session ') + session.id.substring(0, 8);
  }
  return session;
}

// Lightweight session built from a /sessions row. History is fetched on first open.
function makeSessionStub(row) {
  const id = row.id;
  const agentMode = row.agent_mode ? normalizeAgentMode(row.agent_mode) : (id.indexOf('ses_') === 0 ? 'subagent' : 'normal');
  return {
    id: id,
    title: row.title || ((agentMode === 'subagent' ? 'Subagent ' : 'Session ') + id.substring(0, 8)),
    workspace: row.workspace || '',
    messages: [],
    generating: false,
    stream: null,
    provider: row.provider || '',
    model: row.model || '',
    agentMode: agentMode,
    reasoning: row.reasoning_mode || '',
    parentSessionId: row.parent_session_id || '',
    serverRev: null,
    stub: true
  };
}

// Fills a stub from the server (session info + history). Safe to call
// repeatedly; a failed load is retried on the next call.
function ensureSessionLoaded(session) {
  if (!session.stub) return Promise.resolve();
  if (!session.loadPromise) {
    session.loadPromise = loadSessionData(session.id).then(full => {
      // loadSessionData swallows fetch errors; serverRev is set only when
      // GET /session/:id succeeded, so keep the stub (and retry) otherwise.
      if (full.serverRev === undefined) throw new Error('session info unavailable');
      Object.assign(session, full, { stub: false });
    }).finally(() => {
      delete session.loadPromise;
    });
  }
  return session.loadPromise;
}

// ── Session refresh ──
// Live turns are rendered from their stream. Everything else is
// event-driven: a turn's end, Stop, session switch, tab focus/visibility and
// reconnect call refreshSession(), a cheap GET /session/:id (no history)
// that reattaches to a running turn or refetches /messages only when
// message_count / updated_at moved. A short light poll catches changes made
// elsewhere (TUI, other tabs).
function messageSignature(msgs) {
  if (!Array.isArray(msgs) || msgs.length === 0) return '0|empty';
  const last = msgs[msgs.length - 1] || {};
  const content = (last.content || '').slice(-240);
  const timelineLen = (last.timeline || []).length;
  const tools = (last.toolEvents || []).map(t => (t.tool_call_id || '') + ':' + (t.status || '')).join(',');
  return msgs.length + '|' + (last.role || '') + '|' + content.length + ':' + content + '|' + timelineLen + '|' + tools;
}

// A turn that failed before the server persisted it (provider down, 5xx)
// exists only locally. Keep the user's prompt and the inline error across a
// history refresh instead of silently dropping them; when the server did
// persist the turn, carry the error onto its reply.
function keepFailedTurn(local, fresh) {
  const failed = local[local.length - 1];
  if (!failed || failed.role !== 'assistant' || !failed.streamError) return;
  const prompt = local[local.length - 2];
  const freshLast = fresh[fresh.length - 1];
  const freshLastUser = [...fresh].reverse().find(m => m.role === 'user');
  if (prompt && prompt.role === 'user' && (!freshLastUser || freshLastUser.content !== prompt.content)) {
    fresh.push(prompt, failed);
  } else if (freshLast && freshLast.role === 'assistant' && !freshLast.streamError) {
    freshLast.streamError = failed.streamError;
    freshLast.stoppedEarly = failed.stoppedEarly;
  } else if (freshLast && freshLast.role === 'user') {
    fresh.push(failed);
  }
}

function sessionRev(info) {
  return (info.message_count || 0) + ':' + (info.updated_at || 0);
}

// Replace the session's messages with the server history. Never touches a
// session that is following a live turn.
async function syncSessionMessages(session) {
  const res = await fetch('/session/' + session.id + '/messages', { cache: 'no-store' });
  if (!res.ok) return false;
  const msgs = await res.json();
  if (!Array.isArray(msgs) || session.stream) return false;
  const tmp = { messages: [] };
  parseMessages(msgs, tmp);
  keepInterruptedPartials(session.messages, tmp.messages);
  keepFailedTurn(session.messages, tmp.messages);
  if (messageSignature(tmp.messages) === messageSignature(session.messages)) return false;
  const stickToBottom = nearBottom();
  session.messages = tmp.messages;
  if (session.id === state.sessionId) {
    renderMessages();
    // Only follow new messages when the user was already near the bottom;
    // otherwise the jump pill is the affordance.
    if (stickToBottom) scrollToBottom(true);
    else updateJumpPill();
  }
  return true;
}

// opts.force refetches the history even when the revision looks unchanged.
async function refreshSession(session, opts) {
  if (!session || !session.id || session.stub) return;
  // A tab closed mid-turn (its end-of-turn refresh is still queued) must not
  // refetch its whole history into an object nothing renders any more.
  if (!state.openSessions.includes(session)) return;
  const force = !!(opts && opts.force);
  if (session.refreshing) {
    if (force || !session.refreshAgain) session.refreshAgain = force ? 'force' : 'light';
    return;
  }
  if (session.stream) return;  // the live stream is authoritative while it runs
  session.refreshing = true;
  try {
    const res = await fetch('/session/' + session.id, { cache: 'no-store' });
    if (!res.ok) return;
    const info = await res.json();
    if (session.stream) return;
    if (info.generating) {
      // Reattach unless it is the turn the user just stopped winding down.
      if (session.id === state.sessionId && info.turn !== session.stoppedTurn) {
        attachToRunningTurn(session);
      }
      return;
    }
    const rev = sessionRev(info);
    if (!force && rev === session.serverRev) return;
    session.serverRev = rev;
    await syncSessionMessages(session);
  } catch (_) {
    // Offline or server restarting: probe now so the indicator and backoff
    // react without waiting for the 30 s health tick.
    updateConnectionStatus();
  } finally {
    session.refreshing = false;
    const again = session.refreshAgain;
    session.refreshAgain = null;
    if (again) refreshSession(session, { force: again === 'force' });
  }
}

function refreshActiveSession() {
  if (document.hidden || !connOnline) return;
  refreshSession(state.openSessions.find(s => s.id === state.sessionId));
}

async function loadSessionById(id) {
  let session = state.openSessions.find(s => s.id === id);
  if (session) {
    switchSession(id);
    return;
  }

  session = await loadSessionData(id);
  state.openSessions.push(session);
  switchSession(id);
}

function handleNewCommand() { showNewSessionModal(); }
function handleVariantCommand(args) {
  const known = ['off', 'low', 'medium', 'high'];
  const q = (args || '').trim().toLowerCase();
  if (known.includes(q)) {
    state.reasoning = q;
    if (reasoningSelect) reasoningSelect.value = q;
    showToast('Variant (effort): ' + q);
    return;
  }
  if (q && !known.includes(q)) { addSystemMessage('Invalid variant. Use off|low|medium|high.'); return; }
  const items = known.map((id) => ({ id, label: id, category: 'Reasoning', isActive: id === state.reasoning }));
  showFuzzyPicker('Select Reasoning Variant', items, state.reasoning, (chosen) => {
    state.reasoning = chosen.id;
    if (reasoningSelect) reasoningSelect.value = chosen.id;
    showToast('Variant (effort): ' + chosen.id);
  }, 'off | low | medium | high', q);
}
function handleToolsCommand(args) {
  const val = args || (state.toolsEnabled ? 'off' : 'on');
  if (val === 'on') { state.toolsEnabled = true; showToast('Tools enabled'); }
  else if (val === 'off') { state.toolsEnabled = false; showToast('Tools disabled'); }
  else addSystemMessage('Usage: /tools [on|off]');
}
function handleRenameCommand(args) {
  if (args) { doRenameDirect(args); return; }
  showRenameModal();
}
async function doRenameDirect(title) {
  if (!state.sessionId) { addSystemMessage('No active session.'); return; }
  try {
    const res = await fetch('/rename', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ session_id: state.sessionId, title }) });
    if (res.ok) { state.sessionTitle = title; updateStatusBar(); showToast('Renamed to: ' + title); }
    else showToast('Could not rename session: ' + await responseError(res), 'error');
  } catch (e) { showToast('Could not rename session: ' + e.message, 'error'); }
}
async function handleCompactCommand() {
  if (!state.sessionId) {
    addSystemMessage("Compact: No active session.");
    return;
  }
  
  addSystemMessage("Compacting conversation...");
  try {
    const res = await fetch('/session/' + state.sessionId + '/compact', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ keep: 5 })
    });
    
    if (res.ok) {
      const data = await res.json();
      if (data.status === 'success') {
        showToast("Compaction complete");
        const session = state.openSessions.find(s => s.id === state.sessionId);
        if (session) {
          const msgRes = await fetch('/session/' + state.sessionId + '/messages');
          if (msgRes.ok) {
            const msgs = await msgRes.json();
            if (Array.isArray(msgs)) {
              parseMessages(msgs, session);
            }
          }
          renderMessages();
        }
      } else {
        addSystemMessage("Compaction failed: " + (data.error || "unknown error"));
      }
    } else {
      const data = await res.json().catch(() => ({}));
      addSystemMessage("Compaction failed: " + (data.error || res.statusText));
    }
  } catch (e) {
    addSystemMessage("Compaction failed: " + e.message);
  }
}

// ═══════════════════════════════════════════════════════════════════
//  SEND MESSAGE + STREAMING
// ═══════════════════════════════════════════════════════════════════

// A turn runs and persists itself on the server; the WebUI only mirrors it.
// session.stream = { msg, turn, reader, stopped, complete } follows one turn:
// `msg` is the assistant message being streamed into, `turn` the server turn
// id every NDJSON line carries. Lines of another turn, or of a turn the user
// stopped (session.stoppedTurn), are dropped. A dropped connection
// reattaches with {resume: true}: the server replays a snapshot (history
// plus the turn's unsaved text) and continues with the turn's later events.

const kMaxStreamRetries = 8;
// The server writes a heartbeat every 2.5 s while a turn is idle; this much
// silence means the connection died without closing (sleep, proxy, network
// switch), so drop it and resume from the server's event log.
const kStreamStallMs = 15000;

function isNetworkError(e) {
  return !!e && (e.name === 'AbortError' || e.name === 'TypeError'
    || /network|fetch|abort|interrupted|failed/i.test(e.message || ''));
}

async function responseError(res) {
  const text = await res.text().catch(() => '');
  try { return JSON.parse(text).error || text; } catch (_) { return text || ('HTTP ' + res.status); }
}

function postGenerate(session, text) {
  // Prefer the live dropdown selection, then the session's stored pair.
  // Always resolve against /providers so stale/empty DB values cannot
  // produce "Unknown provider:".
  const resolved = resolveProviderModel(
    state.provider || session.provider,
    state.model || session.model
  );
  if (!resolved.ok) {
    return Promise.reject(new Error('No provider/model configured. Check the sidebar selectors.'));
  }
  session.provider = resolved.provider;
  session.model = resolved.model;
  state.provider = resolved.provider;
  state.model = resolved.model;
  return fetch(`/session/${session.id}/generate`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      text,
      provider: resolved.provider,
      model: resolved.model,
      agent_mode: normalizeAgentMode(session.agentMode || state.agentMode),
      reasoning_mode: state.reasoning,
      session_id: session.id
    })
  });
}

const kResumeTimeoutMs = 10000;
function postResume(session) {
  return fetchWithTimeout(`/session/${session.id}/generate`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ resume: true, session_id: session.id })
  }, kResumeTimeoutMs);
}

function postCancel(sessionId) {
  return fetch('/session/' + sessionId + '/cancel', { method: 'POST' }).catch(() => {});
}

function beginStream(session, msg) {
  const stream = { msg, turn: null, reader: null, stopped: false, complete: false };
  session.stream = stream;
  session.generating = true;
  renderSessionTabs();
  if (session.id === state.sessionId) setGenerating(true);
  return stream;
}

// Reset the UI when `stream` ends, unless Stop or a newer turn took over.
function endStream(session, stream) {
  if (session.stream !== stream) return;
  session.stream = null;
  session.generating = false;
  renderSessionTabs();
  if (session.id === state.sessionId) {
    setGenerating(false);
    renderMessages();
    scrollToBottom();
  }
  updateQueueIndicator();
  // Reconcile with what the server persisted for the turn.
  setTimeout(() => refreshSession(session, { force: true }), 300);
  // Per-call usage/cost is persisted before the turn ends; refresh an open
  // Stats tab without the skeleton flash.
  if (session.id === state.sessionId && state.activeTab === 'stats') {
    setTimeout(() => loadStatsTab({ quiet: true }), 400);
  }
}

function failStream(stream, message) {
  showToast(message);
  if (stream.msg) {
    stream.msg.streamError = message;
    stream.msg.stoppedEarly = true;
  }
}

function dropQueuedPrompt(session, text) {
  const queue = session.promptQueue || [];
  const i = queue.indexOf(text);
  if (i >= 0) queue.splice(i, 1);
  updateQueueIndicator();
}

// Rebuild the session from a resume snapshot: persisted history plus the
// running turn's not-yet-persisted text, which later deltas extend.
function applySnapshot(session, stream, evt) {
  parseMessages(Array.isArray(evt.messages) ? evt.messages : [], session);
  let msg = session.messages[session.messages.length - 1];
  if (!msg || msg.role !== 'assistant') {
    msg = { role: 'assistant', content: '', toolEvents: [], timeline: [], createdAt: Date.now() };
    session.messages.push(msg);
  }
  if (evt.reasoning_text) appendThoughtChunk(msg, evt.reasoning_text);
  if (evt.assistant_text) appendTextChunk(msg, evt.assistant_text);
  stream.msg = msg;
  if (session.id === state.sessionId) { renderMessages(); scrollToBottom(); }
}

// A prompt queued during the turn reached the model: show it where the model
// read it and continue the reply in a new assistant message.
function applyInjectedPrompt(session, stream, text) {
  closeOpenThought(stream.msg);
  session.promptQueue = (session.promptQueue || []).filter(q => !text.includes(q));
  session.messages.push({ role: 'user', content: text, createdAt: Date.now() });
  const msg = { role: 'assistant', content: '', toolEvents: [], timeline: [], createdAt: Date.now() };
  session.messages.push(msg);
  stream.msg = msg;
  updateQueueIndicator();
  if (session.id === state.sessionId) { renderMessages(); scrollToBottom(); }
}

// A small model named the session (server session_title).
function applySessionTitle(session, title) {
  if (!title) return;
  session.title = title;
  if (session.id === state.sessionId) { state.sessionTitle = title; updateStatusBar(); }
  renderSessionTabs();
}

// The server compacted the conversation mid-turn: show the notice and the
// summary that replaced the history; the turn continues in a new bubble.
function applyCompaction(session, stream, evt) {
  closeOpenThought(stream.msg);
  session.messages.push({ role: 'system', content: evt.note || 'Context auto-compacted', createdAt: Date.now() });
  session.messages.push({ role: 'user', content: evt.text || '', createdAt: Date.now() });
  const msg = { role: 'assistant', content: '', toolEvents: [], timeline: [], createdAt: Date.now() };
  session.messages.push(msg);
  stream.msg = msg;
  if (session.id === state.sessionId) {
    renderMessages(); scrollToBottom();
    showToast(`Context compacted: ${evt.tokens_before} -> ~${evt.tokens_after} tokens`);
  }
}

function applyTurnEvent(session, stream, evt) {
  if (evt.turn != null) {
    if (evt.turn === session.stoppedTurn) { stream.stopped = true; return; }
    if (stream.turn == null) stream.turn = evt.turn;
    else if (evt.turn !== stream.turn) return;
  }
  if (evt.type === 'session.snapshot') { applySnapshot(session, stream, evt); return; }
  if (evt.type === 'generation.complete') stream.complete = true;
  if (!stream.msg) return;  // resume found no running turn
  if (evt.type === 'backend.user.message.injected') {
    applyInjectedPrompt(session, stream, evt.text || '');
    return;
  }
  if (evt.type === 'backend.conversation.compacted') {
    applyCompaction(session, stream, evt);
    return;
  }
  if (evt.type === 'backend.session.title.changed') {
    applySessionTitle(session, evt.title || '');
    return;
  }
  handleEvent(evt, stream.msg, session);
  if (evt.type === 'generation.complete' && evt.turn == null && !evt.error && stream.msg.content) {
    stream.msg.streamError = 'Connection lost before the reply finished. Click Retry to regenerate it.';
    stream.msg.stoppedEarly = true;
  }
}

// Read one NDJSON response into `stream` until it ends, completes or stops.
async function readTurnStream(session, stream, res) {
  const reader = res.body.getReader();
  stream.reader = reader;
  const decoder = new TextDecoder();
  let buffer = '';
  const consume = (line) => {
    if (!line.trim() || stream.stopped) return;
    let evt;
    try {
      evt = JSON.parse(line);
    } catch (error) {
      console.warn('Ignoring malformed stream event:', error, line);
      return;
    }
    applyTurnEvent(session, stream, evt);
  };
  const readOrStall = () => new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      const err = new Error('Stream stalled: no data from the server');
      err.name = 'AbortError';
      reject(err);
    }, kStreamStallMs);
    reader.read().then(
      (v) => { clearTimeout(timer); resolve(v); },
      (e) => { clearTimeout(timer); reject(e); });
  });
  try {
    while (!stream.stopped && !stream.complete) {
      const { done, value } = await readOrStall();
      if (done) break;
      stream.receivedData = true;
      buffer += decoder.decode(value, { stream: true });
      const lines = buffer.split('\n');
      buffer = lines.pop() || '';
      for (const line of lines) consume(line);
    }
    buffer += decoder.decode();
    consume(buffer);
  } catch (e) {
    reader.cancel().catch(() => {});
    throw e;
  } finally {
    stream.reader = null;
    if (stream.stopped || stream.complete) reader.cancel().catch(() => {});
  }
}

// Follow `stream` until its turn completes or the user stops it. `res` is the
// response that started the turn, or null to reattach to the running turn.
async function followTurn(session, stream, res) {
  let retries = 0;
  try {
    for (;;) {
      let error = null;
      stream.receivedData = false;
      try {
        if (!res) res = await postResume(session);
        if (!res.ok) {
          if (isRetryableHttpStatus(res.status)) throw new TypeError('Server unavailable (HTTP ' + res.status + ')');
          throw new Error(await responseError(res));
        }
        if (!res.body) throw new Error('The server returned an empty response stream');
        await readTurnStream(session, stream, res);
      } catch (e) {
        error = e;
        if (isNetworkError(e)) updateConnectionStatus();
      }
      res = null;
      if (stream.stopped || stream.complete) return;
      if (error && !isNetworkError(error)) {
        failStream(stream, 'Generation interrupted: ' + (error.message || 'unknown error'));
        return;
      }
      // A connection that delivered data before dropping was healthy: start
      // the backoff over so long turns survive several unrelated drops.
      if (stream.receivedData) retries = 0;
      if (retries >= kMaxStreamRetries) {
        failStream(stream, 'Connection lost — click Retry to continue');
        return;
      }
      retries++;
      if (retries === 1) showToast('Connection dropped — reconnecting…', 'warning');
      await new Promise(r => setTimeout(r, Math.min(1000 * Math.pow(1.5, retries - 1), 5000)));
      if (stream.stopped) return;
    }
  } finally {
    endStream(session, stream);
  }
}

// Start a turn for `text`. `res` is passed when the POST was already made
// (a queued prompt that found the previous turn over).
async function runGeneration(session, text, res) {
  session.lastUserPrompt = text;
  const userMsg = { role: 'user', content: text, createdAt: Date.now() };
  const assistantMsg = { role: 'assistant', content: '', toolEvents: [], timeline: [], createdAt: Date.now() };
  session.messages.push(userMsg, assistantMsg);
  const stream = beginStream(session, assistantMsg);
  if (session.id === state.sessionId) { renderMessages(); scrollToBottom(true); }

  if (!res) {
    try {
      res = await postGenerate(session, text);
    } catch (e) {
      failStream(stream, isNetworkError(e) ? 'Connection lost — click Retry to continue' : (e.message || 'Failed to send'));
      endStream(session, stream);
      return;
    }
  }
  if (stream.stopped) {
    // Stopped before the server answered: stop the turn it may have started.
    if (res.ok) postCancel(session.id);
    if (res.body) res.body.cancel().catch(() => {});
    return;
  }
  if (res.status === 202) {
    // A turn was already running (another tab, the TUI): it takes the prompt
    // mid-turn. Follow that turn; its snapshot replaces the optimistic rows.
    session.messages = session.messages.filter(m => m !== userMsg && m !== assistantMsg);
    (session.promptQueue = session.promptQueue || []).push(text);
    updateQueueIndicator();
    stream.msg = null;
    res = null;
  }
  await followTurn(session, stream, res);
}

// Reattach to the server's running turn (page load, reconnect, a turn
// started elsewhere). The resume snapshot rebuilds history and live text.
function attachToRunningTurn(session) {
  if (session.stream) return;
  followTurn(session, beginStream(session, null), null);
}

// Send a prompt while a turn runs: the server queues it and the running turn
// takes it at its next model request (backend.user.message.injected).
async function queuePrompt(session, text) {
  if (!session.promptQueue) session.promptQueue = [];
  session.promptQueue.push(text);
  updateQueueIndicator();
  if (session.id === state.sessionId) { renderMessages(); scrollToBottom(true); }
  const stops = session.stops || 0;
  let res;
  try {
    res = await postGenerate(session, text);
  } catch (e) {
    dropQueuedPrompt(session, text);
    if (session.id === state.sessionId) renderMessages();
    showToast('Failed to queue prompt: ' + e.message);
    return;
  }
  if ((session.stops || 0) !== stops) {
    // Stopped while sending. Stop drops queued prompts, so this one too
    // (the server already dropped it, or started a turn for it to cancel).
    if (res.ok && res.status !== 202) postCancel(session.id);
    if (res.body) res.body.cancel().catch(() => {});
    return;
  }
  if (res.status === 202) {
    showToast('Prompt queued');
    return;
  }
  dropQueuedPrompt(session, text);
  if (res.ok && res.body) {
    // The turn ended meanwhile, so the prompt started a new one.
    await runGeneration(session, text, res);
    return;
  }
  if (session.id === state.sessionId) renderMessages();
  showToast('Failed to queue prompt: ' + await responseError(res));
}

// Withdraw queued prompts the running turn has not taken yet (all when
// `text` is omitted).
function withdrawQueuedPrompts(sessionId, text) {
  fetch('/session/' + sessionId + '/queue', {
    method: 'DELETE',
    headers: { 'Content-Type': 'application/json' },
    body: text == null ? '' : JSON.stringify({ text })
  }).catch(() => {});
}

function updateQueueIndicator() {
  const indicator = document.getElementById('queue-indicator');
  const textEl = document.getElementById('queue-text');
  if (!indicator || !textEl) return;

  const session = state.openSessions.find(s => s.id === state.sessionId);
  if (session && session.promptQueue && session.promptQueue.length > 0) {
    const count = session.promptQueue.length;
    textEl.textContent = `${count} prompt${count > 1 ? 's' : ''} in queue`;
    indicator.classList.remove('hidden');
  } else {
    indicator.classList.add('hidden');
  }
}

async function sendMessage() {
  if (isModalOpen()) return;
  clearDraft();
  const text = promptInput.value.trim();
  if (!text) return;
  if (text.startsWith('/')) {
    promptInput.value = '';
    resizePromptInput();
    handleSlashCommand(text);
    return;
  }

  const session = state.openSessions.find(s => s.id === state.sessionId);
  if (!session) return;

  promptInput.value = '';
  resizePromptInput();
  if (session.generating) {
    await queuePrompt(session, text);
    return;
  }
  await runGeneration(session, text);
}


// KaTeX (~300 KB) and highlight.js (~150 KB) are fetched on first use, not at
// page load. renderMarkdown and the code viewers keep their typeof checks, so
// content drawn before a library lands is re-rendered once it arrives.
const RICH_VENDOR = {
  katex: { js: '/vendor-katex.min.js', css: '/vendor-katex.min.css', ready: () => typeof katex !== 'undefined' && !!katex.renderToString },
  hljs: { js: '/vendor-hljs.min.js', ready: () => !!getHljs() }
};
const richVendorLoads = {};
function loadRichVendor(kind) {
  const spec = RICH_VENDOR[kind];
  if (spec.ready()) return Promise.resolve(true);
  // A failed load stays failed: the page keeps rendering plain text, as it did
  // when a vendor script was missing.
  if (!richVendorLoads[kind]) {
    const loads = [];
    if (spec.css) {
      loads.push(new Promise((resolve) => {
        const link = document.createElement('link');
        link.rel = 'stylesheet';
        link.href = spec.css;
        link.onload = () => resolve(true);
        link.onerror = () => resolve(false);
        document.head.appendChild(link);
      }));
    }
    loads.push(new Promise((resolve) => {
      const s = document.createElement('script');
      s.src = spec.js;
      s.onload = () => resolve(true);
      s.onerror = () => { s.remove(); resolve(false); };
      document.head.appendChild(s);
    }));
    richVendorLoads[kind] = Promise.all(loads).then(() => {
      const ok = spec.ready();
      if (ok) rerenderRichContent();
      return ok;
    });
  }
  return richVendorLoads[kind];
}
// Loads still in flight for the rich libraries this text needs (empty when none).
function richVendorJobsFor(text) {
  const jobs = [];
  if (typeof text !== 'string') return jobs;
  const needKatex = !RICH_VENDOR.katex.ready();
  const needHljs = !RICH_VENDOR.hljs.ready();
  if (!needKatex && !needHljs) return jobs;
  if (needKatex && (text.includes('$') || /\$|\\\[\(\)]/.test(text))) jobs.push(loadRichVendor('katex'));
  if (needHljs && (text.includes('```') || text.includes('~~~'))) jobs.push(loadRichVendor('hljs'));
  return jobs;
}

// Efficient check across session messages without concatenating multi-MB strings.
function richVendorJobsForMessages(messages) {
  const jobs = [];
  const needKatex = !RICH_VENDOR.katex.ready();
  const needHljs = !RICH_VENDOR.hljs.ready();
  if (!needKatex && !needHljs) return jobs;
  if (!Array.isArray(messages) || messages.length === 0) return jobs;

  let foundKatex = false;
  let foundHljs = false;
  for (let i = messages.length - 1; i >= 0; i--) {
    const m = messages[i];
    const text = m ? (m.content || '') : '';
    if (typeof text !== 'string' || !text) continue;
    if (needKatex && !foundKatex && (text.includes('$') || /\$|\\\[\(\)]/.test(text))) {
      foundKatex = true;
    }
    if (needHljs && !foundHljs && (text.includes('```') || text.includes('~~~'))) {
      foundHljs = true;
    }
    if ((!needKatex || foundKatex) && (!needHljs || foundHljs)) break;
  }
  if (needKatex && foundKatex) jobs.push(loadRichVendor('katex'));
  if (needHljs && foundHljs) jobs.push(loadRichVendor('hljs'));
  return jobs;
}

// Re-draw the open transcript once a library lands. A live stream is left
// alone; its final full render picks the library up.
function rerenderRichContent() {
  const cur = state.openSessions.find(s => s.id === state.sessionId);
  if (cur && !cur.stream && !cur.stub) {
    renderCache.nodes = [];
    renderMessages();
  }
}

// Mermaid is ~3.5 MB; fetch it on the first diagram instead of at page load.

// mermaid.render() is not safe to run concurrently: overlapping calls can clobber
// each other's output. Diagrams render one at a time through this queue, each
// with a unique element id, and a failure is logged and shown for that diagram
// only.
//
// The queue drains one diagram per idle callback, so a long chat with many
// diagrams does not hold the main thread for its whole backlog. Diagrams on
// screen go first. Total work is unchanged.
const mermaidQueue = [];
let mermaidQueueBusy = false;
let mermaidRenderSeq = 0;

function queueMermaidRender(targetId, code) {
  mermaidQueue.push({ targetId, code });
  if (mermaidQueueBusy) return;
  mermaidQueueBusy = true;
  scheduleMermaidSlice();
}

function scheduleMermaidSlice() {
  if (typeof requestIdleCallback === 'function') {
    requestIdleCallback(runMermaidSlice, { timeout: 300 });
  } else {
    setTimeout(runMermaidSlice, 16);
  }
}

// Take the next job whose target is on screen if one is near the front of the
// queue; otherwise take the oldest job.
function takeMermaidJob() {
  const vh = window.innerHeight;
  const scan = Math.min(mermaidQueue.length, 20);
  for (let i = 0; i < scan; i++) {
    const el = document.getElementById(mermaidQueue[i].targetId);
    if (!el) continue;
    const r = el.getBoundingClientRect();
    if (r.bottom > 0 && r.top < vh) return mermaidQueue.splice(i, 1)[0];
  }
  return mermaidQueue.shift();
}

async function runMermaidSlice() {
  const job = takeMermaidJob();
  if (job) {
    const target = document.getElementById(job.targetId);
    if (target) {
      try {
        const m = await loadMermaid();
        const { svg } = await m.render(`${job.targetId}-r${++mermaidRenderSeq}`, job.code);
        const again = document.getElementById(job.targetId);
        if (again) again.innerHTML = svg;
      } catch (err) {
        console.error('mermaid render failed for', job.targetId, err);
        const again = document.getElementById(job.targetId);
        if (again) again.innerHTML = '<div class="diagram-error">Mermaid syntax error: ' + esc(err.message || err) + '</div>';
      }
    }
  }
  if (mermaidQueue.length) {
    scheduleMermaidSlice();
  } else {
    mermaidQueueBusy = false;
  }
}

let mermaidLoadPromise = null;
function loadMermaid() {
  if (typeof mermaid !== 'undefined' && mermaid.render) return Promise.resolve(mermaid);
  if (!mermaidLoadPromise) {
    mermaidLoadPromise = new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = '/vendor-mermaid.min.js';
      s.onload = () => {
        if (typeof mermaid !== 'undefined' && mermaid.render) resolve(mermaid);
        else reject(new Error('Mermaid did not initialize'));
      };
      s.onerror = () => {
        s.remove();
        mermaidLoadPromise = null;
        reject(new Error('Mermaid failed to load'));
      };
      document.head.appendChild(s);
    });
  }
  return mermaidLoadPromise;
}

// While the user drags a selection in the chat, renders wait for the mouse
// to come up: replacing DOM under a drag-selection drops it, and following
// new output would yank the text away from the pointer.
let chatSelecting = false;
const deferredRenders = { full: null, stream: null };
function flushDeferredRenders() {
  chatSelecting = false;
  const { full, stream } = deferredRenders;
  deferredRenders.full = deferredRenders.stream = null;
  if (full) full();
  else if (stream) stream();
}

// Coalesces bursts of full rebuilds (tool/usage events) into one render + scroll per frame.
let fullRenderPending = false;
function scheduleFullRender() {
  if (fullRenderPending) return;
  fullRenderPending = true;
  requestAnimationFrame(() => {
    fullRenderPending = false;
    if (chatSelecting) { deferredRenders.full = scheduleFullRender; return; }
    renderMessages();
    scrollToBottom();
  });
}

// Streaming renders are coalesced to one per frame, and spaced further apart
// when re-rendering the reply gets expensive (long markdown), so typing,
// scrolling and the caret stay smooth on large answers.
let streamRenderPending = false;
let streamRenderCost = 0;
function scheduleStreamingRender(session, msg) {
  if (streamRenderPending) return;
  streamRenderPending = true;
  const run = () => requestAnimationFrame(() => {
    streamRenderPending = false;
    // A render queued behind the stream's end must not re-stream the final one.
    if (session.id !== state.sessionId || !session.stream || session.stream.msg !== msg) return;
    if (chatSelecting) { deferredRenders.stream = () => scheduleStreamingRender(session, msg); return; }
    const t0 = performance.now();
    renderStreamingIncremental(msg);
    streamRenderCost = performance.now() - t0;
  });
  const delay = Math.min(240, streamRenderCost * 3);
  if (delay > 16) setTimeout(run, delay); else run();
}

// Which blocks renderMessage() produces for `msg`; equal shapes mean the
// streaming fast path can patch the last block in place.
function messageShape(msg) {
  const tl = Array.isArray(msg.timeline) ? msg.timeline : [];
  let shape = '';
  let segments = false;
  for (const e of tl) {
    if (e.kind === 'tool') shape += 'T';
    else if (e.kind === 'thought' && e.text && state.showThinking) shape += 'R';
    else if (e.kind === 'text' && e.text) { shape += 'X'; segments = true; }
  }
  if (!segments && msg.content) shape += 'C';
  if (msg.streamError) shape += 'E';
  if (msg.stoppedEarly) shape += 'S';
  return shape;
}

function renderStreamingIncremental(msg) {
  // A queued-prompt block is always the last child; look past it for the message.
  let lastChild = messagesEl.lastElementChild;
  if (lastChild && lastChild.classList.contains('queued-block')) lastChild = lastChild.previousElementSibling;
  if (!lastChild || !lastChild.classList.contains('assistant') || lastChild._msgRef !== msg
      || lastChild._shape !== messageShape(msg)) {
    renderMessages();
    scrollToBottom();
    return;
  }
  // Fast path: only the trailing thought or text block grew.
  const cacheEntry = renderCache.nodes[renderCache.nodes.length - 1];
  const tl = Array.isArray(msg.timeline) ? msg.timeline : [];
  const last = tl[tl.length - 1];
  if (last && last.kind === 'thought' && state.showThinking) {
    const blocks = lastChild.querySelectorAll('.reasoning-block .reasoning-text');
    const el = blocks[blocks.length - 1];
    if (el) renderMarkdownStreaming(el, last.text || '', { decorate: false });
  } else {
    const blocks = lastChild.querySelectorAll('.md-content');
    const el = blocks[blocks.length - 1];
    const text = (last && last.kind === 'text') ? last.text : msg.content;
    if (el && text) renderMarkdownStreaming(el, text);
  }
  const session = state.openSessions.find(x => x.id === state.sessionId);
  if (session && cacheEntry && cacheEntry.el === lastChild) {
    cacheEntry.sig = renderSignature(session, msg, session.messages.length - 1);
  }
  scrollToBottom();
}

function handleEvent(evt, msg, session) {
  switch (evt.type) {
    case 'session.started':
      msg.sessionId = evt.session_id;
      session.id = evt.session_id;
      if (session.id === state.sessionId) {
        state.sessionId = evt.session_id;
        updateStatusBar();
        renderSessionTabs();
      }
      break;
    case 'backend.heartbeat':
      // Periodic keepalive from server — stream is healthy
      break;
    case 'backend.task.status.changed':
      // A subagent of this session started, finished or was killed.
      if (evt.lead_session_id === state.sessionId) scheduleDelegatedRefresh(150);
      break;
    case 'backend.message.delta':
      // Append (do NOT overwrite): backend sends incremental chunks and a
      // final empty-text delta with done=true, which would otherwise wipe
      // the accumulated assistant text (leaving only the token usage line).
      if (evt.text) {
        appendTextChunk(msg, evt.text);
        msg.streamError = null;
      }
      if (session.id === state.sessionId) {
        scheduleStreamingRender(session, msg);
      }
      break;
    case 'backend.reasoning.delta':
      if (evt.text) {
        appendThoughtChunk(msg, evt.text);
        msg.streamError = null;
      }
      if (session.id === state.sessionId) {
        scheduleStreamingRender(session, msg);
      }
      break;
    case 'backend.step.latency':
      // Each model call is priced and persisted before this event: an open
      // Stats tab follows the turn call by call.
      if (session.id === state.sessionId && state.activeTab === 'stats') scheduleStatsRefresh();
      break;
    case 'backend.token.usage.updated': {
      const parts = [];
      if (evt.prompt_tokens != null && evt.prompt_tokens > 0) parts.push('prompt ' + evt.prompt_tokens);
      if (evt.completion_tokens != null && evt.completion_tokens > 0) parts.push('completion ' + evt.completion_tokens);
      if (evt.total_tokens != null && evt.total_tokens > 0) parts.push('total ' + evt.total_tokens);
      if (evt.cached_prompt_tokens != null && evt.cached_prompt_tokens > 0) parts.push('cached ' + evt.cached_prompt_tokens);
      if (evt.reasoning_tokens != null && evt.reasoning_tokens > 0) parts.push('thinking ' + evt.reasoning_tokens);
      else if (state.reasoning !== 'off') parts.push('thinking 0');
      if (parts.length > 0) {
        msg.usage = 'Tokens — ' + parts.join(' · ');
        if (session.id === state.sessionId) scheduleFullRender();
      }
      break;
    }
    case 'backend.tool.call.started':
      if (!msg.toolEvents) msg.toolEvents = [];
      closeOpenThought(msg);
      msg.toolEvents.push({ type: 'tool_call', tool_call_id: evt.tool_call_id, tool_name: evt.tool_name, arguments: evt.arguments, status: 'running' });
      if (!msg.timeline) msg.timeline = [];
      msg.timeline.push({ kind: 'tool', tool_call_id: evt.tool_call_id });
      if (session.id === state.sessionId) scheduleFullRender();
      break;
    case 'backend.tool.call.completed':
      if (msg.toolEvents) {
        const tc = msg.toolEvents.find(t => t.tool_call_id === evt.tool_call_id);
        if (tc) {
          tc.status = evt.is_error ? 'error' : 'success';
          tc.result = evt.result;
          tc.duration_ms = evt.duration_ms;
        }
      }
      if (session.id === state.sessionId) scheduleFullRender();
      break;
    case 'backend.error.occurred':
      if (evt.severity === 'info') {
        // Heartbeat or status notice — do not flag generation as an error
        break;
      }
      if (evt.severity === 'warning' || evt.severity === 'warn') {
        // Warning diagnostic (e.g. slow request TTFT wait) — show transient toast, do not abort/flag stream as error
        showToast(evt.message);
        break;
      }
      showToast(evt.message);
      msg.streamError = evt.message || 'The backend reported an error';
      if (session.id === state.sessionId) renderMessages();
      break;
    case 'generation.complete':
      if (evt.error) {
        // T1.2: surface backend error inline even when partial text exists.
        msg.streamError = evt.error;
        msg.stoppedEarly = true;
        showToast('Generation failed: ' + evt.error);
      } else {
        msg.streamError = null;
        msg.stoppedEarly = false;
      }
      if (session.id === state.sessionId) renderMessages();
      break;
  }
}

// ═══════════════════════════════════════════════════════════════════
//  RENDERING
// ═══════════════════════════════════════════════════════════════════

function switchFilesSubtab(sub) {
  state.filesSubtab = sub === 'git' ? 'git' : 'explorer';
  if (filesSubtabExplorer) filesSubtabExplorer.classList.toggle('active', state.filesSubtab === 'explorer');
  if (filesSubtabGit) filesSubtabGit.classList.toggle('active', state.filesSubtab === 'git');
  if (filesExplorer) filesExplorer.classList.toggle('hidden', state.filesSubtab !== 'explorer');
  if (filesGit) filesGit.classList.toggle('hidden', state.filesSubtab !== 'git');
  loadFilesTab();
}

async function loadFilesTab() {
  if (state.filesSubtab === 'git') {
    await loadGitChanges();
  } else {
    await loadFsListing(state.fsDir || '');
  }
}

async function loadGitChanges() {
  if (!filesGit) return;
  if (!state.sessionId) {
    filesGit.innerHTML = '<div class="files-empty">No active session. Select or create one to see its working-tree changes.</div>';
    return;
  }
  filesGit.innerHTML = '<div class="files-empty">Loading working tree…</div>';
  try {
    const res = await fetch('/session/' + state.sessionId + '/files');
    if (!res.ok) throw new Error(await responseError(res));
    const data = await res.json();
    renderGitChanges(data);
  } catch (e) {
    filesGit.innerHTML = '<div class="files-empty">Could not load changes: ' + esc(e.message) + '</div>';
  }
}

function renderGitChanges(data) {
  if (!filesGit) return;
  const ws = data.workspace || '';
  let html = '';
  html += '<div class="files-workspace">' + SVG_ICONS.folder + '<span>' + esc(ws) + '</span></div>';

  if (!data.is_git_repo) {
    html += '<div class="files-empty">Not a git repository. Initialize one with <code>git init</code> to see working-tree diffs here.</div>';
    filesGit.innerHTML = html;
    return;
  }

  const ins = data.insertions || 0;
  const del = data.deletions || 0;
  const untracked = data.untracked || 0;
  const modified = data.modified || 0;
  const staged = data.staged || 0;
  html += '<div class="files-summary">'
    + '<span class="chip"><span class="add">+' + ins + '</span> / <span class="del">-' + del + '</span></span>'
    + '<span class="chip">Modified: ' + modified + '</span>'
    + '<span class="chip">Staged: ' + staged + '</span>'
    + '<span class="chip">Untracked: ' + untracked + '</span>'
    + '</div>';

  const files = Array.isArray(data.files) ? data.files : [];
  if (files.length > 0) {
    html += '<div class="file-list">';
    for (const f of files) {
      const type = f.type || 'modified';
      const label = type.charAt(0).toUpperCase() + type.slice(1);
      const fins = f.insertions || 0;
      const fdel = f.deletions || 0;
      const counts = (fins || fdel)
        ? ' <span class="file-counts"><span class="add">+' + fins + '</span> <span class="del">-' + fdel + '</span></span>'
        : '';
      const path = f.path || '';
      html += '<div class="file-row file-row-clickable" data-open-path="' + esc(path) + '" title="Open in editor">'
        + '<span class="file-type ' + esc(type) + '">' + esc(label) + '</span>'
        + '<span class="file-path">' + esc(path) + '</span>'
        + counts
        + '</div>';
    }
    html += '</div>';
  }

  const diff = data.diff || '';
  if (diff.trim().length > 0) {
    html += '<div class="stat-section-title diff-toggle" tabindex="0">Unified diff</div>';
    const diffHtml = diff.split('\n').map((line) => {
      let cls = '';
      if (line.startsWith('+++') || line.startsWith('---') || line.startsWith('diff --git')) cls = 'meta';
      else if (line.startsWith('@@')) cls = 'hunk';
      else if (line.startsWith('+')) cls = 'add';
      else if (line.startsWith('-')) cls = 'del';
      return '<span class="diff-line' + (cls ? ' ' + cls : '') + '">' + (esc(line) || ' ') + '</span>';
    }).join('');
    html += '<div class="diff-view diff-block"><pre>' + diffHtml + '</pre></div>';
  } else if (files.length === 0) {
    html += '<div class="files-empty">Working tree clean — no modified files.</div>';
  }

  filesGit.innerHTML = html;
  filesGit.querySelectorAll('.diff-toggle').forEach((t) => {
    const go = () => { const d = filesGit.querySelector('.diff-view'); if (d) d.classList.toggle('hidden'); };
    t.addEventListener('click', go);
    t.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); go(); } });
  });
  filesGit.querySelectorAll('.file-row-clickable').forEach((row) => {
    row.addEventListener('click', () => {
      const p = row.getAttribute('data-open-path');
      if (!p) return;
      switchFilesSubtab('explorer');
      openFsFile(p);
    });
  });
}

// ── Workspace filesystem browser + viewer/editor (GitHub Mobile Style) ──

// Nerd Font (Symbols Nerd Font Mono) glyphs for file types; codepoints are
// from the bundled font's cmap (fa-*, dev-*, seti-*, custom-*).
const NF = {
  folder: '', file: '', text: '', image: '', pdf: '', archive: '', code: '',
  js: '', ts: '', py: '', cpp: '', c: '', md: '', html: '', css: '',
  sh: '', rs: '', go: '', java: '', yml: '', config: '', swift: '',
  kotlin: '', make: '', cmake: '', git: '', react: '', toml: '', db: '', up: ''
};
function getFsFileIcon(path, isDir) {
  if (isDir) return NF.folder;
  const name = (path || '').split('/').pop() || '';
  const lower = name.toLowerCase();
  if (lower === 'cmakelists.txt' || lower.endsWith('.cmake')) return NF.cmake;
  if (lower === 'makefile' || lower === 'gnumakefile') return NF.make;
  if (lower.startsWith('.git')) return NF.git;
  if (lower === 'dockerfile' || lower.startsWith('dockerfile.')) return NF.config;

  const dot = lower.lastIndexOf('.');
  const ext = dot >= 0 ? lower.slice(dot + 1) : '';
  switch (ext) {
    case 'js': case 'mjs': case 'cjs': return NF.js;
    case 'jsx': case 'tsx': return NF.react;
    case 'ts': case 'mts': case 'cts': return NF.ts;
    case 'py': case 'pyw': return NF.py;
    case 'cpp': case 'cxx': case 'cc': case 'hpp': case 'hh': case 'hxx': return NF.cpp;
    case 'c': case 'h': return NF.c;
    case 'html': case 'htm': case 'xml': case 'svg': return NF.html;
    case 'css': case 'scss': case 'less': return NF.css;
    case 'json': case 'jsonc': case 'ini': case 'conf': case 'cfg': return NF.config;
    case 'yaml': case 'yml': return NF.yml;
    case 'toml': return NF.toml;
    case 'md': case 'markdown': return NF.md;
    case 'sh': case 'bash': case 'zsh': case 'fish': return NF.sh;
    case 'png': case 'jpg': case 'jpeg': case 'gif': case 'webp': case 'ico': case 'bmp': return NF.image;
    case 'rs': return NF.rs;
    case 'go': return NF.go;
    case 'java': return NF.java;
    case 'kt': case 'kts': return NF.kotlin;
    case 'swift': return NF.swift;
    case 'pdf': return NF.pdf;
    case 'zip': case 'gz': case 'tgz': case 'xz': case 'zst': case 'tar': return NF.archive;
    case 'db': case 'sqlite': case 'sql': return NF.db;
    case 'txt': case 'log': return NF.text;
    default: return NF.file;
  }
}

function isMarkdownFile(path) {
  if (!path || typeof path !== 'string') return false;
  const lower = path.toLowerCase();
  return lower.endsWith('.md') || lower.endsWith('.markdown');
}
function setFsMobileView(view) {
  state.fsMobileView = view;
  if (filesExplorer) {
    filesExplorer.classList.toggle('mobile-show-viewer', view === 'viewer');
  }
}

function setFsViewMode(mode) {
  state.fsViewMode = mode;
  if (fsViewPlaceholder) fsViewPlaceholder.classList.toggle('hidden', mode !== 'placeholder');
  if (fsViewCode) fsViewCode.classList.toggle('hidden', mode !== 'code');
  if (fsViewMarkdown) fsViewMarkdown.classList.toggle('hidden', mode !== 'markdown');
  if (fsViewImage) fsViewImage.classList.toggle('hidden', mode !== 'image');
  if (fsViewBinary) fsViewBinary.classList.toggle('hidden', mode !== 'binary');
  if (fsViewEditor) fsViewEditor.classList.toggle('hidden', mode !== 'editor');
  if (fsEditBtn) {
    fsEditBtn.textContent = mode === 'editor' ? 'Read' : 'Edit';
  }
  if (fsSaveBtn) {
    fsSaveBtn.classList.toggle('hidden', mode !== 'editor');
  }
}

function setFsDirty(dirty) {
  state.fsDirty = !!dirty;
  if (fsDirtyBadge) fsDirtyBadge.classList.toggle('hidden', !state.fsDirty);
  if (fsSaveBtn) {
    fsSaveBtn.disabled = !state.fsOpenPath || !state.fsDirty;
    fsSaveBtn.classList.toggle('hidden', state.fsViewMode !== 'editor');
  }
  if (fsEditorPath && state.fsOpenPath) {
    fsEditorPath.textContent = state.fsOpenPath + (state.fsDirty ? ' •' : '');
  }
}



function renderFsBreadcrumb(relPath) {
  if (!fsBreadcrumb) return;
  const parts = relPath ? relPath.split('/').filter(Boolean) : [];
  let html = '<button type="button" class="fs-crumb" data-path="">root</button>';
  let acc = '';
  for (const part of parts) {
    acc = acc ? acc + '/' + part : part;
    html += '<span class="fs-crumb-sep">/</span>';
    html += '<button type="button" class="fs-crumb" data-path="' + esc(acc) + '">' + esc(part) + '</button>';
  }
  fsBreadcrumb.innerHTML = html;
  fsBreadcrumb.querySelectorAll('.fs-crumb').forEach((btn) => {
    btn.addEventListener('click', () => {
      const p = btn.getAttribute('data-path') || '';
      loadFsListing(p);
    });
  });
}

async function loadFsListing(relPath) {
  if (!fsListing) return;
  if (!state.sessionId) {
    fsListing.innerHTML = '<div class="files-empty">No active session. Select or create one to browse its files.</div>';
    if (fsBreadcrumb) fsBreadcrumb.innerHTML = '';
    return;
  }

  if (relPath === state.fsDir && state.fsListingRunIndex === (state.fsListingRunIndex || 0)) {
    // no-op dedupe: path already loaded
    const dedupePath = state.fsDir;
    if (dedupePath === relPath && state.fsRawEntries.length > 0) {
      renderFsListingEntries();
      return;
    }
  }

  state.fsDir = relPath || '';
  renderFsBreadcrumb(state.fsDir);
  fsListing.innerHTML = '<div class="files-empty loading-spinner"><span>Loading…</span></div>';
  const runIndex = (state.fsListingRunIndex || 0) + 1;
  state.fsListingRunIndex = runIndex;
  try {
    const q = state.fsDir ? ('?path=' + encodeURIComponent(state.fsDir)) : '';
    const res = await fetch('/session/' + state.sessionId + '/fs/list' + q);
    const data = await res.json().catch(() => ({}));
    if (!res.ok) {
      if (res.status === 404 && (data.error === 'directory not found' || data.error === 'not a directory')) {
        if (runIndex !== state.fsListingRunIndex) return;
        state.fsReady = true;
        state.fsRawEntries = [];
        const wsDisplay = data.workspace || state.sessionWorkspace || 'workspace';
        fsListing.innerHTML = `
          <div class="files-empty fs-workspace-missing">
            <div class="files-empty-title" style="font-weight:600;margin-bottom:6px;">Folder not found</div>
            <div class="files-empty-desc" style="font-size:var(--text-sm);color:var(--muted);line-height:1.4;">The directory <code style="font-family:var(--font-mono);background:var(--surface);padding:2px 6px;border-radius:4px;">${esc(wsDisplay)}</code> does not exist on disk.</div>
          </div>`;
        return;
      }
      throw new Error(data.error || res.statusText || 'list failed');
    }
    if (runIndex !== state.fsListingRunIndex) return; // stale response
    state.fsReady = true;
    state.fsRawEntries = Array.isArray(data.entries) ? data.entries : [];
    renderFsListingEntries();
  } catch (e) {
    if (runIndex === state.fsListingRunIndex) {
      fsListing.innerHTML = '<div class="files-empty">Failed to list directory: ' + esc(e.message) + '</div>';
    }
  }
}

function renderFsListingEntries() {
  if (!fsListing) return;
  const filter = (state.fsFilterText || '').toLowerCase().trim();
  let entries = state.fsRawEntries;

  if (filter) {
    entries = entries.filter(e => (e.name || '').toLowerCase().includes(filter));
  }

  let html = '';
  if (state.fsDir && !filter) {
    const parent = state.fsDir.includes('/')
      ? state.fsDir.slice(0, state.fsDir.lastIndexOf('/'))
      : '';
    html += '<button type="button" class="fs-entry" data-type="dir" data-path="' + esc(parent) + '">'
      + '<span class="fs-icon nf">' + NF.up + '</span>'
      + '<span class="fs-name">..</span>'
      + '</button>';
  }

  if (entries.length === 0) {
    html += '<div class="files-empty">' + (filter
      ? 'No files match “' + esc((state.fsFilterText || '').trim()) + '”. Clear the filter to show all files.'
      : 'This folder is empty.') + '</div>';
  } else {
    for (const e of entries) {
      const isDir = e.type === 'dir';
      const icon = getFsFileIcon(e.path || e.name, isDir);
      const size = (!isDir && e.size != null) ? '<span class="fs-size">' + formatBytes(e.size) + '</span>' : '';
      const active = (!isDir && state.fsOpenPath === e.path) ? ' active' : '';
      const linkCls = (!isDir && state.sessionId) ? ' md-internal-link' : '';
      html += '<button type="button" class="fs-entry' + active + linkCls + '" data-type="' + (isDir ? 'dir' : 'file')
        + '" data-path="' + esc(e.path || '') + '">'
        + '<span class="fs-icon nf' + (isDir ? ' dir' : '') + '">' + icon + '</span>'
        + '<span class="fs-name">' + esc(e.name || '') + '</span>'
        + size
        + '</button>';
    }
  }

  fsListing.innerHTML = html;
  fsListing.querySelectorAll('.fs-entry').forEach((btn) => {
    btn.addEventListener('click', () => {
      const type = btn.getAttribute('data-type');
      const path = btn.getAttribute('data-path') || '';
      if (type === 'dir') loadFsListing(path);
      else { openFsFile(path); document.querySelectorAll('.fs-entry.active').forEach(el => el.classList.remove('active')); btn.classList.add('active'); }
    });
  });

  // Fill the cross-file resolution cache with full paths for every listing.
  if (state.sessionId && entries.length > 0 && state.fsReady !== false) {
    const runCache = state.fsCache[state.sessionId] || (state.fsCache[state.sessionId] = {});
    entries.forEach((e) => { if (e.path && !runCache[e.path]) runCache[e.path] = { full_path: e.path }; });
  }
}

function updateLineNumbers(codeText) {
  if (!fsLineNumbers) return;
  const count = (codeText || '').split('\n').length || 1;
  let nums = '';
  for (let i = 1; i <= count; i++) {
    nums += i + '\n';
  }
  fsLineNumbers.textContent = nums;
}

function updateCodeViewer(content, lang) {
  setFsViewMode('code');
  updateLineNumbers(content);

  if (fsCodeHljs) {
    const engine = getHljs();
    const knownLang = lang && engine && engine.getLanguage(lang) ? lang : '';
    if (engine && knownLang) {
      try {
        fsCodeHljs.innerHTML = engine.highlight(content, { language: knownLang, ignoreIllegals: true }).value;
      } catch (e) {
        fsCodeHljs.textContent = content;
      }
    } else if (engine && content.length > 0 && content.length < 200000) {
      try {
        fsCodeHljs.innerHTML = engine.highlightAuto(content).value;
      } catch (e) {
        fsCodeHljs.textContent = content;
      }
    } else {
      fsCodeHljs.textContent = content;
    }
  }

  if (fsViewCode) {
    fsViewCode.classList.toggle('wrap-lines', !!state.fsLineWrap);
  }
  if (fsCodeWrapper) {
    fsCodeWrapper.classList.toggle('wrap-lines', !!state.fsLineWrap);
  }
}

function updateMarkdownViewer(content) {
  if (state.fsMdMode !== 'preview') {
    updateCodeViewer(content, 'markdown');
    return;
  }
  setFsViewMode('markdown');
  if (!fsViewMarkdown) return;
  try {
    fsViewMarkdown.innerHTML = renderMarkdown(content);
    bindFrontmatterToggles(fsViewMarkdown);
    tagMarkdownLinks(fsViewMarkdown);
  } catch (err) {
    fsViewMarkdown.textContent = content;
  }
}


function updateImageViewer(relPath, size) {
  setFsViewMode('image');
  const rawUrl = '/session/' + state.sessionId + '/fs/raw?path=' + encodeURIComponent(relPath);
  if (fsImagePreview) {
    fsImagePreview.src = rawUrl;
    fsImagePreview.onload = () => {
      if (fsImageInfo) {
        fsImageInfo.textContent = fsImagePreview.naturalWidth + ' × ' + fsImagePreview.naturalHeight + ' px · ' + (size ? formatBytes(size) : '');
      }
    };
  }
}

function updateBinaryViewer(relPath, size) {
  setFsViewMode('binary');
  const rawUrl = '/session/' + state.sessionId + '/fs/raw?path=' + encodeURIComponent(relPath);
  if (fsBinaryText) {
    fsBinaryText.textContent = 'Binary file (' + (size ? formatBytes(size) : '') + ') cannot be displayed inline.';
  }
  if (fsBinaryDownloadLink) {
    fsBinaryDownloadLink.href = rawUrl;
  }
}

function toggleFsEditMode() {
  if (!state.fsOpenPath) return;
  if (state.fsViewMode === 'editor') {
    const isMd = isMarkdownFile(state.fsOpenPath);
    const content = (fsEditor && fsEditor.value != null) ? fsEditor.value : state.fsSavedContent;
    if (isMd) {
      state.fsMdMode = 'preview';
      if (fsMdPreviewBtn) fsMdPreviewBtn.classList.add('active');
      if (fsMdCodeBtn) fsMdCodeBtn.classList.remove('active');
      updateMarkdownViewer(content);
    } else {
      updateCodeViewer(content, fsLang);
    }
  } else {
    const content = (fsEditor && state.fsDirty) ? fsEditor.value : state.fsSavedContent;
    setFsViewMode('editor');
    setFsEditorContent(content, state.fsOpenPath);
  }
  setFsDirty(state.fsDirty);
}

async function confirmDiscardIfDirty() {
  if (!state.fsDirty) return true;
  return window.confirm('You have unsaved changes. Discard them?');
}

// Map file path → highlight.js language id (empty = plain / auto).


function syncFsHighlightScroll() {
  if (!fsEditor || !fsHighlight) return;
  fsHighlight.scrollTop = fsEditor.scrollTop;
  fsHighlight.scrollLeft = fsEditor.scrollLeft;
}

function applyFsPlainOverlay(source) {
  if (!fsHighlightCode || !fsEditor) return;
  fsHighlightCode.textContent = source;
  if (source.endsWith('\n')) fsHighlightCode.appendChild(document.createTextNode('\n'));
  fsHighlightCode.className = 'hljs';
  fsEditor.classList.remove('fs-editor-plain');
  fsEditor.classList.add('fs-editor-highlighting');
}

function updateFsHighlight() {
  if (!fsEditor || !fsHighlightCode) return;
  const text = fsEditor.value;
  const plainMode = !state.fsOpenPath || fsEditor.disabled;
  const engine = getHljs();

  if (plainMode) {
    fsEditor.classList.add('fs-editor-plain');
    fsEditor.classList.remove('fs-editor-highlighting');
    fsHighlightCode.textContent = '';
    fsHighlightCode.className = 'hljs';
    return;
  }

  const source = text;
  if (!engine) {
    applyFsPlainOverlay(source);
    return;
  }

  try {
    const knownLang = fsLang && engine.getLanguage(fsLang) ? fsLang : '';
    let result = null;
    if (knownLang) {
      result = engine.highlight(source, { language: knownLang, ignoreIllegals: true });
    } else if (source.length > 0 && source.length < 200000) {
      result = engine.highlightAuto(source);
    }
    if (result && result.value) {
      fsHighlightCode.innerHTML = result.value;
      if (source.endsWith('\n')) fsHighlightCode.appendChild(document.createTextNode('\n'));
      fsHighlightCode.className = 'hljs' + (result.language ? (' language-' + result.language) : '');
      fsEditor.classList.remove('fs-editor-plain');
      fsEditor.classList.add('fs-editor-highlighting');
    } else {
      applyFsPlainOverlay(source);
    }
  } catch (e) {
    applyFsPlainOverlay(source);
  }
}

function getHljs() {
  if (typeof window !== 'undefined' && window.hljs) return window.hljs;
  if (typeof hljs !== 'undefined') return hljs;
  return null;
}

function setFsEditorContent(content, path) {
  fsLang = detectFsLanguage(path);
  if (fsEditor) {
    fsEditor.value = content;
    fsEditor.disabled = false;
  }
  updateFsHighlight();
  syncFsHighlightScroll();
}

function clearFsEditor() {
  if (fsEditor) {
    fsEditor.value = '';
    fsEditor.disabled = true;
  }
  state.fsOpenPath = null;
  state.fsSavedContent = '';
  setFsViewMode('placeholder');
  if (fsFileIcon) fsFileIcon.textContent = '📄';
  if (fsFileBadge) fsFileBadge.textContent = '';
  if (fsCopyPathBtn) fsCopyPathBtn.disabled = true;
  if (fsCopyContentBtn) fsCopyContentBtn.disabled = true;
  if (fsWrapBtn) fsWrapBtn.disabled = true;
  if (fsEditBtn) fsEditBtn.disabled = true;
  if (fsRawBtn) fsRawBtn.classList.add('hidden');
  if (fsMdToggle) fsMdToggle.classList.add('hidden');
  updateFsHighlight();
}

function fsTrackTab(path) {
  state.fsTabs = state.fsTabs || [];
  if (path && !state.fsTabs.includes(path)) state.fsTabs.push(path);
  renderFsTabs();
}
function renderFsTabs() {
  let bar = document.getElementById('fs-open-tabs');
  if (!bar) {
    const pane = document.getElementById('fs-browser-pane') || document.getElementById('files-explorer');
    if (!pane) return;
    bar = document.createElement('div'); bar.id = 'fs-open-tabs'; /* id="fs-open-tabs" */ bar.className = 'open-tabs';
    pane.prepend(bar);
  }
  bar.innerHTML = '';
  for (const t of (state.fsTabs || [])) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'open-tab' + (t === state.fsOpenPath ? ' active' : '');
    b.textContent = t.split('/').pop(); b.title = t;
    b.addEventListener('click', () => openFsFile(t));
    const x = document.createElement('span'); x.className = 'open-tab-x'; x.textContent = ' ×';
    x.addEventListener('click', (e) => { e.stopPropagation(); state.fsTabs = (state.fsTabs||[]).filter(z => z !== t); renderFsTabs(); });
    b.appendChild(x); bar.appendChild(b);
  }
}
async function openFsFile(relPath, fragment = null) {
  if (!relPath || !state.sessionId) return;
  if (state.fsOpenPath === relPath && !state.fsDirty) {
    setFsMobileView('viewer');
    if (fragment) scrollToMarkdownHeading(fragment);
    return;
  }
  if (!(await confirmDiscardIfDirty())) return;

  const name = relPath.split('/').pop() || '';
  const lower = name.toLowerCase();
  const ext = lower.lastIndexOf('.') >= 0 ? lower.slice(lower.lastIndexOf('.') + 1) : '';
  const isImage = ['png', 'jpg', 'jpeg', 'gif', 'webp', 'ico', 'bmp', 'svg'].includes(ext);
  const isMd = isMarkdownFile(relPath);

  state.fsOpenPath = relPath;
  fsTrackTab(relPath);
  fsLang = detectFsLanguage(relPath);

  if (fsFileIcon) fsFileIcon.textContent = getFsFileIcon(relPath, false);
  if (fsEditorPath) fsEditorPath.textContent = relPath;
  if (fsCopyPathBtn) fsCopyPathBtn.disabled = false;
  if (fsCopyContentBtn) fsCopyContentBtn.disabled = isImage;
  if (fsWrapBtn) fsWrapBtn.disabled = false;
  if (fsEditBtn) {
    fsEditBtn.disabled = isImage;
    fsEditBtn.textContent = 'Edit';
  }
  if (fsCloseBtn) fsCloseBtn.disabled = false;

  const rawUrl = '/session/' + state.sessionId + '/fs/raw?path=' + encodeURIComponent(relPath);
  if (fsRawBtn) {
    fsRawBtn.href = rawUrl;
    fsRawBtn.classList.remove('hidden');
  }

  if (fsMdToggle) {
    fsMdToggle.classList.toggle('hidden', !isMd);
    if (isMd) {
      state.fsMdMode = 'preview';
      if (fsMdPreviewBtn) fsMdPreviewBtn.classList.add('active');
      if (fsMdCodeBtn) fsMdCodeBtn.classList.remove('active');
    }
  }

  if (fsListing) {
    fsListing.querySelectorAll('.fs-entry').forEach((el) => {
      el.classList.toggle('active', el.getAttribute('data-path') === state.fsOpenPath);
    });
  }

  setFsMobileView('viewer');

  if (isImage) {
    state.fsSavedContent = '';
    setFsDirty(false);
    if (fsFileBadge) fsFileBadge.textContent = ext.toUpperCase();
    updateImageViewer(relPath, null);
    if (fsEditorStatus) fsEditorStatus.textContent = 'Image file';
    return;
  }

  if (fsEditorStatus) { fsEditorStatus.classList.add('loading-spinner'); fsEditorStatus.textContent = 'Loading…'; }
  try {
    const readyState = await fetch('/session/' + state.sessionId + '/fs/list',
      { method: 'HEAD', cache: 'no-store' }).then(r => r.ok).catch(() => false);
    if (readyState) state.fsReady = true;
    else state.fsReady = false;
    const res = await fetch('/session/' + state.sessionId + '/fs/read?path=' + encodeURIComponent(relPath));
    const data = await res.json().catch(() => ({}));

    if (res.status === 415 || data.is_binary) {
      state.fsSavedContent = '';
      setFsDirty(false);
      if (fsFileBadge) fsFileBadge.textContent = formatBytes(data.size || 0);
      updateBinaryViewer(relPath, data.size);
      if (fsEditorStatus) fsEditorStatus.textContent = 'Binary file';
      return;
    }

    if (!res.ok) throw new Error(data.error || res.statusText || 'read failed');

    state.fsSavedContent = data.content || '';
    await Promise.all([loadRichVendor('hljs'), ...richVendorJobsFor(state.fsSavedContent)]);
    setFsEditorContent(state.fsSavedContent, state.fsOpenPath);
    setFsDirty(false);

    const lineCount = (state.fsSavedContent.split('\n')).length;
    const szStr = data.size != null ? formatBytes(data.size) : formatBytes(state.fsSavedContent.length);
    if (fsFileBadge) fsFileBadge.textContent = szStr + ' · ' + lineCount + ' lines · ' + (fsLang || 'text');

    if (isMd) {
      updateMarkdownViewer(state.fsSavedContent);
      if (fragment) setTimeout(() => scrollToMarkdownHeading(fragment), 60);
    } else {
      updateCodeViewer(state.fsSavedContent, fsLang);
    }

    if (fsEditorStatus) fsEditorStatus.textContent = szStr + ' · ' + lineCount + ' lines · ' + (fsLang || 'text');
  } catch (e) {
    if (fsEditorStatus) fsEditorStatus.textContent = 'Error: ' + e.message;
    showToast('Open failed: ' + e.message);
  }
}

async function saveOpenFile() {
  if (!state.sessionId || !state.fsOpenPath || !fsEditor) return;
  if (!state.fsDirty) return;
  if (fsEditorStatus) fsEditorStatus.textContent = 'Saving…';
  if (fsSaveBtn) fsSaveBtn.disabled = true;
  try {
    const res = await fetch('/session/' + state.sessionId + '/fs/write', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ path: state.fsOpenPath, content: fsEditor.value })
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(data.error || res.statusText || 'save failed');
    state.fsSavedContent = fsEditor.value;
    setFsDirty(false);

    const lineCount = (state.fsSavedContent.split('\n')).length;
    const szStr = data.size != null ? formatBytes(data.size) : formatBytes(state.fsSavedContent.length);
    if (fsFileBadge) fsFileBadge.textContent = szStr + ' · ' + lineCount + ' lines · ' + (fsLang || 'text');
    if (fsEditorStatus) fsEditorStatus.textContent = 'Saved · ' + szStr + ' · ' + (fsLang || 'text');

    const isMd = isMarkdownFile(state.fsOpenPath);
    if (isMd && state.fsViewMode !== 'editor') {
      updateMarkdownViewer(state.fsSavedContent);
    } else if (!isMd && state.fsViewMode !== 'editor') {
      updateCodeViewer(state.fsSavedContent, fsLang);
    }

    showToast('Saved ' + state.fsOpenPath);
  } catch (e) {
    if (fsEditorStatus) fsEditorStatus.textContent = 'Save failed: ' + e.message;
    showToast('Save failed: ' + e.message);
    setFsDirty(true);
  }
}

async function closeOpenFile() {
  if (!(await confirmDiscardIfDirty())) return;
  state.fsOpenPath = null;
  state.fsSavedContent = '';
  setFsDirty(false);
  state.fsReady = false;
  state.fsListingRunIndex = 0;
  if (state.fsCache && state.fsCache[state.sessionId]) delete state.fsCache[state.sessionId];
  clearFsEditor();
  if (fsEditorPath) fsEditorPath.textContent = 'No file open';
  if (fsCloseBtn) fsCloseBtn.disabled = true;
  if (fsSaveBtn) fsSaveBtn.disabled = true;
  if (fsEditorStatus) fsEditorStatus.textContent = '';
  if (fsListing) {
    fsListing.querySelectorAll('.fs-entry.active').forEach((el) => el.classList.remove('active'));
  }
  setFsMobileView('browser');
}

async function openChildSession(sid) {
  if (!sid) return;
  if (state.sessionId && state.sessionId !== sid) {
    state.parentSessionId = state.sessionId;
    try { sessionStorage.setItem('qcode-parent-session', state.sessionId); } catch (_) {}
  }
  await loadSessionById(sid);
  switchTab('chat');
  showToast('Opened child session');
}

async function returnToParentSession() {
  let pid = state.parentSessionId;
  if (!pid) {
    try { pid = sessionStorage.getItem('qcode-parent-session'); } catch (_) {}
  }
  if (!pid) {
    const parentSession = state.openSessions.find(s => s.agentMode !== 'subagent' && !s.id.startsWith('ses_'));
    if (parentSession) pid = parentSession.id;
  }
  if (!pid) {
    try {
      const res = await fetch('/session/last?messages=0');
      if (res.ok) {
        const last = await res.json();
        if (last && last.id && last.id !== state.sessionId) pid = last.id;
      }
    } catch (_) {}
  }
  if (!pid) {
    showToast('No parent session to return to');
    return;
  }
  state.parentSessionId = null;
  try { sessionStorage.removeItem('qcode-parent-session'); } catch (_) {}
  await loadSessionById(pid);
  switchTab('chat');
  showToast('Returned to parent session');
}

let delegatedPollTimer = null;
let delegatedLoadSeq = 0;
const kDelegatedFallbackMs = 10000;

// Refresh the Subagents panel after delayMs, when it is the active tab.
// An earlier pending refresh is kept: a slower one must not push it back.
let delegatedPollDue = 0;
function scheduleDelegatedRefresh(delayMs) {
  const due = Date.now() + delayMs;
  if (delegatedPollTimer && delegatedPollDue <= due) return;
  clearTimeout(delegatedPollTimer);
  delegatedPollDue = due;
  delegatedPollTimer = setTimeout(() => {
    delegatedPollTimer = null;
    if (state.activeTab === 'sessions') loadDelegatedSessionsTab();
  }, delayMs);
}

async function loadDelegatedSessionsTab() {
  if (!sessionsContent) return;
  // Loads overlap (Stop, a task event and the fallback can each start one). An
  // older load that answers last must not paint a stale row over a newer one.
  const loadSeq = ++delegatedLoadSeq;
  if (!state.sessionId) {
    sessionsContent.innerHTML = emptyState('bot', 'No active session', 'Select or create a session to see its subagents.');
    return;
  }
  if (!sessionsContent.querySelector('.agent-list')) {
    sessionsContent.innerHTML = '<div class="panel-loading"><span class="qcode-spinner"></span><span>Loading subagents…</span></div>';
  }
  try {
    let tasks = [];
    try {
      const res = await fetch('/tasks?parent_session_id=' + encodeURIComponent(state.sessionId));
      if (res.ok) {
        const data = await res.json();
        if (data.metadata && Array.isArray(data.metadata.tasks)) {
          tasks = data.metadata.tasks;
        }
      }
    } catch (_) {}

    let savedSubagents = [];
    try {
      const resSub = await fetch('/sessions?include_subagents=1&parent_session_id=' + encodeURIComponent(state.sessionId));
      if (resSub.ok) {
        const all = await resSub.json();
        if (Array.isArray(all)) {
          savedSubagents = all.filter(s => {
            if (s.parent_session_id) return s.parent_session_id === state.sessionId;
            return (s.agent_mode === 'subagent' || (s.id && s.id.startsWith('ses_')));
          });
        }
      }
    } catch (_) {}

    if (loadSeq !== delegatedLoadSeq) return;  // a newer load owns the panel

    const seenSids = new Set();
    const combined = [];

    // Active / recent tasks for this session from in-memory registry first
    for (const task of tasks) {
      if (task.parent_session_id && task.parent_session_id !== state.sessionId) {
        continue;
      }
      const sid = task.task_id || task.sessionId || '';
      if (sid) seenSids.add(sid);
      combined.push({
        sid,
        status: task.status || 'running',
        model: task.model || '',
        description: task.description || sid
      });
    }

    // Persisted subagent sessions belonging to this parent session
    for (const sub of savedSubagents) {
      if (!sub.id || seenSids.has(sub.id)) continue;
      seenSids.add(sub.id);
      combined.push({
        sid: sub.id,
        status: 'saved',
        model: sub.model || '',
        description: sub.title || sub.id
      });
    }

    // Subagent transitions arrive as backend.task.status.changed on the lead's
    // stream and refresh the panel at once. This slow fallback covers a
    // subagent that finishes after the lead's turn has ended (no stream).
    if (combined.some(c => c.status === 'running')) {
      scheduleDelegatedRefresh(kDelegatedFallbackMs);
    } else {
      clearTimeout(delegatedPollTimer);
      delegatedPollTimer = null;
    }

    const statusLabel = { running: 'Running', completed: 'Done', done: 'Done', failed: 'Failed', error: 'Failed', cancelled: 'Cancelled', interrupted: 'Interrupted', killed: 'Killed', saved: 'Saved', queued: 'Queued' };
    const totalCount = combined.length;
    let filtered = combined;
    if (state.sessionsFilterText) {
      filtered = combined.filter(c => {
        const text = `${c.sid} ${c.status} ${c.model} ${c.description}`.toLowerCase();
        return text.includes(state.sessionsFilterText);
      });
    }

    if (totalCount === 0) {
      sessionsContent.innerHTML = emptyState('bot', 'No subagents yet', 'The orchestrator starts subagents with the task tool. They will show up here.');
      return;
    }

    sessionsContent.innerHTML = '';
    const heading = document.createElement('div');
    heading.className = 'panel-subhead';
    // Per-status counts, most active first: "2 Running · 1 Done · 1 Failed".
    const counts = new Map();
    for (const c of combined) {
      const key = String(c.status || 'saved').toLowerCase();
      counts.set(key, (counts.get(key) || 0) + 1);
    }
    const order = ['running', 'queued', 'failed', 'error', 'interrupted', 'killed', 'cancelled', 'done', 'completed', 'saved'];
    const summary = [...counts.entries()]
      .sort((a, b) => order.indexOf(a[0]) - order.indexOf(b[0]))
      .map(([key, n]) => n + ' ' + (statusLabel[key] || key))
      .join(' · ');
    heading.innerHTML = '<span>' + (state.sessionsFilterText ? `${filtered.length} of ${totalCount}` : String(totalCount))
      + ' subagent' + (totalCount === 1 ? '' : 's') + '</span>'
      + (summary ? '<span class="panel-subhead-summary">' + esc(summary) + '</span>' : '')
      + '<span class="panel-subhead-hint">Click to open in chat</span>';
    sessionsContent.appendChild(heading);

    if (filtered.length === 0) {
      sessionsContent.insertAdjacentHTML('beforeend', emptyState('search', 'No matches', 'No subagents match your filter.'));
      return;
    }

    const list = document.createElement('div');
    list.className = 'agent-list';
    for (const item of filtered) {
      const sid = item.sid;
      const row = document.createElement('div');
      row.className = 'agent-card';
      const isCurrent = sid && sid === state.sessionId;
      if (isCurrent) row.classList.add('current');
      const status = String(item.status || 'saved').toLowerCase();
      row.innerHTML = '<span class="agent-card-icon">' + (status === 'running' ? '<span class="qcode-spinner"></span>' : icon('bot', 16)) + '</span>'
        + '<span class="agent-card-main">'
        +   '<span class="agent-card-title">' + esc(item.description || sid || 'Subagent') + '</span>'
        +   '<span class="agent-card-meta">'
        +     '<span class="status-chip ' + esc(status) + '">' + esc(statusLabel[status] || status) + '</span>'
        +     (item.model ? '<span class="agent-card-model">' + esc(item.model) + '</span>' : '')
        +     (sid ? '<code class="agent-card-id">' + esc(sid.length > 14 ? sid.slice(0, 14) + '…' : sid) + '</code>' : '')
        +   '</span>'
        + '</span>'
        + (status === 'running' && sid ? '<button type="button" class="agent-card-stop" title="Stop this subagent">Stop</button>' : '')
        + '<span class="agent-card-go">' + (isCurrent ? '<span class="status-chip current">Open</span>' : icon('chevron', 14)) + '</span>';

      const stopBtn = row.querySelector('.agent-card-stop');
      if (stopBtn) {
        stopBtn.addEventListener('click', async (ev) => {
          // Stop must not also open the subagent's row.
          ev.stopPropagation();
          ev.preventDefault();
          stopBtn.disabled = true;
          try {
            const res = await fetch('/tasks/' + encodeURIComponent(sid) + '/kill', { method: 'POST' });
            showToast(res.ok ? 'Stopping subagent' : 'That subagent is no longer running');
          } catch (_) {
            showToast('Could not stop the subagent');
          }
          loadDelegatedSessionsTab();
        });
      }

      if (sid) {
        row.setAttribute('role', 'button');
        row.setAttribute('tabindex', '0');
        row.title = 'Open subagent session ' + sid;
        row.addEventListener('click', () => openChildSession(sid));
        row.addEventListener('keydown', (ev) => {
          if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); openChildSession(sid); }
        });
      } else {
        row.classList.add('no-open');
        row.title = 'No session id for this task';
      }
      list.appendChild(row);
    }
    sessionsContent.appendChild(list);
  } catch (e) {
    sessionsContent.innerHTML = '<div class="error-panel">' + icon('alert', 18) + '<div><strong>Could not load subagents</strong><span>' + esc(e.message)
      + '</span></div><button type="button" class="btn" data-retry="sessions">Retry</button></div>';
    const rb = sessionsContent.querySelector('[data-retry="sessions"]');
    if (rb) rb.addEventListener('click', loadDelegatedSessionsTab);
  }
}

let lastStatsData = null;

function copyStatsSummary() {
  if (!lastStatsData) {
    showToast('No stats loaded to copy');
    return;
  }
  const d = lastStatsData;
  const usage = d.usage || {};
  const promptTok = Number(d.prompt_tokens) || Number(usage.input_tokens) || 0;
  const compTok = Number(d.completion_tokens) || Number(usage.output_tokens) || 0;
  const totTok = Number(d.total_tokens) || (promptTok + compTok);
  const toolCalls = Number(d.tool_calls) || 0;
  const toolTime = Number(d.total_tool_time_ms) || 0;
  const avgToolMs = toolCalls > 0 ? (toolTime / toolCalls).toFixed(0) + ' ms' : '0 ms';
  const md = [
    `# Session Stats: ${d.title || d.id || 'Active Session'}`,
    `- **Session ID:** \`${d.id || state.sessionId || '—'}\``,
    `- **Provider / Model:** ${d.provider || '—'} / \`${d.model || '—'}\``,
    `- **Workspace:** \`${d.workspace || '—'}\``,
    `- **Messages:** ${d.message_count || 0}`,
    `- **Tokens:** ${formatNumber(totTok)} total (${formatNumber(promptTok)} prompt, ${formatNumber(compTok)} completion)`,
    `- **Tool Invocations:** ${toolCalls} calls across ${formatMs(toolTime)} (avg ${avgToolMs}/call)`,
  ];
  if (Number(usage.model_calls) > 0) {
    const cost = usageCostSummary(usage);
    md.push(`- **Model Calls:** ${usage.model_calls} (avg ${formatMs(usage.avg_call_ms)}, ${(Number(usage.output_tok_per_s) || 0).toFixed(1)} tok/s)`);
    md.push(`- **Billed Input:** ${formatNumber(usage.input_tokens)} (${(Number(usage.cache_hit_pct) || 0).toFixed(0)}% cache hit), output ${formatNumber(usage.output_tokens)}`);
    if (Number(usage.reasoning_tokens) > 0) {
      md.push(`- **Thinking:** ${formatNumber(usage.reasoning_tokens)} tokens`);
    }
    if (cost.available) md.push(`- **Cost:** ${formatUsd(cost.total)}${cost.estimated ? ' (estimate)' : ''}`);
  }
  const mdText = md.join('\n');
  copyText(mdText, 'Stats summary copied to clipboard');
}

// Coalesces a turn's per-call events into one quiet Stats reload.
let statsRefreshTimer = null;
function scheduleStatsRefresh() {
  if (statsRefreshTimer) return;
  statsRefreshTimer = setTimeout(() => {
    statsRefreshTimer = null;
    if (state.activeTab === 'stats') loadStatsTab({ quiet: true });
  }, 500);
}

async function loadStatsTab(opts) {
  const quiet = !!(opts && opts.quiet) && lastStatsData != null;
  if (!state.sessionId) {
    lastStatsData = null;
    statsContent.innerHTML = emptyState('info', 'No active session', 'Open or create a session to see token usage, cost and tool metrics.');
    return;
  }
  if (!quiet) statsContent.innerHTML = `
    <div class="stats-loading loading-spinner">
      <div class="stats-skeleton-banner"></div>
      <div class="stats-skeleton-grid">
        <div class="stats-skeleton-card"></div>
        <div class="stats-skeleton-card"></div>
        <div class="stats-skeleton-card"></div>
        <div class="stats-skeleton-card"></div>
      </div>
      <div class="stats-skeleton-card tall"></div>
    </div>`;
  const requested = state.sessionId;
  try {
    const res = await fetch('/session/' + state.sessionId + '/stats');
    if (!res.ok) throw new Error(await responseError(res));
    const data = await res.json();
    if (requested !== state.sessionId) return;  // switched sessions meanwhile
    lastStatsData = data;
    renderStatsTab(data);
  } catch (e) {
    lastStatsData = null;
    statsContent.innerHTML = '<div class="error-panel">' + icon('alert', 18) + '<div><strong>Could not load stats</strong><span>' + esc(e.message)
      + '</span></div><button type="button" class="btn" data-retry="stats">Retry</button></div>';
    const b = statsContent.querySelector('[data-retry="stats"]');
    if (b) b.addEventListener('click', loadStatsTab);
  }
}

function renderStatsTab(data) {
  const usage = data.usage || {};
  const promptTokens = (Number(data.prompt_tokens) > 0)
    ? Number(data.prompt_tokens)
    : (Number(usage.input_tokens) || 0);
  const completionTokens = (Number(data.completion_tokens) > 0)
    ? Number(data.completion_tokens)
    : (Number(usage.output_tokens) || 0);
  const totalTokens = (Number(data.total_tokens) > 0)
    ? Number(data.total_tokens)
    : (promptTokens + completionTokens);
  const thinkingTokens = Number(usage.reasoning_tokens) || 0;
  const toolCalls = Number(data.tool_calls) || 0;
  const totalToolTimeMs = Number(data.total_tool_time_ms) || 0;
  const avgToolTime = toolCalls > 0 ? (totalToolTimeMs / toolCalls) : 0;
  const calls = Number(usage.model_calls) || 0;
  const hit = Number(usage.cache_hit_pct) || 0;
  const cacheRead = Number(usage.cache_read_tokens) || 0;
  const input = Number(usage.input_tokens) || promptTokens;
  const tokPerS = Number(usage.output_tok_per_s) || 0;
  const ctx = data.context || {};
  const ctxWindow = Number(ctx.window) || 0;
  const used = Number(ctx.used) || 0;

  const pct = (part, whole) => (whole > 0 ? Math.max(0, Math.min(100, (part / whole) * 100)) : 0);
  const pctLabel = (v) => (v > 0 && v < 10 ? v.toFixed(1) : v.toFixed(0)) + '%';
  const ctxPct = pct(used, ctxWindow);

  let createdFormatted = '—';
  let createdRelative = '';
  if (data.created_at) {
    try {
      const d = new Date(data.created_at * 1000);
      createdFormatted = d.toLocaleString(undefined, {
        month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit'
      });
      createdRelative = relTime(data.created_at * 1000);
    } catch (_) {}
  }

  const usageHtml = renderUsageSections(data, { hideKpis: true });

  const sid = data.id || state.sessionId || '';
  const title = data.title || 'Untitled Session';
  const provider = data.provider || '—';
  const model = data.model || '—';
  const workspace = data.workspace || '';

  const kpi = (label, value, sub, cls = '') => `
        <div class="stat-card kpi-card ${cls}">
          <div class="stat-label">${esc(label)}</div>
          <div class="stat-value">${esc(value)}</div>
          <div class="stat-subtext">${esc(sub)}</div>
        </div>`;
  const ctxLabel = ctxWindow > 0
    ? `${formatNumber(used)} / ${formatNumber(ctxWindow)} tokens`
    : (used > 0 ? `${formatNumber(used)} tokens in use` : 'Context window unknown');
  const html = `
    <div class="stats-container">
      <section class="stats-session-hero">
        <div class="stats-hero-top">
          <h3 class="stats-hero-title" title="${esc(title)}">${esc(title)}</h3>
          <button type="button" class="stats-hero-id-badge" title="Copy session ID" data-copy-sid="${esc(sid)}">
            <code>${esc(sid ? (sid.length > 16 ? sid.slice(0, 16) + '…' : sid) : '—')}</code>${icon('copy', 12)}
          </button>
        </div>
        <div class="stats-hero-meta-row">
          <span class="meta-chip"><span>Provider</span><strong>${esc(provider)}</strong></span>
          <span class="meta-chip"><span>Model</span><strong>${esc(model)}</strong></span>
          ${createdFormatted !== '—' ? `<span class="meta-chip" title="${esc(createdFormatted)}"><span>Started</span><strong>${esc(createdRelative || createdFormatted)}</strong></span>` : ''}
          ${workspace ? `<span class="meta-chip wide" title="${esc(workspace)}"><span>Workspace</span><code>${esc(shortPath(workspace))}</code></span>` : ''}
        </div>
        <div class="stats-hero-context-meter">
          <div class="stats-context-label-row">
            <span class="stats-context-label">Context</span>
            <span class="stats-context-val">${esc(ctxLabel)}${ctxWindow > 0 ? ` · <strong>${esc(pctLabel(ctxPct))}</strong>` : ''}</span>
          </div>
          <div class="stats-context-track" role="progressbar" aria-valuenow="${ctxPct.toFixed(0)}" aria-valuemin="0" aria-valuemax="100">
            <div class="stats-context-bar ${ctxPct > 85 ? 'danger' : ctxPct > 60 ? 'warning' : 'ok'}" style="width: ${ctxPct}%"></div>
          </div>
        </div>
      </section>

      <div class="stat-grid stats-kpi-grid">
        ${kpi('Tokens', formatNumber(totalTokens), `${formatNumber(promptTokens)} in · ${formatNumber(completionTokens)} out${thinkingTokens > 0 ? ' · ' + formatNumber(thinkingTokens) + ' thinking' : ''}`, 'accent')}
        ${kpi('Cache hit', input > 0 ? hit.toFixed(0) + '%' : '—', input > 0 ? `${formatNumber(cacheRead)} of ${formatNumber(input)} cached` : 'No cache activity')}
        ${kpi('Model calls', formatNumber(calls), calls > 0 ? `avg ${formatMs(usage.avg_call_ms)} · ${tokPerS.toFixed(1)} tok/s` : 'No model calls')}
        ${kpi('Tool calls', formatNumber(toolCalls), toolCalls > 0 ? `${formatMs(totalToolTimeMs)} total · avg ${formatMs(avgToolTime)}` : 'No tools run')}
      </div>

      ${usageHtml}
    </div>
  `;

  statsContent.innerHTML = html;

  // Add click to copy session id
  const copySidBtn = statsContent.querySelector('[data-copy-sid]');
  if (copySidBtn) {
    copySidBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      const s = copySidBtn.getAttribute('data-copy-sid');
      if (s) copyText(s, 'Session ID copied to clipboard');
    });
  }
}

const INITIAL_MESSAGE_WINDOW = 40;
const WINDOW_EXPAND_STEP = 40;
const MAX_INLINE_TOOLS = 20;
const renderCache = { sessionId: null, nodes: [] };

function renderSignature(session, msg, index) {
  const live = session.generating && index === session.messages.length - 1;
  const tools = (msg.toolEvents || []).map(t => t.status + (t.result ? 'r' : '')).join(',');
  const text = msg.content || '';
  const thought = msg.reasoning || '';
  return [msg.role, messageShape(msg), text.length, text.slice(-24), thought.length, tools,
    msg.usage || '', msg.streamError || '', msg.stoppedEarly ? 1 : 0,
    state.showThinking ? 1 : 0, live ? 1 : 0].join('|');
}

// Puts exactly `nodes` into `parent`, in order, moving only what changed.
// Unchanged message nodes are never detached, so layout work stays local to
// what changed and a text selection inside them survives the render.
function reconcileChildren(parent, nodes) {
  let cur = parent.firstChild;
  for (const node of nodes) {
    if (node === cur) { cur = cur.nextSibling; continue; }
    parent.insertBefore(node, cur);
  }
  while (cur) { const next = cur.nextSibling; parent.removeChild(cur); cur = next; }
}

function renderMessages() {
  const session = state.openSessions.find(s => s.id === state.sessionId);
  if (!session) { reconcileChildren(messagesEl, []); return; }
  const out = [];

  const isSub = session.agentMode === 'subagent' || (session.id && session.id.startsWith('ses_'));
  if (isSub) {
    const modelLabel = session.model ? `${session.provider ? session.provider + ' / ' : ''}${session.model}` : '';
    const key = session.id + '|' + modelLabel;
    let banner = renderCache.banner;
    if (!banner || banner._key !== key) {
      banner = document.createElement('div');
      banner._key = key;
      banner.className = 'subagent-session-banner';
      banner.innerHTML = `
      <div class="subagent-banner-info">
        <span class="subagent-banner-icon">${icon('bot', 16)}</span>
        <span class="subagent-banner-title">Subagent session</span>
        <span class="subagent-banner-id">${esc(session.id)}</span>
        ${modelLabel ? `<span class="subagent-banner-model">${esc(modelLabel)}</span>` : ''}
      </div>
      <button type="button" class="subagent-banner-back-btn" title="Return to parent session (Esc or b)">Back to parent</button>
    `;
      const backBtn = banner.querySelector('.subagent-banner-back-btn');
      if (backBtn) backBtn.addEventListener('click', returnToParentSession);
      renderCache.banner = banner;
    }
    out.push(banner);
  }

  const queue = session.promptQueue || [];
  if (state.reasoning === 'off' && session.messages.length > 0) {
    if (!renderCache.hint) {
      const hint = document.createElement('div');
      hint.className = 'thinking-hint';
      hint.textContent = 'Reasoning is Off — set Low/Med/High or /variant for thinking trace.';
      renderCache.hint = hint;
    }
    out.push(renderCache.hint);
  }
  if (session.messages.length === 0 && queue.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'empty-chat';
    empty.innerHTML = '<div class="empty-chat-mark">Q</div>'
      + '<h2>What are we building?</h2>'
      + '<p>Ask QCode to explore the workspace, write code, or run a command.</p>'
      + '<div class="empty-chat-hint"><span><kbd>/</kbd> commands</span><span><kbd>Ctrl</kbd> <kbd>P</kbd> palette</span><span><kbd>Enter</kbd> send</span></div>';
    out.push(empty);
    renderCache.sessionId = session.id;
    renderCache.nodes = [];
    reconcileChildren(messagesEl, out);
    return;
  }
  const total = session.messages.length;
  if (session._visibleCount === undefined) {
    session._visibleCount = Math.min(total, INITIAL_MESSAGE_WINDOW);
  }
  const visibleCount = Math.min(total, Math.max(INITIAL_MESSAGE_WINDOW, session._visibleCount));
  session._visibleCount = visibleCount;
  const startIndex = Math.max(0, total - visibleCount);
  const hiddenCount = startIndex;

  if (hiddenCount > 0) {
    const topBar = document.createElement('div');
    topBar.className = 'load-earlier-container';
    topBar.innerHTML = `
      <button type="button" class="load-earlier-btn" title="Show earlier messages">
        ${icon('chevron', 12)} Show earlier messages (${hiddenCount} hidden)
      </button>
      <button type="button" class="load-all-btn" title="Show all messages in history">Show all (${total})</button>
    `;
    const loadEarlierBtn = topBar.querySelector('.load-earlier-btn');
    if (loadEarlierBtn) {
      loadEarlierBtn.addEventListener('click', (e) => {
        e.preventDefault();
        const c = document.getElementById('chat-container');
        const prevHeight = c ? c.scrollHeight : 0;
        const prevTop = c ? c.scrollTop : 0;
        session._visibleCount = Math.min(total, session._visibleCount + WINDOW_EXPAND_STEP);
        renderMessages();
        if (c) {
          const heightDiff = c.scrollHeight - prevHeight;
          c.scrollTop = prevTop + heightDiff;
        }
      });
    }
    const loadAllBtn = topBar.querySelector('.load-all-btn');
    if (loadAllBtn) {
      loadAllBtn.addEventListener('click', (e) => {
        e.preventDefault();
        const c = document.getElementById('chat-container');
        const prevHeight = c ? c.scrollHeight : 0;
        const prevTop = c ? c.scrollTop : 0;
        session._visibleCount = total;
        renderMessages();
        if (c) {
          const heightDiff = c.scrollHeight - prevHeight;
          c.scrollTop = prevTop + heightDiff;
        }
      });
    }
    out.push(topBar);
  }

  // Reuse the DOM of messages that did not change since the last render so
  // full re-renders (tool events, refreshes) neither flicker nor lose
  // expanded tool/thought state; only messages new to this view animate in.
  const sameView = renderCache.sessionId === session.id;
  const prevNodes = sameView ? (renderCache.nodes || []) : [];
  const prevMap = new Map();
  for (const n of prevNodes) {
    if (n && n.msg) prevMap.set(n.msg, n);
  }
  const nodes = [];
  const visibleMessages = session.messages.slice(startIndex);
  visibleMessages.forEach((msg, relIndex) => {
    const absIndex = startIndex + relIndex;
    const sig = renderSignature(session, msg, absIndex);
    const cached = prevMap.get(msg);
    let el;
    if (cached && cached.sig === sig) {
      el = cached.el;
    } else {
      el = renderMessage(msg);
      if (sameView && absIndex >= (renderCache.lastTotal || 0)) el.classList.add('enter');
    }
    nodes.push({ sig, msg, el });
    out.push(el);
  });
  renderCache.sessionId = session.id;
  renderCache.nodes = nodes;
  renderCache.lastTotal = total;
  if (queue.length > 0) out.push(renderQueuedBlock(queue));
  reconcileChildren(messagesEl, out);
}

function renderMarkdownInto(el, text) {
  el._stream = null;
  el.innerHTML = renderMarkdown(text);
  tagMarkdownLinks(el);
  decorateCodeBlocks(el);
}

// Streaming variant of renderMarkdownInto: finished paragraphs are rendered
// once into their own chunk elements and never touched again; only the tail
// (the paragraph or code block still being written) is re-parsed per frame.
// That keeps per-token cost flat on long replies and leaves text selections
// in finished paragraphs intact. The stream's final full render replaces it.
function renderMarkdownStreaming(el, text, opts) {
  const decorate = !(opts && opts.decorate === false);
  let st = el._stream;
  if (!st || !text.startsWith(st.prefix)) {
    el.innerHTML = '';
    st = el._stream = { prefix: '', cut: 0, tail: document.createElement('div'), tailText: null };
    st.tail.className = 'md-stream-tail';
    el.appendChild(st.tail);
  }
  const fill = (node, src) => {
    node.innerHTML = renderMarkdown(src);
    tagMarkdownLinks(node);
    if (decorate) decorateCodeBlocks(node);
  };
  const cut = findStableMarkdownCut(text, st.cut);
  if (cut > st.cut) {
    const chunk = document.createElement('div');
    chunk.className = 'md-stream-chunk';
    fill(chunk, text.slice(st.cut, cut));
    el.insertBefore(chunk, st.tail);
    st.cut = cut;
    st.prefix = text.slice(0, cut);
    st.tailText = null;
  }
  const tailText = text.slice(st.cut);
  if (tailText !== st.tailText) {
    fill(st.tail, tailText);
    st.tailText = tailText;
  }
}

// Fenced blocks from the marked renderer already carry a header with a copy
// button; bare <pre><code> (e.g. raw HTML in markdown) gets a floating one.
function decorateCodeBlocks(root) {
  root.querySelectorAll('pre code').forEach((code) => {
    const pre = code.parentElement;
    if (!pre || pre.closest('.code-block-container') || pre.querySelector('.copy-code-btn')) return;
    pre.classList.add('has-float-copy');
    const cb = document.createElement('button');
    cb.type = 'button'; cb.className = 'copy-code-btn floating';
    cb.innerHTML = icon('copy', 13); cb.title = 'Copy code'; cb.setAttribute('aria-label', 'Copy code block');
    cb.addEventListener('click', (e) => { e.stopPropagation(); copyText(code.innerText, 'Code copied'); });
    pre.appendChild(cb);
  });
}

function renderMessage(msg) {
  const div = document.createElement('div'); div.className = 'message ' + msg.role;
  div._msgRef = msg;
  const content = document.createElement('div'); content.className = 'message-content';
  const liveSession = state.openSessions.find(s => s.id === state.sessionId);
  const isLive = !!(liveSession && liveSession.generating
    && liveSession.messages[liveSession.messages.length - 1] === msg);

  if (msg.role === 'assistant' || msg.role === 'system') {
    const header = document.createElement('div'); header.className = 'message-header';
    const avatar = msg.role === 'assistant'
      ? '<span class="role-avatar" aria-hidden="true">Q</span><span class="role-name">QCode</span>'
      : '<span class="role-avatar system" aria-hidden="true">' + icon('info', 12) + '</span><span class="role-name">System</span>';
    header.innerHTML = avatar;
    if (isLive) {
      const working = document.createElement('span'); working.className = 'msg-working';
      working.innerHTML = '<span class="dot"></span><span class="dot"></span><span class="dot"></span>';
      header.appendChild(working);
    }
    if (msg.createdAt) {
      const ts = document.createElement('span'); ts.className = 'msg-ts'; ts.title = new Date(msg.createdAt).toLocaleString();
      ts.textContent = relTime(msg.createdAt); header.appendChild(ts);
    }
    div.appendChild(header);
  }

  // Interleaved timeline: thoughts, tool calls and text segments render in
  // the order they streamed. Older messages without text segments fall back
  // to tools/thought first and the full text last.
  const timeline = Array.isArray(msg.timeline) ? msg.timeline : [];
  const hasTextSegments = timeline.some(e => e.kind === 'text');
  let lastThoughtIdx = -1;
  for (let ti = 0; ti < timeline.length; ti++) {
    if (timeline[ti].kind === 'thought' && timeline[ti].text) lastThoughtIdx = ti;
  }
  const lastEntry = timeline[timeline.length - 1];
  const appendText = (text, streaming) => {
    const textEl = document.createElement('div');
    textEl.className = 'md-content' + (streaming ? ' streaming-cursor' : '');
    if (streaming) renderMarkdownStreaming(textEl, text);
    else renderMarkdownInto(textEl, text);
    content.appendChild(textEl);
  };
  const appendToolGroup = (tools) => {
    if (!tools || tools.length === 0) return;
    const toolGroup = document.createElement('div');
    toolGroup.className = 'tool-events';

    if (tools.length <= MAX_INLINE_TOOLS || isLive) {
      for (const t of tools) {
        toolGroup.appendChild(renderToolBlock(t));
      }
    } else {
      const headCount = 5;
      const tailCount = 10;
      const hiddenTools = tools.slice(headCount, tools.length - tailCount);

      for (let i = 0; i < headCount; i++) {
        toolGroup.appendChild(renderToolBlock(tools[i]));
      }

      const expander = document.createElement('div');
      expander.className = 'tool-group-expander';
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'tool-expand-btn';
      btn.innerHTML = `${icon('chevron', 12)} Show ${hiddenTools.length} more tool calls (${tools.length} total)`;
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        if (hiddenTools.length <= 20) {
          const frag = document.createDocumentFragment();
          for (const ht of hiddenTools) {
            frag.appendChild(renderToolBlock(ht));
          }
          toolGroup.insertBefore(frag, expander);
          expander.remove();
          return;
        }

        // Batched expansion using requestAnimationFrame to prevent main UI thread freeze
        btn.disabled = true;
        btn.innerHTML = `<span class="qcode-spinner" style="display:inline-block;width:12px;height:12px;border-width:2px;margin-right:6px;vertical-align:middle;"></span>Expanding ${hiddenTools.length} tools…`;
        let curIdx = 0;
        const chunkSize = 20;

        function renderNextChunk() {
          const end = Math.min(curIdx + chunkSize, hiddenTools.length);
          const frag = document.createDocumentFragment();
          for (let i = curIdx; i < end; i++) {
            frag.appendChild(renderToolBlock(hiddenTools[i]));
          }
          toolGroup.insertBefore(frag, expander);
          curIdx = end;

          if (curIdx < hiddenTools.length) {
            btn.innerHTML = `<span class="qcode-spinner" style="display:inline-block;width:12px;height:12px;border-width:2px;margin-right:6px;vertical-align:middle;"></span>Loading… (${curIdx}/${hiddenTools.length})`;
            requestAnimationFrame(renderNextChunk);
          } else {
            expander.remove();
          }
        }

        requestAnimationFrame(renderNextChunk);
      });
      expander.appendChild(btn);
      toolGroup.appendChild(expander);

      for (let i = tools.length - tailCount; i < tools.length; i++) {
        toolGroup.appendChild(renderToolBlock(tools[i]));
      }
    }
    content.appendChild(toolGroup);
  };

  if (timeline.length > 0) {
    const byId = {};
    if (msg.toolEvents) for (const t of msg.toolEvents) byId[t.tool_call_id] = t;
    let pendingTools = [];
    for (let ti = 0; ti < timeline.length; ti++) {
      const entry = timeline[ti];
      if (entry.kind === 'tool') {
        const t = byId[entry.tool_call_id];
        if (t) pendingTools.push(t);
        continue;
      }
      if (pendingTools.length > 0) {
        appendToolGroup(pendingTools);
        pendingTools = [];
      }
      if (entry.kind === 'thought' && entry.text && state.showThinking) {
        const live = isLive && ti === lastThoughtIdx && entry === lastEntry;
        content.appendChild(renderThoughtBlock(entry.text, live ? { open: true, live: true } : undefined));
      } else if (entry.kind === 'text' && entry.text) {
        appendText(entry.text, isLive && entry === lastEntry);
      }
    }
    if (pendingTools.length > 0) {
      appendToolGroup(pendingTools);
      pendingTools = [];
    }
  } else {
    if (msg.toolEvents && msg.toolEvents.length > 0) {
      appendToolGroup(msg.toolEvents);
    }
    if (msg.reasoning && state.showThinking) {
      content.appendChild(renderThoughtBlock(msg.reasoning, isLive ? { open: true, live: true } : undefined));
    }
  }
  if (msg.content && !hasTextSegments) appendText(msg.content, isLive);
  if (isLive && !msg.content && timeline.length === 0) {
    const pending = document.createElement('div'); pending.className = 'msg-pending';
    pending.innerHTML = '<span class="qcode-spinner"></span><span>Working…</span>';
    content.appendChild(pending);
  }

  if (msg.stoppedEarly && !msg.streamError) {
    const warn = document.createElement('div');
    warn.className = 'stopped-early';
    warn.innerHTML = icon('info', 14) + '<span>Stopped early — partial response kept. Retry to continue.</span>';
    content.appendChild(warn);
  }
  if (msg.streamError) {
    const isInterrupted = /interrupted|stream ended|abort|cancel|network|fetch|connection|offline/i.test(msg.streamError);
    const error = document.createElement('div');
    error.className = 'stream-error' + (isInterrupted ? ' interrupted' : '');
    error.setAttribute('role', 'alert');
    error.innerHTML = '<span class="stream-error-icon">' + icon(isInterrupted ? 'info' : 'alert', 16) + '</span>'
      + '<div class="stream-error-body"><strong>' + (isInterrupted ? 'Response interrupted' : 'Generation failed') + '</strong>'
      + '<span>' + esc(msg.streamError) + '</span></div>';
    const retryBtn = document.createElement('button');
    retryBtn.type = 'button';
    retryBtn.className = 'error-retry-btn';
    retryBtn.innerHTML = icon('retry', 13) + '<span>Retry</span>';
    retryBtn.addEventListener('click', () => handleRetryCommand());
    error.appendChild(retryBtn);
    content.appendChild(error);
  }

  if (msg.role === 'user' || msg.role === 'assistant') {
    const acts = document.createElement('div'); acts.className = 'msg-actions';
    const mkBtn = (name, title, fn) => {
      const b = document.createElement('button'); b.type = 'button'; b.className = 'msg-action-btn';
      b.innerHTML = icon(name, 14); b.title = title; b.setAttribute('aria-label', title);
      b.addEventListener('click', (e) => { e.stopPropagation(); fn(b); });
      acts.appendChild(b); return b;
    };
    const flashCopied = (b) => {
      b.innerHTML = icon('check', 14); b.classList.add('done');
      setTimeout(() => { b.innerHTML = icon('copy', 14); b.classList.remove('done'); }, 1400);
    };
    mkBtn('copy', 'Copy message', (b) => { copyText(msg.content || '', 'Message copied'); flashCopied(b); });
    if (msg.role === 'assistant' && msg.content && !isLive) {
      mkBtn('raw', 'Toggle raw markdown', (b) => {
        const showing = div.classList.toggle('show-raw');
        b.classList.toggle('active', showing);
        let raw = content.querySelector('.raw-content');
        if (showing && !raw) {
          raw = document.createElement('pre'); raw.className = 'raw-content';
          raw.textContent = msg.content || '';
          content.insertBefore(raw, acts);
        }
      });
      mkBtn('retry', 'Retry from this prompt', () => handleRetryCommand());
    }
    if (msg.usage && msg.role === 'assistant') {
      const uc = document.createElement('span'); uc.className = 'usage-block';
      uc.textContent = msg.usage.replace(/^Tokens — /, '');
      uc.title = msg.usage;
      acts.appendChild(uc);
    }
    content.appendChild(acts);
  }
  div.appendChild(content);
  div._shape = messageShape(msg);
  return div;
}


function copyText(t, okMsg) {
  const done = () => showToast(okMsg || 'Copied');
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(t).then(done).catch(() => fallbackCopy(t, done));
  } else fallbackCopy(t, done);
}
function fallbackCopy(t, done) {
  try {
    const ta = document.createElement('textarea'); ta.value = t; document.body.appendChild(ta);
    ta.select(); document.execCommand('copy'); ta.remove(); done();
  } catch (_) { showToast('Copy failed'); }
}







function renderToolBlock(tc) {
  const toolName = tc.tool_name || '';
  
  // Extract command
  let command = toolName;
  if (tc.arguments) {
    const args = parseToolValue(tc.arguments);
    if (toolName === 'bash' || toolName === 'shell' || toolName === 'run_command') {
      command = args.command || args.cmd || args.script || '';
    } else if (['read_file', 'view_file', 'write_file', 'edit_file'].includes(toolName)) {
      command = toolName + ' ' + (args.path || args.file || args.file_path || args.filename || '');
    } else if (['search', 'grep', 'ripgrep'].includes(toolName)) {
      command = toolName + ' "' + (args.query || args.pattern || '') + '"';
    } else if (toolName === 'task' || toolName === 'dispatch_agent') {
      command = 'task ' + (args.description || args.prompt || '');
    } else {
      command = args.raw || (toolName + ' ' + JSON.stringify(args));
    }
  }

  // Extract description
  let desc = '';
  if (tc.arguments) {
    const args = parseToolValue(tc.arguments);
    desc = args.description || args.desc || args.prompt || '';
  }

  // Extract workdir
  let workdir = '';
  if (tc.arguments && (toolName === 'bash' || toolName === 'shell' || toolName === 'run_command')) {
    const args = parseToolValue(tc.arguments);
    workdir = args.workdir || args.cwd || '';
  }

  // Extract output content
  let output = '';
  let exitCode = null;
  let hasExit = false;
  if (tc.result) {
    try {
      const resultObj = typeof tc.result === 'string' ? JSON.parse(tc.result) : tc.result;
      if (resultObj && typeof resultObj === 'object') {
        output = resultObj.output || resultObj.content || resultObj.result || resultObj.summary || resultObj.matches || resultObj.error || '';
        if (resultObj.metadata && typeof resultObj.metadata === 'object' && 'exit' in resultObj.metadata) {
          exitCode = resultObj.metadata.exit;
          hasExit = true;
        }
      } else {
        output = String(tc.result);
      }
    } catch(e) {
      output = String(tc.result);
    }
  }

  const toolIcons = {
    bash: 'terminal', shell: 'terminal', run_command: 'terminal',
    task: 'bot', dispatch_agent: 'bot',
    read_file: 'fileText', view_file: 'fileText',
    write_file: 'pencil', edit_file: 'pencil',
    search: 'search', grep: 'search', ripgrep: 'search', glob: 'search',
    webfetch: 'globe', web_fetch: 'globe', websearch: 'globe', web_search: 'globe'
  };
  const toolIcon = toolIcons[toolName] || 'wrench';
  const displayNames = { bash: 'Bash', task: 'Task', read_file: 'Read File', write_file: 'Write File', view_file: 'Read File', edit_file: 'Edit File', search: 'Search' };
  const displayName = displayNames[toolName] || capitalize(toolName);

  const block = document.createElement('div');
  block.className = 'tool-block ' + tc.status; // success, error, running
  const childSid = extractChildSessionId(tc);
  if (childSid) {
    block.classList.add('task-openable');
    block.dataset.childSession = childSid;
  }
  
  const header = document.createElement('div');
  header.className = 'tool-header';
  
  header.setAttribute('role', 'button');
  header.tabIndex = 0;

  const chevron = document.createElement('span');
  chevron.className = 'tool-chevron';
  chevron.innerHTML = icon('chevron', 12);

  const typeIcon = document.createElement('span');
  typeIcon.className = 'tool-type-icon';
  typeIcon.innerHTML = icon(toolIcon, 14);

  const heading = document.createElement('span');
  heading.className = 'tool-heading';
  const title = desc || displayName;
  let subtitle = command && command !== toolName ? command : '';
  if (subtitle.startsWith(toolName + ' ')) subtitle = subtitle.slice(toolName.length + 1).trim();
  if (subtitle === title) subtitle = '';
  heading.innerHTML = `<span class="tool-title">${esc(title)}</span>`
    + (subtitle ? `<span class="tool-subtitle">${esc(subtitle.split('\n')[0])}</span>` : '');

  const timing = document.createElement('span');
  timing.className = 'tool-duration';
  const ms = tc.duration_ms ? Math.round(tc.duration_ms) : 0;
  timing.textContent = ms ? (ms >= 1000 ? (ms / 1000).toFixed(ms >= 10000 ? 0 : 1) + 's' : ms + 'ms') : '';

  const statusEl = document.createElement('span');
  statusEl.className = 'tool-status-icon';
  if (tc.status === 'running') statusEl.innerHTML = '<span class="qcode-spinner"></span>';
  else if (tc.status === 'error') statusEl.innerHTML = icon('x', 13);
  else statusEl.innerHTML = icon('check', 13);

  header.appendChild(chevron);
  header.appendChild(typeIcon);
  header.appendChild(heading);
  if (childSid) {
    const openEl = document.createElement('button');
    openEl.type = 'button';
    openEl.className = 'task-open-child';
    openEl.innerHTML = icon('external', 12) + '<span>Open</span>';
    openEl.title = 'Open delegated child session in chat (' + childSid + ')';
    openEl.addEventListener('click', (ev) => {
      ev.stopPropagation();
      openChildSession(childSid);
    });
    header.appendChild(openEl);
  }
  header.appendChild(timing);
  header.appendChild(statusEl);

  const body = document.createElement('div');
  body.className = 'tool-body collapsed';

  // The body (potentially large output) is built on first expand only.
  let bodyFilled = false;
  const fillBody = () => {
    bodyFilled = true;

    // Command row
    const cmdRow = document.createElement('div');
    cmdRow.className = 'tool-cmd-row';
    cmdRow.innerHTML = `<span class="tool-prompt">$</span> <span class="tool-cmd">${esc(command)}</span>`;
    body.appendChild(cmdRow);

    // Workdir row
    if (workdir) {
      const wdRow = document.createElement('div');
      wdRow.className = 'tool-wd-row';
      wdRow.innerHTML = `<span class="tool-in">in</span> <span class="tool-path">${esc(workdir)}</span>`;
      body.appendChild(wdRow);
    }

    // Output row
    if (output) {
      const outPre = document.createElement('pre');
      outPre.className = 'tool-output' + (tc.status === 'error' ? ' error' : '');
      outPre.textContent = output;
      body.appendChild(outPre);
    }

    // Exit code row
    if (hasExit) {
      const exitRow = document.createElement('div');
      exitRow.className = 'tool-exit-row ' + (exitCode === 0 ? 'success' : 'error');
      exitRow.textContent = `${exitCode === 0 ? '✓' : '✗'} exit ${exitCode}`;
      body.appendChild(exitRow);
    }

    if (childSid) {
      const childActionRow = document.createElement('div');
      childActionRow.className = 'tool-child-action-row';
      childActionRow.innerHTML = `<button type="button" class="tool-open-child-btn">${icon('bot', 14)}<span>Open child session</span><code>${esc(childSid)}</code>${icon('chevron', 12)}</button>`;
      const openBtn = childActionRow.querySelector('.tool-open-child-btn');
      if (openBtn) {
        openBtn.addEventListener('click', (ev) => {
          ev.stopPropagation();
          openChildSession(childSid);
        });
      }
      body.appendChild(childActionRow);
    }
  };

  // Expanded state lives on the tool-call object so it survives re-renders.
  const applyExpanded = () => {
    const expanded = !!tc._open;
    if (expanded && !bodyFilled) fillBody();
    body.classList.toggle('collapsed', !expanded);
    block.classList.toggle('open', expanded);
    header.setAttribute('aria-expanded', String(expanded));
  };
  header.addEventListener('click', (ev) => {
    if (childSid && ev.target.closest && ev.target.closest('.task-open-child')) {
      openChildSession(childSid);
      return;
    }
    tc._open = !tc._open;
    applyExpanded();
  });
  header.addEventListener('keydown', (ev) => {
    if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); header.click(); }
  });
  applyExpanded();

  block.appendChild(header);
  block.appendChild(body);
  return block;
}


// ── Markdown helpers: frontmatter + link navigation (inspired by Markdown-Oxide) ──────────────





function parseFrontmatter(raw) {
  if (!raw || typeof raw !== 'string') return null;
  if (typeof window !== 'undefined' && window.jsyaml && typeof window.jsyaml.load === 'function') {
    try {
      const result = window.jsyaml.load(raw);
      if (result && typeof result === 'object') return result;
    } catch (e) {
      // Fallback if jsyaml fails
    }
  }
  const data = {};
  const lines = raw.split(/\r?\n/);
  let currentKey = null;
  for (let line of lines) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#')) continue;
    if (trimmed.startsWith('- ') && currentKey) {
      const val = trimmed.slice(2).trim().replace(/^["']|["']$/g, '');
      if (!Array.isArray(data[currentKey])) {
        data[currentKey] = data[currentKey] ? [data[currentKey]] : [];
      }
      data[currentKey].push(val);
      continue;
    }
    const colonIdx = line.indexOf(':');
    if (colonIdx > 0) {
      const key = line.slice(0, colonIdx).trim();
      let valStr = line.slice(colonIdx + 1).trim();
      currentKey = key;
      if (!valStr) {
        data[key] = [];
        continue;
      }
      if (valStr.startsWith('[') && valStr.endsWith(']')) {
        const items = valStr.slice(1, -1).split(',').map(s => s.trim().replace(/^["']|["']$/g, '')).filter(Boolean);
        data[key] = items;
      } else if (valStr === 'true') {
        data[key] = true;
      } else if (valStr === 'false') {
        data[key] = false;
      } else {
        data[key] = valStr.replace(/^["']|["']$/g, '');
      }
    }
  }
  return Object.keys(data).length > 0 ? data : null;
}

function renderFrontmatterBox(raw) {
  if (!raw) return '';
  const data = typeof raw === 'object' ? raw : parseFrontmatter(raw);
  if (!data || typeof data !== 'object') {
    return `<div class="md-frontmatter-card">
      <div class="md-fm-header md-fm-toggle">
        <span class="md-fm-title">
          <svg class="md-fm-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>
          Properties
        </span>
        <span class="md-fm-chevron">&#9660;</span>
      </div>
      <div class="md-fm-body"><pre class="md-fm-raw">${esc(String(raw))}</pre></div>
    </div>`;
  }

  const entries = Object.entries(data);
  if (entries.length === 0) return '';

  let html = `<div class="md-frontmatter-card">
    <div class="md-fm-header md-fm-toggle">
      <span class="md-fm-title">
        <svg class="md-fm-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>
        Properties (${entries.length})
      </span>
      <span class="md-fm-chevron">&#9660;</span>
    </div>
    <div class="md-fm-body">`;

  for (const [key, val] of entries) {
    const keyLower = key.toLowerCase();
    html += `<div class="md-fm-row-key">${esc(key)}</div><div class="md-fm-row-val">`;

    if (keyLower === 'tags' || keyLower === 'tag') {
      const tagList = Array.isArray(val) ? val : String(val).split(/[\s,]+/);
      html += `<div class="md-fm-tag-list">`;
      for (const t of tagList) {
        const cleanTag = String(t).trim().replace(/^#/, '');
        if (cleanTag) {
          html += `<span class="md-fm-tag">#${esc(cleanTag)}</span>`;
        }
      }
      html += `</div>`;
    } else if (keyLower === 'aliases' || keyLower === 'alias') {
      const aliasList = Array.isArray(val) ? val : [val];
      html += `<div class="md-fm-tag-list">`;
      for (const a of aliasList) {
        const cleanAlias = String(a).trim();
        if (cleanAlias) {
          html += `<span class="md-fm-alias">📌 ${esc(cleanAlias)}</span>`;
        }
      }
      html += `</div>`;
    } else if (Array.isArray(val)) {
      html += `<div class="md-fm-tag-list">`;
      for (const item of val) {
        html += `<span class="md-fm-tag" style="background: rgba(255,255,255,0.06); color: var(--text); border-color: var(--border);">${esc(String(item))}</span>`;
      }
      html += `</div>`;
    } else if (typeof val === 'boolean') {
      html += `<span class="md-fm-bool ${val}">${val}</span>`;
    } else if (typeof val === 'string' && /^https?:\/\//i.test(val.trim())) {
      html += `<a href="${esc(val.trim())}" target="_blank" rel="noopener noreferrer">${esc(val.trim())}</a>`;
    } else if (typeof val === 'object' && val !== null) {
      html += `<pre class="md-fm-raw" style="margin:0;">${esc(JSON.stringify(val, null, 2))}</pre>`;
    } else {
      html += `${esc(String(val != null ? val : ''))}`;
    }

    html += `</div>`;
  }

  html += `</div></div>`;
  return html;
}



function normalizeRelPath(baseDir, rel) {
  if (!rel) return '';
  const segs = baseDir ? baseDir.split('/') : [];
  for (const part of rel.split('/')) {
    if (part === '' || part === '.') continue;
    if (part === '..') { if (segs.length) segs.pop(); }
    else segs.push(part);
  }
  return segs.join('/');
}

function scrollToMarkdownHeading(fragment) {
  if (!fragment || !fsViewMarkdown) return;
  const cleanId = fragment.replace(/^#/, '').trim();
  if (!cleanId) return;

  let targetEl = null;
  try {
    targetEl = fsViewMarkdown.querySelector('#' + CSS.escape(cleanId));
  } catch (e) {}

  if (!targetEl) {
    const slug = cleanId.toLowerCase().trim().replace(/[^\w\s-]/g, '').replace(/\s+/g, '-');
    try {
      targetEl = fsViewMarkdown.querySelector('#' + CSS.escape(slug));
    } catch (e) {}
  }
  if (!targetEl) {
    const headings = fsViewMarkdown.querySelectorAll('h1, h2, h3, h4, h5, h6');
    for (const h of headings) {
      if (h.textContent.trim().toLowerCase() === cleanId.toLowerCase() ||
          (h.id && h.id.toLowerCase() === cleanId.toLowerCase())) {
        targetEl = h;
        break;
      }
    }
  }

  if (targetEl) {
    targetEl.scrollIntoView({ behavior: 'smooth', block: 'start' });
    targetEl.classList.remove('md-target-highlight');
    void targetEl.offsetWidth;
    targetEl.classList.add('md-target-highlight');
    setTimeout(() => targetEl.classList.remove('md-target-highlight'), 2000);
  }
}

async function navigateMarkdownLink(href) {
  if (!href) return false;
  if (href.startsWith('/')) {
    const absPath = decodeURIComponent(href.split('?')[0]);
    await switchFilesSubtab('explorer');
    await openFsFile(absPath);
    return true;
  }
  const hashIdx = href.indexOf('#');
  const pathPart = (hashIdx >= 0 ? href.slice(0, hashIdx) : href).split('?')[0];
  const fragment = hashIdx >= 0 ? href.slice(hashIdx + 1) : '';

  if (!pathPart) {
    if (fragment) {
      scrollToMarkdownHeading(fragment);
      return true;
    }
    return false;
  }

  let baseDir = '';
  if (state.fsOpenPath) {
    const idx = state.fsOpenPath.lastIndexOf('/');
    baseDir = idx >= 0 ? state.fsOpenPath.slice(0, idx) : '';
  } else if (state.fsDir) {
    baseDir = state.fsDir;
  }

  let resolved = normalizeRelPath(baseDir, pathPart);
  // `[[note]]` with no extension from the file viewer only resolves against
  // the current directory; try against the workspace root as a future-run.
  const wantMd = !(/\.[a-zA-Z0-9]+$/.test(resolved));

  const cache = state.fsReady && state.fsCache ? state.fsCache[state.sessionId] : null;
  if (cache) {
    const rel = resolved.replace(/^\/+/, '');
    const cand = cache[rel];
    if (cand) resolved = cand.full_path || resolved;
  } else if (state.fsReady) {
    const full = await getFsPath(resolved);
    if (full) resolved = full;
  }

  await switchFilesSubtab('explorer');

  if (state.fsOpenPath === resolved) {
    setFsMobileView('viewer');
    if (fragment) {
      scrollToMarkdownHeading(fragment);
    }
    return true;
  }

  const lower = resolved.toLowerCase();
  if (lower.endsWith('.md') || lower.endsWith('.markdown') || wantMd) {
    await openFsFile(resolved, fragment);
    return true;
  }

  const hasExt = lower.includes('.') && !lower.endsWith('/');
  if (!hasExt) {
    await loadFsListing(resolved);
    return true;
  }

  await openFsFile(resolved);
  return true;
}

function tagMarkdownLinks(container) {
  if (!container) return;
  container.querySelectorAll('a[href]').forEach((a) => {
    const href = a.getAttribute('href') || '';
    if (/^(https?:|mailto:|tel:|data:|\/\/)/i.test(href) || href.startsWith('/')) {
      if (/^https?:/i.test(href)) {
        a.setAttribute('target', '_blank');
        a.setAttribute('rel', 'noopener noreferrer');
      }
      return;
    }
    a.classList.add('md-internal-link');
  });
}

let _qcodeMarkedRenderer = null;
function getMarkedRenderer() {
  if (_qcodeMarkedRenderer) return _qcodeMarkedRenderer;
  if (typeof marked === 'undefined' || !marked.Renderer) return null;
  const r = new marked.Renderer();
  r.heading = function (text, level, raw) {
    const cleanRaw = raw || text.replace(/<[^>]+>/g, '');
    const slug = cleanRaw.toLowerCase().trim().replace(/[^\w\s-]/g, '').replace(/\s+/g, '-');
    return `<h${level} id="${slug}">${text}</h${level}>`;
  };
  r.code = function (code, lang) {
    const displayLang = (lang || 'code').trim();
    const codeStr = typeof code === 'object' ? code.text : code;
    const cleanCode = codeStr.replace(/\n$/, '');

    if (displayLang === 'mermaid') {
      const diagramId = 'mermaid-' + Math.random().toString(36).substring(2, 9);
      setTimeout(() => {
        if (!document.getElementById(diagramId)) return;
        queueMermaidRender(diagramId, cleanCode);
      }, 50);
      return `<div class="mermaid-diagram-container"><div id="${diagramId}" class="mermaid-target">${esc(cleanCode)}</div></div>`;
    }

    if (displayLang === 'plantuml' || displayLang === 'puml') {
      const diagramId = 'plantuml-' + Math.random().toString(36).substring(2, 9);
      const isDark = true;
      const initialMode = isDark ? 'dark' : 'light';
      const pumlUrl = encodePlantUmlHex(cleanCode, initialMode);
      const encodedCode = encodeURIComponent(cleanCode);
      return `<div class="plantuml-diagram-container" id="${diagramId}" data-puml-code="${encodedCode}" data-puml-mode="${initialMode}">
        <div class="code-block-header">
          <span class="code-block-lang">plantuml</span>
          <div style="display:flex;gap:6px;align-items:center;">
            <button type="button" class="copy-code-btn plantuml-mode-btn" onclick="togglePlantUmlDiagramMode('${diagramId}')" title="Toggle between Dark and Light mode">
              <span class="plantuml-mode-label">${isDark ? 'Light' : 'Dark'}</span>
            </button>
            <button type="button" class="copy-code-btn" onclick="copyPlantUmlCode('${diagramId}', this)">Copy</button>
            <a href="${pumlUrl}" target="_blank" rel="noopener noreferrer" class="copy-code-btn plantuml-link" style="text-decoration:none;display:inline-flex;align-items:center;">Open SVG</a>
          </div>
        </div>
        <div class="plantuml-image-wrapper ${isDark ? 'dark-mode' : 'light-mode'}">
          <img src="${pumlUrl}" alt="PlantUML Diagram" loading="lazy" onerror="this.parentElement.innerHTML='<div class=\'plantuml-error\'>PlantUML render error. Check syntax.</div>'" />
        </div>
      </div>`;
    }

    if (displayLang === 'latex' || displayLang === 'katex' || displayLang === 'math') {
      if (typeof katex !== 'undefined' && katex.renderToString) {
        try {
          const rendered = katex.renderToString(cleanCode, { displayMode: true, throwOnError: false });
          return `<div class="katex-display">${rendered}</div>`;
        } catch (e) {
          // fall through to code block
        }
      }
    }

    let highlighted = esc(cleanCode);
    const hl = getHljs();
    const hlLang = displayLang.toLowerCase();
    if (hl && hlLang !== 'code' && hl.getLanguage && hl.getLanguage(hlLang) && cleanCode.length < 200000) {
      try { highlighted = hl.highlight(cleanCode, { language: hlLang, ignoreIllegals: true }).value; } catch (_) {}
    }
    return `<div class="code-block-container">
      <div class="code-block-header">
        <span class="code-block-lang">${esc(displayLang)}</span>
        <button type="button" class="copy-code-btn" onclick="qcodeCopyCode(this)" title="Copy code" aria-label="Copy code">${icon('copy', 13)}<span>Copy</span></button>
      </div>
      <pre class="md-code"><code class="hljs">${highlighted}</code></pre>
    </div>`;
  };
  r.codespan = function (code) {
    const text = typeof code === 'object' ? code.text : code;
    return `<code class="md-inline-code">${esc(text)}</code>`;
  };
  r.link = function (href, title, text) {
    const h = href || '';
    const external = /^(https?:|mailto:|data:|\/\/)/i.test(h);
    const attrs = external ? ' target="_blank" rel="noopener noreferrer"' : ' data-md-link="internal"';
    const t = title ? ` title="${esc(title)}"` : '';
    // The transform stage emits `<target>` angle-urls so hrefs never contain
    // unescaped parens — replace the angle wraps with percent-encoded URLs.
    let url = h;
    if (/^<[^>]+>$/.test(url)) {
      url = url.slice(1, -1);
      if (!external) url = url.replace(/\(/g, '%28').replace(/\)/g, '%29');
    }
    return `<a href="${esc(url)}"${t}${attrs}>${text}</a>`;
  };

  r.image = function (href, title, text) {
    const h = href || '';
    const external = /^(https?:|data:|\/\/)/i.test(h);
    let src = h;
    if (!external && state.sessionId) {
      let baseDir = '';
      if (state.fsOpenPath) {
        const idx = state.fsOpenPath.lastIndexOf('/');
        baseDir = idx >= 0 ? state.fsOpenPath.slice(0, idx) : '';
      }
      const relPath = normalizeRelPath(baseDir, h) || h;
      src = '/session/' + state.sessionId + '/fs/raw?path=' + encodeURIComponent(relPath);
    }
    const t = title ? ` title="${esc(title)}"` : '';
    const alt = text ? ` alt="${esc(text)}"` : '';
    return `<img src="${esc(src)}"${alt}${t} class="md-image" />`;
  };
  _qcodeMarkedRenderer = r;
  return r;
}

function prepareMarkdownBody(src, opts) {
  opts = opts || {};
  const fm = extractFrontmatter(src);
  let md = fm.body;
  if (opts.wikilinks !== false) md = transformWikilinks(md);
  return { frontmatter: fm.frontmatter, md: md };
}

async function onMarkdownLinkClick(e) {
  const el = e.target ? e.target.closest('a[href], .md-transclusion-card, .md-frontmatter-card .md-fm-toggle') : null;
  if (!el) return;
  if (el.classList.contains('md-fm-toggle')) return; // toggle handled by bindFrontmatterToggles
  // Frontmatter card links are plain hrefs (external), handled by browser; skip.
  if (el.closest('.md-frontmatter-card')) return;

  let href = '';
  if (el.tagName === 'A') {
    href = el.getAttribute('href') || '';
  } else if (el.classList.contains('md-transclusion-card')) {
    href = el.getAttribute('data-href') || '';
  }
  if (!href) return;

  if (href.startsWith('http')) {
    if (el.tagName === 'A') {
      el.setAttribute('target', '_blank');
      el.setAttribute('rel', 'noopener noreferrer');
    }
    return;
  }
  if (href.startsWith('#')) {
    e.preventDefault();
    scrollToMarkdownHeading(href.slice(1));
    return;
  }
  if (href.startsWith('/')) {
    e.preventDefault();
    await navigateMarkdownLink(href);
    return;
  }
  if (/^(mailto:|tel:|data:|\/\/)/i.test(href)) {
    return;
  }

  e.preventDefault();
  const ok = await navigateMarkdownLink(href);
  if (!ok && el.tagName === 'A') {
    window.open(href, '_blank');
  }
}

function renderMarkdown(text) {
  if (typeof marked === 'undefined' || !marked.parse) {
    return esc(text).replace(/\n/g, '<br>');
  }

  const mathTokens = [];
  const codeBlocks = [];

  // Libraries not fetched yet: request them now (the transcript re-renders when they arrive).
  if (typeof katex === 'undefined' && /\$|\\[([]/.test(text)) loadRichVendor('katex');
  if (!getHljs() && /```|~~~/.test(text)) loadRichVendor('hljs');

  // 1. Protect code fences
  let protectedText = text.replace(/(```[\s\S]*?```|`[^`\n]+`)/g, (match) => {
    const cid = '@@CODE_BLOCK_' + codeBlocks.length + '@@';
    codeBlocks.push(match);
    return cid;
  });

  // Auto-wrap standalone @startuml ... @enduml into ```plantuml fences
  protectedText = protectedText.replace(/(^|\n)(@(startuml|startmindmap|startwbs|startgantt)[\s\S]*?@(enduml|endmindmap|endwbs|endgantt))(\n|$)/g, '$1```plantuml\n$2\n```$5');

  // 2. Extract and protect display math ($$...$$)
  if (typeof katex !== 'undefined' && katex.renderToString) {
    protectedText = protectedText.replace(/\$\$([\s\S]*?)\$\$/g, (match, expr) => {
      const tid = '@@KATEX_DISP_' + mathTokens.length + '@@';
      try {
        const rendered = katex.renderToString(expr.trim(), { displayMode: true, throwOnError: false });
        mathTokens.push({ id: tid, html: '<div class="katex-display">' + rendered + '</div>', isDisplay: true });
      } catch (e) {
        mathTokens.push({ id: tid, html: match, isDisplay: true });
      }
      return '\n\n' + tid + '\n\n';
    });

    // 3. Extract and protect inline math ($...$)
    protectedText = protectedText.replace(/(^|[^\\\$])\$([^\$\n\r]+?)\$/g, (match, prefix, expr) => {
      // Avoid converting common currency like $10 or $99.95
      if (/^\s*\d+(\.\d+)?\s*$/.test(expr)) return match;
      const tid = '@@KATEX_INL_' + mathTokens.length + '@@';
      try {
        const rendered = katex.renderToString(expr.trim(), { displayMode: false, throwOnError: false });
        mathTokens.push({ id: tid, html: '<span class="katex-inline">' + rendered + '</span>', isDisplay: false });
        return prefix + tid;
      } catch (e) {
        return match;
      }
    });
  }

  // 4. Restore code blocks before marked parsing
  protectedText = protectedText.replace(/@@CODE_BLOCK_(\d+)@@/g, (_, id) => codeBlocks[Number(id)]);

  // 5. Run marked parsing with custom renderer
  const { frontmatter, md } = prepareMarkdownBody(protectedText, { wikilinks: true });
  const renderer = getMarkedRenderer();
  const parseOptions = { renderer: renderer, gfm: true, breaks: true, headerIds: false, mangle: false };
  let html = marked.parse(md, parseOptions);

  // 6. Substitute rendered KaTeX math tokens back
  for (const item of mathTokens) {
    if (item.isDisplay) {
      html = html.replace(new RegExp('<p>\\s*' + item.id + '\\s*<\\/p>', 'g'), item.html);
    }
    html = html.replace(new RegExp(item.id, 'g'), item.html);
  }

  if (frontmatter) {
    html = renderFrontmatterBox(frontmatter) + html;
  }
  return html;
}

function bindFrontmatterToggles(container) {
  if (!container) return;
  container.querySelectorAll('.md-frontmatter-card').forEach((card) => {
    const header = card.querySelector('.md-fm-toggle') || card;
    header.addEventListener('click', (e) => {
      if (e.target.closest('a')) return;
      card.classList.toggle('collapsed');
    });
  });
}

async function getFsPath(rel) {
  try {
    const res = await fetch('/session/' + state.sessionId + '/fs/exists?path=' + encodeURIComponent(rel));
    if (res.ok) {
      const data = await res.json().catch(() => ({}));
      if (data && data.exists) return data.path;
    }
  } catch (e) {}
  return null;
}

const MD_IMG_EXTS = /^(png|jpe?g|gif|svg|webp|bmp|ico|avif)$/i;

let lastGenerating = null;
function setGenerating(on) {
  if (messagesEl) messagesEl.setAttribute('aria-busy', on ? 'true' : 'false');
  if (streamStatusEl && lastGenerating !== Boolean(on)) {
    if (lastGenerating !== null) streamStatusEl.textContent = on ? 'Generating response' : 'Response complete';
    lastGenerating = Boolean(on);
  }
  sendBtn.disabled = false;
  promptInput.disabled = false;
  sendBtn.innerHTML = on
    ? '<span class="send-label">Queue</span><span class="send-icon"><svg class="ui-icon" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg></span>'
    : '<span class="send-icon"><svg class="ui-icon" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><line x1="12" y1="19" x2="12" y2="5"/><polyline points="5 12 12 5 19 12"/></svg></span>';
  sendBtn.classList.toggle('queue-mode', Boolean(on));
  sendBtn.title = on ? 'Queue prompt (Enter)' : 'Send (Enter)';
  document.body.classList.toggle('is-generating', Boolean(on));
  sendBtn.setAttribute('aria-label', on ? 'Queue prompt' : 'Send message');
  if (pauseBtn) pauseBtn.classList.toggle('hidden', !on);
  if (typeof retryBtn !== 'undefined' && retryBtn) retryBtn.disabled = Boolean(on);
  updateStatusBar();
  renderSessionTabs();
}
function nearBottom() {
  const c = document.getElementById('chat-container');
  if (!c) return true;
  return (c.scrollHeight - c.scrollTop - c.clientHeight) < 120;
}
// Whether the chat follows new output. Only the user unsticks it, by
// scrolling up (wheel, touch, keys, scrollbar or a selection drag); growth of
// the streaming reply or layout shifts never do. It sticks again once they
// are back at the very bottom, press the Latest pill, or send.
let chatStick = true;
let chatScrollRaf = 0;
let chatPointerHeld = false;
function onChatScroll() {
  if (chatScrollRaf) return;
  chatScrollRaf = requestAnimationFrame(() => {
    chatScrollRaf = 0;
    const c = document.getElementById('chat-container');
    if (!c) return;
    const gap = c.scrollHeight - c.scrollTop - c.clientHeight;
    if (gap <= 4) chatStick = true;
    else if (chatPointerHeld || chatSelecting) chatStick = false;
    updateJumpPill();
  });
}
function scrollToBottom(force) {
  const c = document.getElementById('chat-container');
  if (!c) return;
  if (force) chatStick = true;
  if (chatStick) c.scrollTop = c.scrollHeight;
  updateJumpPill();
}
function updateJumpPill() {
  const c = document.getElementById('chat-container');
  if (!c || !document.getElementById('main-area')) return;
  let pill = document.getElementById('jump-latest');
  const show = (c.scrollHeight - c.scrollTop - c.clientHeight) > 300;
  if (show === !!pill) return;
  if (show && !pill) {
    pill = document.createElement('button'); pill.id = 'jump-latest'; /* id="jump-latest" */ pill.className = 'jump-latest';
    pill.innerHTML = icon('arrowDown', 14) + '<span>Latest</span>'; pill.type = 'button'; pill.setAttribute('aria-label', 'Jump to latest message');
    pill.addEventListener('click', () => scrollToBottom(true));
    (document.getElementById('input-area') || document.getElementById('main-area')).appendChild(pill);
  } else if (!show && pill) pill.remove();
}


// ── Toasts ──
// showToast(message, kind?) — kind is 'error' | 'warning' | 'success' | 'info';
// when omitted it is inferred from the wording so existing call sites get the
// right severity. Identical visible toasts are coalesced with a counter.
const TOAST_MAX = 4;
function inferToastKind(msg) {
  if (/\b(fail(ed|ure)?|error|cannot|can't|could not|unable|invalid|unknown|denied|refused|offline|not found|no provider)\b/i.test(msg)) return 'error';
  if (/\b(warn(ing)?|slow|interrupted|timeout|timed out|retrying|usage:)\b/i.test(msg)) return 'warning';
  if (/^(no|nothing)\b/i.test(msg)) return 'info';
  if (/\b(copied|created|saved|cleared|renamed|switched|deleted|removed|loaded|compacted|queued|done)\b/i.test(msg)) return 'success';
  return 'info';
}
function toastRegion() {
  let r = document.getElementById('toast-region');
  if (!r) {
    r = document.createElement('div');
    r.id = 'toast-region';
    r.className = 'toast-region';
    r.setAttribute('role', 'status');
    r.setAttribute('aria-live', 'polite');
    document.body.appendChild(r);
  }
  return r;
}
function dismissToast(t) {
  if (!t || t._leaving) return;
  t._leaving = true;
  clearTimeout(t._timer);
  t.classList.add('leaving');
  setTimeout(() => t.remove(), 180);
}
function armToast(t) {
  clearTimeout(t._timer);
  t._timer = setTimeout(() => dismissToast(t), t._ttl);
}
function showToast(msg, kind) {
  const text = String(msg == null ? '' : msg).trim();
  if (!text) return;
  const k = ['error', 'warning', 'success', 'info'].includes(kind) ? kind : inferToastKind(text);
  const region = toastRegion();
  for (const existing of region.children) {
    if (existing._text === text && !existing._leaving) {
      existing._count = (existing._count || 1) + 1;
      const c = existing.querySelector('.toast-count');
      if (c) { c.textContent = '×' + existing._count; c.hidden = false; }
      armToast(existing);
      return;
    }
  }
  const t = document.createElement('div');
  t.className = 'toast toast-' + k;
  t.setAttribute('role', k === 'error' ? 'alert' : 'status');
  t._text = text;
  t._ttl = k === 'error' ? 8000 : k === 'warning' ? 6000 : 3200;
  const iconName = { error: 'alert', warning: 'alert', success: 'check', info: 'info' }[k];
  t.innerHTML = '<span class="toast-icon">' + icon(iconName, 15) + '</span>'
    + '<span class="toast-msg"></span><span class="toast-count" hidden></span>'
    + '<button type="button" class="toast-close" aria-label="Dismiss notification">' + icon('x', 13) + '</button>';
  t.querySelector('.toast-msg').textContent = text;
  t.querySelector('.toast-close').addEventListener('click', () => dismissToast(t));
  t.addEventListener('mouseenter', () => clearTimeout(t._timer));
  t.addEventListener('mouseleave', () => armToast(t));
  region.appendChild(t);
  while (region.children.length > TOAST_MAX) region.firstElementChild.remove();
  armToast(t);
}

// ── T2 Persistence (qcode.* localStorage) ──
const PERSIST_KEYS = ['provider','model','reasoning','theme','activeTab','showThinking','filesSubtab','sessionId'];
function persistPrefs() {
  try {
    const o = {};
    for (const k of PERSIST_KEYS) o[k] = state[k] !== undefined ? state[k] : null;
    localStorage.setItem('qcode-prefs', JSON.stringify(o));
  } catch (_) {}
}
function restorePrefs() {
  try {
    const raw = localStorage.getItem('qcode-prefs');
    if (!raw) return;
    const o = JSON.parse(raw);
    for (const k of PERSIST_KEYS) {
      if (o[k] !== undefined && o[k] !== null) {
        if (k === 'theme' && !THEMES[o[k]]) continue;
        state[k] = o[k];
      }
    }
  } catch (_) {}
}
function persistDraft() {
  try {
    if (!state.sessionId || !promptInput) return;
    localStorage.setItem('qcode-draft-' + state.sessionId, promptInput.value);
  } catch (_) {}
}
function restoreDraft() {
  try {
    if (!state.sessionId || !promptInput) return;
    const d = localStorage.getItem('qcode-draft-' + state.sessionId);
    if (d) { promptInput.value = d; resizePromptInput(); }
  } catch (_) {}
}
function clearDraft() {
  try {
    if (state.sessionId) localStorage.removeItem('qcode-draft-' + state.sessionId);
  } catch (_) {}
}


// ═══════════════════════════════════════════════════════════════════
//  MULTI-SESSION TABS HELPERS
// ═══════════════════════════════════════════════════════════════════

async function createNewSession(title = '', workspace = '') {
  try {
    const resolved = resolveProviderModel(state.provider, state.model);
    if (!resolved.ok) {
      showToast('No provider/model configured');
      return;
    }
    state.provider = resolved.provider;
    state.model = resolved.model;

    const res = await fetch('/sessions', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        provider: resolved.provider,
        model: resolved.model,
        workspace,
        title,
        custom_id: title
      })
    });
    if (!res.ok) throw new Error(await responseError(res));
    {
      const data = await res.json();
      const resolvedTitle = (data.title && data.title !== data.id)
        ? data.title
        : (title || data.title || 'Session - ' + resolved.model);
      const newSession = {
        id: data.id,
        title: resolvedTitle,
        workspace: (data.workspace !== undefined ? data.workspace : workspace) || '',
        messages: [],
        generating: false,
        stream: null,
        provider: resolved.provider,
        model: resolved.model
      };

      state.openSessions.push(newSession);
      switchSession(data.id);
      showToast('New session created');
    }
  } catch (e) {
    showToast('Could not create session: ' + e.message, 'error');
    renderSessionTabs();
  }
}

// Pause/cancel the currently generating session. Safe to call from any
// focus context (document keydown, terminal, file editor, input box).
// No-op when nothing is generating or a modal is open.
function pauseActiveGeneration() {
  if (isModalOpen()) return;
  const activeSession = state.openSessions.find(s => s.id === state.sessionId);
  if (activeSession && activeSession.generating) {
    showToast('Paused — generation cancelled');
    cancelSession(activeSession.id);
  }
}

// Stop the session's turn. The UI resets at once and the stream's late events
// are dropped; the server sets the turn's abort flag, drops its queued
// prompts and persists the partial reply, which the refresh then shows.
function cancelSession(id) {
  const session = state.openSessions.find(s => s.id === id);
  if (!session) return;
  const stream = session.stream;
  session.stops = (session.stops || 0) + 1;
  if (stream) {
    stream.stopped = true;
    if (stream.turn != null) session.stoppedTurn = stream.turn;
    if (stream.reader) stream.reader.cancel().catch(() => {});
  }
  session.stream = null;
  session.generating = false;
  session.promptQueue = [];
  renderSessionTabs();

  if (id === state.sessionId) {
    setGenerating(false);
    renderMessages();
    updateQueueIndicator();
  }

  postCancel(id).then(() => refreshSession(session, { force: true }));
}

let pendingSwitchId = null;
async function switchSession(id) {
  const session = state.openSessions.find(s => s.id === id);
  if (!session) return;
  // Close the drawer afterwards only if the user had it open for this switch; the
  // startup load can finish seconds later and must not shut a drawer opened meanwhile.
  const drawerWasOpen = !!document.getElementById('sidebar')?.classList.contains('active');

  // Immediate state update on click for snappy feedback
  pendingSwitchId = id;
  state.sessionId = id;
  state.sessionTitle = stripTags(session.title || '');
  state.sessionWorkspace = stripTags(session.workspace || '');
  state.agentMode = session.agentMode || (id.indexOf('ses_') === 0 ? 'subagent' : 'normal');
  if (session.reasoning) {
    state.reasoning = session.reasoning;
    if (reasoningSelect) reasoningSelect.value = session.reasoning;
  }

  // Reset file browser/editor when switching sessions (discard unsaved).
  state.fsDir = '';
  state.fsOpenPath = null;
  state.fsSavedContent = '';
  state.fsDirty = false;
  state.fsReady = false;
  state.fsListingRunIndex = 0;
  if (state.fsCache && state.fsCache[id]) delete state.fsCache[id];
  clearFsEditor();
  if (fsEditorPath) fsEditorPath.textContent = 'No file open';
  if (fsCloseBtn) fsCloseBtn.disabled = true;
  if (fsSaveBtn) fsSaveBtn.disabled = true;
  if (fsDirtyBadge) fsDirtyBadge.classList.add('hidden');
  if (fsEditorStatus) fsEditorStatus.textContent = '';

  updateStatusBar();
  renderSessionTabs();
  persistPrefs();

  // Load files immediately if in files tab
  if (state.activeTab === 'files') {
    loadFilesTab();
  }

  // Instant Session Switching: If session already has cached messages, render
  // immediately without wiping chat or showing the loading spinner.
  const hasCachedMessages = isSessionSwitchCached(session);
  const isStub = shouldShowSessionLoading(session);

  if (!isStub && hasCachedMessages) {
    if (session.title) state.sessionTitle = stripTags(session.title);
    if (session.workspace) state.sessionWorkspace = stripTags(session.workspace);
    if (session.reasoning) {
      state.reasoning = session.reasoning;
      if (reasoningSelect) reasoningSelect.value = session.reasoning;
    }
    const resolved = resolveProviderModel(
      session.provider || state.provider,
      session.model || state.model
    );
    if (resolved.ok) {
      applyProviderModel(resolved.provider, resolved.model);
      session.provider = resolved.provider;
      session.model = resolved.model;
    }
    const expectedHash = `#/session/${id}`;
    if (window.location.hash !== expectedHash) {
      window.location.hash = `/session/${id}`;
    }
    persistPrefs();
    setGenerating(session.generating);
    if (state.activeTab === 'chat') {
      renderMessages();
      scrollToBottom(true);
    }
    updateStatusBar();
    if (drawerWasOpen) closeMobileSidebar();
    updateQueueIndicator();
    restoreDraft();

    if (state.activeTab === 'files') loadFilesTab();
    else if (state.activeTab === 'stats') loadStatsTab();
    else if (state.activeTab === 'sessions') loadDelegatedSessionsTab();

    // Async background refresh: check for updates/tail and vendor jobs
    const richJobs = richVendorJobsForMessages(session.messages);
    if (richJobs.length) {
      Promise.all(richJobs).then(() => {
        if (state.sessionId === id && state.activeTab === 'chat') {
          renderMessages();
        }
      });
    }
    refreshSession(session);
    return;
  }

  if (isStub) {
    state.loadingSessionId = id;
    renderSessionTabs();
    if (messagesEl && state.activeTab === 'chat') {
      messagesEl.innerHTML = `
        <div class="session-loading-window">
          <div class="session-loading-card">
            <div class="session-loading-spinner-wrap">
              <span class="qcode-spinner session-loading-spinner"></span>
            </div>
            <div class="session-loading-title">Loading session…</div>
            <div class="session-loading-sub">${esc(session.title || id)}</div>
          </div>
        </div>
      `;
    }
    try {
      await ensureSessionLoaded(session);
    } catch (e) {
      console.error('Failed to load session', id, e);
      if (pendingSwitchId === id) showToast('Could not load session — try again');
      state.loadingSessionId = null;
      renderSessionTabs();
      return;
    } finally {
      if (pendingSwitchId === id) {
        state.loadingSessionId = null;
      }
    }
    if (pendingSwitchId !== id) return;
  }

  // Rich libraries (marked, highlight.js) load in the background if not ready yet
  const richJobs = session.messages && session.messages.length
    ? richVendorJobsForMessages(session.messages) : [];
  if (richJobs.length) {
    await Promise.all(richJobs);
    if (pendingSwitchId !== id) return;
  }

  if (session.title) state.sessionTitle = stripTags(session.title);
  if (session.workspace) state.sessionWorkspace = stripTags(session.workspace);
  if (session.reasoning) {
    state.reasoning = session.reasoning;
    if (reasoningSelect) reasoningSelect.value = session.reasoning;
  }

  // Restore provider/model from the session, falling back to a valid
  // configured pair when the stored values are empty or stale.
  const resolved = resolveProviderModel(
    session.provider || state.provider,
    session.model || state.model
  );
  if (resolved.ok) {
    applyProviderModel(resolved.provider, resolved.model);
    session.provider = resolved.provider;
    session.model = resolved.model;
  }

  const expectedHash = `#/session/${id}`;
  if (window.location.hash !== expectedHash) {
    window.location.hash = `/session/${id}`;
  }

  persistPrefs();
  setGenerating(session.generating);
  renderMessages();
  scrollToBottom(true);
  renderSessionTabs();
  updateStatusBar();
  if (drawerWasOpen) closeMobileSidebar();
  updateQueueIndicator();
  restoreDraft();

  // Refresh the auxiliary tabs for the newly active session.
  if (state.activeTab === 'files') loadFilesTab();
  else if (state.activeTab === 'stats') loadStatsTab();
  else if (state.activeTab === 'sessions') loadDelegatedSessionsTab();
  // Reattaches to a running turn (e.g. after a reload) or picks up history
  // written while this session was in the background.
  refreshSession(session);
}

function closeSessionTab(id) {
  const index = state.openSessions.findIndex(s => s.id === id);
  if (index === -1) return;
  const wasActive = state.sessionId === id;
  state.openSessions.splice(index, 1);
  if (wasActive) {
    if (state.parentSessionId && state.openSessions.some(s => s.id === state.parentSessionId)) {
      returnToParentSession();
    } else if (state.openSessions.length > 0) {
      const nextIndex = Math.min(index, state.openSessions.length - 1);
      switchSession(state.openSessions[nextIndex].id);
    } else {
      state.sessionId = null;
      createNewSession();
    }
  } else {
    renderSessionTabs();
  }
}

async function deleteSessionPermanently(id) {
  try {
    let res;
    try {
      res = await fetch(`/session/${id}`, { method: 'DELETE' });
    } catch (_) {
      throw new Error('check your connection and try again');
    }
    if (!res.ok) {
      throw new Error(await responseError(res));
    }
    // Remove deleted session and any child sessions linked to it
    const removedIds = new Set([id]);
    for (const s of state.openSessions) {
      if (s.parentSessionId === id || (s.id && s.id.startsWith('ses_') && state.parentSessionId === id)) {
        removedIds.add(s.id);
      }
    }
    state.openSessions = state.openSessions.filter(s => !removedIds.has(s.id));
    // Drafts are keyed by session id; a deleted session's draft would otherwise sit in localStorage forever.
    for (const rid of removedIds) {
      try { localStorage.removeItem('qcode-draft-' + rid); } catch (_) {}
    }

    if (removedIds.has(state.sessionId)) {
      if (state.openSessions.length > 0) {
        switchSession(state.openSessions[0].id);
      } else {
        state.sessionId = null;
        await createNewSession();
      }
    } else {
      renderSessionTabs();
    }
    if (state.activeTab === 'sessions') {
      loadDelegatedSessionsTab();
    }
    showToast("Session and child subagents deleted.");
  } catch (e) {
    showToast("Could not delete session: " + e.message, 'error');
  }
}



function renderSessionTabs() {
  if (!sessionTabsContainer) return;

  try {
    const raw = localStorage.getItem('qcode-pinned');
    if (raw) state.pinned = JSON.parse(raw);
  } catch (_) {}
  state.pinned = Array.isArray(state.pinned) ? state.pinned : [];
  let list = [...state.openSessions];
  const q = (state.sessionsFilterText || '').toLowerCase().trim();
  if (q) list = list.filter(x => ((x.title||'') + ' ' + (x.workspace||'') + ' ' + x.id).toLowerCase().includes(q));
  list.sort((a, b) => ((state.pinned.includes(b.id)?1:0) - (state.pinned.includes(a.id)?1:0)));
  if (list.length === 0) {
    sessionTabsContainer.innerHTML = q
      ? '<div class="session-list-empty">No sessions match “' + esc(q) + '”.</div>'
      : '<div class="session-list-empty">No sessions yet. Click + to start one.</div>';
    return;
  }
  const pinIcon = (on) => `<svg class="ui-icon" width="13" height="13" viewBox="0 0 24 24" fill="${on ? 'currentColor' : 'none'}" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2"/></svg>`;
  updateActiveSessionCountBadge();
  sessionTabsContainer.innerHTML = list.map(session => {
    const isActive = session.id === state.sessionId;
    const isPinned = state.pinned.includes(session.id);
    const activeClass = isActive && state.activeTab === 'chat' ? 'active' : '';
    const title = session.title || 'Session';
    const ws = session.workspace ? shortPath(session.workspace) : '';
    const isSub = session.agentMode === 'subagent' || (session.id && session.id.startsWith('ses_'));
    const isLoading = session.id === state.loadingSessionId;
    const isRunning = session.generating;
    const lead = isRunning
      ? '<span class="session-status-indicator running" aria-label="running session" title="Running…"><span class="session-active-dot"></span></span>'
      : (isLoading
          ? '<span class="session-status-indicator loading" aria-label="loading session" title="Loading session…"><span class="qcode-spinner"></span></span>'
          : (isSub ? `<span class="session-icon sub">${icon('bot', 14)}</span>` : (isPinned ? `<span class="session-icon pin">${pinIcon(true)}</span>` : '')));
    return `
      <div class="session-item-wrapper ${activeClass}${isLoading && !isRunning ? ' is-loading' : ''} ${isSub ? 'is-subagent' : ''}${isPinned ? ' pinned' : ''}" data-id="${session.id}">
        <div class="session-item-main" data-id="${session.id}" role="button" tabindex="0">
          <div class="session-item-title-row">
            ${lead}
            <span class="session-title-text" title="${esc(title)}">${esc(title)}</span>
            ${isPinned && (isActive || session.generating) ? `<span class="session-icon pin" style="margin-left: auto; margin-right: 2px;">${pinIcon(true)}</span>` : ''}
          </div>
          ${ws ? `<div class="session-item-workspace" title="${esc(session.workspace)}">${esc(ws)}</div>` : ''}
        </div>
        <div class="session-item-actions">
          <button class="session-action-btn pin-session-btn${isPinned ? ' on' : ''}" data-id="${session.id}" title="${isPinned ? 'Unpin' : 'Pin'}" aria-label="${isPinned ? 'Unpin' : 'Pin'} session: ${esc(title)}">${pinIcon(isPinned)}</button>
          ${isSub
            ? `<button class="session-action-btn close-session-tab-btn" data-id="${session.id}" title="Close (keeps saved session)" aria-label="Close session: ${esc(title)}">${icon('x', 13)}</button>`
            : `<button class="session-action-btn rename-session-btn" data-id="${session.id}" data-title="${esc(title)}" title="Rename" aria-label="Rename session: ${esc(title)}">${SVG_ICONS.rename}</button>`}
          <button class="session-action-btn delete-session-btn" data-id="${session.id}" title="Delete permanently" aria-label="Delete session: ${esc(title)}">${SVG_ICONS.delete}</button>
        </div>
      </div>
    `;
  }).join('');

  sessionTabsContainer.querySelectorAll('.session-item-main').forEach(el => {
    el.addEventListener('click', () => {
      switchSession(el.dataset.id);
    });
    el.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); switchSession(el.dataset.id); }
    });
  });

  sessionTabsContainer.querySelectorAll('.rename-session-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      showRenameModal(btn.dataset.id, btn.dataset.title);
    });
  });

  sessionTabsContainer.querySelectorAll('.close-session-tab-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      closeSessionTab(btn.dataset.id);
    });
  });

  sessionTabsContainer.querySelectorAll('.delete-session-btn').forEach(btn => {
    btn.addEventListener('click', async (e) => {
      e.stopPropagation();
      const confirmed = confirm("Are you sure you want to permanently delete this session and all its messages?");
      if (confirmed) {
        await deleteSessionPermanently(btn.dataset.id);
      }
    });
  });
  sessionTabsContainer.querySelectorAll('.pin-session-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      state.pinned = state.pinned || [];
      const i = state.pinned.indexOf(btn.dataset.id);
      if (i >= 0) state.pinned.splice(i, 1); else state.pinned.push(btn.dataset.id);
      try { localStorage.setItem('qcode-pinned', JSON.stringify(state.pinned)); } catch (_) {}
      renderSessionTabs();
    });
  });


}
