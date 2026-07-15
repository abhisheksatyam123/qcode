
function openMobileSidebar() {
  const sidebar = document.getElementById('sidebar');
  const overlay = document.getElementById('sidebar-overlay');
  if (sidebar) sidebar.classList.add('active');
  if (overlay) overlay.classList.add('active');
}

function closeMobileSidebar() {
  const sidebar = document.getElementById('sidebar');
  const overlay = document.getElementById('sidebar-overlay');
  if (sidebar) sidebar.classList.remove('active');
  if (overlay) overlay.classList.remove('active');
}

// ── QCode Web UI ──────────────────────────────────────────────────
const state = {
  provider: '',
  model: '',
  reasoning: 'off',
  toolsEnabled: true,
  openSessions: [], // Array of { id, title, workspace, messages, generating, reader, provider, model }
  providers: [],
  sessionId: null,  // Active session ID
  sessionTitle: '',
  sessionWorkspace: '',
  // UI Tabs & layout state
  activeTab: 'chat', // 'chat' | 'terminal' | 'files' | 'stats'
  layoutMode: 'tab', // 'tab' or 'split'
  terminalOpen: false
};

// ── DOM refs ──
const messagesEl = document.getElementById('messages');
const promptInput = document.getElementById('prompt-input');
const sendBtn = document.getElementById('send-btn');
const clearBtn = document.getElementById('clear-btn');
const providerSelect = document.getElementById('provider-select');
const modelSelect = document.getElementById('model-select');
const reasoningSelect = document.getElementById('reasoning-select');
const modalOverlay = document.getElementById('modal-overlay');
const statusBar = document.getElementById('status-bar');
const statusSession = document.getElementById('header-session-title');
const statusWorkspace = document.getElementById('header-session-workspace');
const newSessionBtn = document.getElementById('new-session-btn');
const toggleTerminalBtn = document.getElementById('toggle-terminal-btn');
const terminalPanel = document.getElementById('terminal-panel');
const terminalContainer = document.getElementById('terminal-container');
const terminalCloseBtn = document.getElementById('terminal-close-btn');
const filesPanel = document.getElementById('files-panel');
const filesContent = document.getElementById('files-content');
const filesRefreshBtn = document.getElementById('files-refresh-btn');
const statsPanel = document.getElementById('stats-panel');
const statsContent = document.getElementById('stats-content');
const statsRefreshBtn = document.getElementById('stats-refresh-btn');
const tabFilesBtn = document.getElementById('tab-files-btn');
const tabStatsBtn = document.getElementById('tab-stats-btn');

// New DOM refs for tabs & layout toggle
const mainEl = document.getElementById('main');
const mainContentWrapperEl = document.getElementById('main-content-wrapper');
const tabTerminalBtn = document.getElementById('tab-terminal-btn');
const tabChatBtn = document.getElementById('tab-chat-btn');
const layoutToggleBtn = document.getElementById('layout-toggle-btn');
const sessionTabsContainer = document.getElementById('session-tabs-container');

// ── Modal state ──
let pickerCleanup = null;

// ── Terminal state ──
let term = null;       // xterm instance
let fitAddon = null;   // xterm fit addon
let termId = null;     // server terminal session id
let termPollTimer = null;

// ── Slash commands ──
const SLASH_COMMANDS = [
  { name: '/help',      desc: 'Show available commands' },
  { name: '/model',     desc: 'List or switch provider/model' },
  { name: '/new',       desc: 'Start a new chat session' },
  { name: '/reasoning', desc: 'Set reasoning: off|low|medium|high' },
  { name: '/tools',     desc: 'Toggle tool use: on|off' },
  { name: '/rename',    desc: 'Rename current session' },
  { name: '/session',   desc: 'List or load saved sessions' },
  { name: '/compact',   desc: 'Summarize conversation' },
];

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
  if (hash === '#/terminal') {
    if (state.activeTab !== 'terminal') {
      switchTab('terminal');
    }
    return;
  }
  
  const sid = getSessionIdFromHash();
  if (sid) {
    if (sid !== state.sessionId) {
      let session = state.openSessions.find(s => s.id === sid);
      if (session) {
        switchSession(sid);
      } else {
        session = await loadSessionData(sid);
        if (session && session.title) {
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
  await loadProviders();
  setupEventListeners();
  updateStatusBar();
  
  try {
    const sessionsRes = await fetch('/sessions');
    if (sessionsRes.ok) {
      const sessions = await sessionsRes.json();
      if (Array.isArray(sessions) && sessions.length > 0) {
        let lastId = getSessionIdFromHash();
        if (!lastId) {
          const lastRes = await fetch('/session/last');
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

        for (const s of sessions) {
          const session = await loadSessionData(s.id);
          state.openSessions.push(session);
        }
        
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
    providerSelect.innerHTML = state.providers.map((p, i) =>
      `<option value="${i}">${p.name}</option>`
    ).join('');
    onProviderChange(0);
    providerSelect.addEventListener('change', () => {
      onProviderChange(parseInt(providerSelect.value));
    });
  } catch (e) {
    showToast('Failed to load providers: ' + e.message);
  }
}

function onProviderChange(idx) {
  const p = state.providers[idx];
  if (!p) return;
  state.provider = p.id;
  modelSelect.innerHTML = p.models.map(m =>
    `<option value="${m.id}">${m.name}</option>`
  ).join('');
  if (p.models.length > 0) state.model = p.models[0].id;
}

// Apply a provider/model selection to both the dropdowns and state.
// Used by the model picker and when restoring a session.
function applyProviderModel(providerId, modelId) {
  const pidx = state.providers.findIndex(p => p.id === providerId || p.name === providerId);
  if (pidx < 0) {
    state.provider = providerId;
    state.model = modelId;
    return false;
  }
  const p = state.providers[pidx];
  providerSelect.value = pidx;
  onProviderChange(pidx);
  const midx = p.models.findIndex(m => m.id === modelId || m.name === modelId);
  const resolvedModelId = midx >= 0 ? p.models[midx].id : (p.models.length > 0 ? p.models[0].id : modelId);
  if (midx >= 0) {
    modelSelect.value = resolvedModelId;
  }
  state.provider = p.id;
  state.model = resolvedModelId;
  return true;
}

function setupEventListeners() {
  sendBtn.addEventListener('click', sendMessage);
  promptInput.addEventListener('keydown', handleInputKeydown);
  clearBtn.addEventListener('click', () => {
    const session = state.openSessions.find(s => s.id === state.sessionId);
    if (session) {
      session.messages = [];
      renderMessages();
    }
  });
  reasoningSelect.addEventListener('change', () => {
    state.reasoning = reasoningSelect.value;
  });
  modelSelect.addEventListener('change', () => {
    state.model = modelSelect.value;
    const session = state.openSessions.find(s => s.id === state.sessionId);
    if (session) {
      session.model = state.model;
    }
  });
  providerSelect.addEventListener('change', () => {
    const session = state.openSessions.find(s => s.id === state.sessionId);
    if (session) {
      session.provider = state.provider;
      session.model = state.model;
    }
  });
  promptInput.addEventListener('input', () => {
    updateSlashMenu();
  });
  document.addEventListener('click', (e) => {
    if (slashMenuEl && !slashMenuEl.contains(e.target) && e.target !== promptInput) {
      closeSlashMenu();
    }
  });
  if (newSessionBtn) {
    newSessionBtn.addEventListener('click', showNewSessionModal);
  }
  
  // Tab '+' button listener
  const newTabBtn = document.getElementById('new-tab-btn');
  if (newTabBtn) {
    newTabBtn.addEventListener('click', showNewSessionModal);
  }

  if (toggleTerminalBtn) {
    toggleTerminalBtn.addEventListener('click', toggleTerminal);
  }
  if (terminalCloseBtn) {
    terminalCloseBtn.addEventListener('click', closeTerminal);
  }
  
  // Tab and layout event listeners
  if (tabChatBtn) {
    tabChatBtn.addEventListener('click', () => switchTab('chat'));
  }
  if (tabTerminalBtn) {
    tabTerminalBtn.addEventListener('click', () => switchTab('terminal'));
  }
  if (tabFilesBtn) {
    tabFilesBtn.addEventListener('click', () => switchTab('files'));
  }
  if (tabStatsBtn) {
    tabStatsBtn.addEventListener('click', () => switchTab('stats'));
  }
  if (filesRefreshBtn) {
    filesRefreshBtn.addEventListener('click', () => loadFilesTab());
  }
  if (statsRefreshBtn) {
    statsRefreshBtn.addEventListener('click', () => loadStatsTab());
  }
  if (layoutToggleBtn) {
    layoutToggleBtn.addEventListener('click', toggleLayoutMode);
  }

  // Mobile sidebar event listeners
  const menuToggleBtn = document.getElementById('menu-toggle-btn');
  if (menuToggleBtn) {
    menuToggleBtn.addEventListener('click', openMobileSidebar);
  }
  const sidebarCloseBtn = document.getElementById('sidebar-close-btn');
  if (sidebarCloseBtn) {
    sidebarCloseBtn.addEventListener('click', closeMobileSidebar);
  }
  const sidebarOverlay = document.getElementById('sidebar-overlay');
  if (sidebarOverlay) {
    sidebarOverlay.addEventListener('click', closeMobileSidebar);
  }

  // Escape key cancels active generation
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      const activeSession = state.openSessions.find(s => s.id === state.sessionId);
      if (activeSession && activeSession.generating) {
        showToast('Cancelling generation...');
        cancelSession(activeSession.id);
      }
    }
  });

  window.addEventListener('hashchange', handleHashChange);
}

// ── Status Bar ──
function updateStatusBar() {
  statusSession.textContent = state.sessionTitle || (state.sessionId ? 'Session: ' + state.sessionId.substring(0,8) + '…' : 'No session');
  statusWorkspace.textContent = state.sessionWorkspace ? '📁 ' + state.sessionWorkspace : '';
}

// ═══════════════════════════════════════════════════════════════════
//  NEW SESSION MODAL
// ═══════════════════════════════════════════════════════════════════

function showNewSessionModal() {
  modalOverlay.innerHTML = `
    <div class="picker-modal">
      <div class="picker-header">New Session</div>
      <input class="rename-input" id="ns-title" type="text" placeholder="Session title (optional)" />
      <input class="rename-input" id="ns-workspace" type="text" placeholder="Workspace directory (e.g. /home/user/project)" />
      <div class="modal-actions">
        <button class="modal-btn secondary" id="ns-cancel">Cancel</button>
        <button class="modal-btn primary" id="ns-create">Create</button>
      </div>
    </div>`;
  modalOverlay.classList.add('active');
  document.getElementById('ns-title').focus();

  function close() { modalOverlay.classList.remove('active'); modalOverlay.innerHTML = ''; }

  document.getElementById('ns-cancel').addEventListener('click', close);
  modalOverlay.addEventListener('click', function bd(e) {
    if (e.target === modalOverlay) { modalOverlay.removeEventListener('click', bd); close(); }
  });

  async function doCreate() {
    const title = document.getElementById('ns-title').value.trim();
    const workspace = document.getElementById('ns-workspace').value.trim();
    await createNewSession(title, workspace);
    close();
  }

  document.getElementById('ns-create').addEventListener('click', doCreate);
  document.getElementById('ns-workspace').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); doCreate(); }
    if (e.key === 'Escape') { e.preventDefault(); close(); }
    e.stopPropagation();
  });
  document.getElementById('ns-title').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); document.getElementById('ns-workspace').focus(); }
    if (e.key === 'Escape') { e.preventDefault(); close(); }
    e.stopPropagation();
  });
}

// ═══════════════════════════════════════════════════════════════════
//  TERMINAL
// ═══════════════════════════════════════════════════════════════════

function toggleTerminal() {
  if (!state.terminalOpen) {
    state.terminalOpen = true;
    const ws = state.sessionWorkspace || '';
    startTerminal(ws);
    if (state.layoutMode === 'tab') {
      switchTab('terminal');
    } else {
      updateLayoutUI();
    }
  } else {
    closeTerminal();
  }
}

async function startTerminal(workspace) {
  // Cleanup existing terminal
  if (term) { term.dispose(); term = null; }
  if (termPollTimer) { clearInterval(termPollTimer); termPollTimer = null; }
  if (termId) {
    try { await fetch('/terminal/' + termId, { method: 'DELETE' }); } catch(e) {}
    termId = null;
  }
  terminalContainer.innerHTML = '';

  try {
    const res = await fetch('/terminal/create', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ workspace })
    });
    if (!res.ok) throw new Error(await res.text());
    const data = await res.json();
    termId = data.id;

    // Create xterm with Fira Code and fit addon
    term = new Terminal({
      fontFamily: 'Fira Code, "DejaVu Sans Mono", "Segoe UI Emoji", monospace',
      fontSize: 14,
      theme: { background: '#0d1117', foreground: '#c9d1d9', cursor: '#64ffda' }
    });
    fitAddon = new FitAddon.FitAddon();
    term.loadAddon(fitAddon);
    term.open(terminalContainer);
    
    setTimeout(() => {
      if (fitAddon) {
        try {
          fitAddon.fit();
          sendResize(term.cols, term.rows);
        } catch (e) {}
      }
    }, 50);

    term.onResize(size => {
      sendResize(size.cols, size.rows);
    });

    // Send input to server
    term.onData((data) => {
      fetch('/terminal/' + termId + '/input', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ data })
      }).catch(() => {});
    });

    // Poll for output
    termPollTimer = setInterval(async () => {
      if (!termId) return;
      try {
        const res = await fetch('/terminal/' + termId + '/stream');
        if (res.ok) {
          const text = await res.text();
          if (text) term.write(text);
        }
      } catch (e) {}
    }, 80);

  } catch (e) {
    showToast('Terminal error: ' + e.message);
    state.terminalOpen = false;
    updateLayoutUI();
  }
}

async function closeTerminal() {
  state.terminalOpen = false;
  if (termPollTimer) { clearInterval(termPollTimer); termPollTimer = null; }
  if (term) { term.dispose(); term = null; }
  if (termId) {
    try { await fetch('/terminal/' + termId, { method: 'DELETE' }); } catch(e) {}
    termId = null;
  }
  if (state.layoutMode === 'tab') {
    switchTab('chat');
  } else {
    updateLayoutUI();
  }
}

function switchTab(tab) {
  if (state.layoutMode === 'split') {
    state.layoutMode = 'tab';
  }
  state.activeTab = tab;
  if (tab === 'terminal' && !state.terminalOpen) {
    state.terminalOpen = true;
    const ws = state.sessionWorkspace || '';
    startTerminal(ws);
  }
  if (tab === 'files') loadFilesTab();
  if (tab === 'stats') loadStatsTab();
  updateLayoutUI();
  closeMobileSidebar();
}

function toggleLayoutMode() {
  state.layoutMode = state.layoutMode === 'tab' ? 'split' : 'tab';
  if (state.layoutMode === 'split' && !state.terminalOpen) {
    state.terminalOpen = true;
    const ws = state.sessionWorkspace || '';
    startTerminal(ws);
  }
  updateLayoutUI();
}

function updateLayoutUI() {
  if (state.layoutMode === 'split') {
    if (mainContentWrapperEl) mainContentWrapperEl.classList.add('split-view');
    if (mainEl) mainEl.classList.remove('hidden');
    if (terminalPanel) terminalPanel.classList.remove('hidden');
    // Split is chat + terminal only — never stack files/stats alongside.
    if (filesPanel) filesPanel.classList.add('hidden');
    if (statsPanel) statsPanel.classList.add('hidden');

    [tabChatBtn, tabTerminalBtn, tabFilesBtn, tabStatsBtn].forEach(b => {
      if (b) b.classList.remove('active');
    });
    if (tabTerminalBtn) tabTerminalBtn.classList.add('active');
    if (tabChatBtn) tabChatBtn.classList.add('active');
    if (layoutToggleBtn) layoutToggleBtn.innerHTML = '<span class="layout-icon">🔲</span> Tab View';
  } else {
    if (mainContentWrapperEl) mainContentWrapperEl.classList.remove('split-view');
    if (layoutToggleBtn) layoutToggleBtn.innerHTML = '<span class="layout-icon">🔲</span> Split View';

    // Exclusive tab mode: hide every pane, then show only the active one.
    if (mainEl) mainEl.classList.add('hidden');
    if (terminalPanel) terminalPanel.classList.add('hidden');
    if (filesPanel) filesPanel.classList.add('hidden');
    if (statsPanel) statsPanel.classList.add('hidden');
    [tabChatBtn, tabTerminalBtn, tabFilesBtn, tabStatsBtn].forEach(b => {
      if (b) b.classList.remove('active');
    });

    if (state.activeTab === 'chat') {
      if (mainEl) mainEl.classList.remove('hidden');
      if (tabChatBtn) tabChatBtn.classList.add('active');
    } else if (state.activeTab === 'terminal') {
      if (terminalPanel) terminalPanel.classList.remove('hidden');
      if (tabTerminalBtn) tabTerminalBtn.classList.add('active');
    } else if (state.activeTab === 'files') {
      if (filesPanel) filesPanel.classList.remove('hidden');
      if (tabFilesBtn) tabFilesBtn.classList.add('active');
    } else if (state.activeTab === 'stats') {
      if (statsPanel) statsPanel.classList.remove('hidden');
      if (tabStatsBtn) tabStatsBtn.classList.add('active');
    }
  }
  renderSessionTabs();

  if (term && fitAddon && state.terminalOpen &&
      (state.layoutMode === 'split' || state.activeTab === 'terminal')) {
    setTimeout(() => {
      try {
        fitAddon.fit();
      } catch (e) {}
    }, 50);
  }
}

// ═══════════════════════════════════════════════════════════════════
//  MODAL / PICKER SYSTEM
// ═══════════════════════════════════════════════════════════════════

function isModalOpen() {
  return modalOverlay.classList.contains('active');
}

function closeModal() {
  modalOverlay.classList.remove('active');
  modalOverlay.innerHTML = '';
  if (pickerCleanup) { pickerCleanup(); pickerCleanup = null; }
}

function showPickerModal(title, items, activeId, onSelect) {
  let cursorIdx = items.findIndex(i => i.isActive);
  if (cursorIdx < 0) cursorIdx = 0;

  function render() {
    let html = `<div class="picker-modal">`;
    html += `<div class="picker-header">${esc(title)}</div>`;
    html += `<div class="picker-body">`;
    let lastCat = '';
    for (let i = 0; i < items.length; i++) {
      const it = items[i];
      if (it.category && it.category !== lastCat) {
        lastCat = it.category;
        html += `<div class="picker-category">${esc(it.category)}</div>`;
      }
      const cls = i === cursorIdx ? 'picker-item selected' : 'picker-item';
      let marker = i === cursorIdx ? '▶' : (it.isActive ? '●' : '');
      html += `<div class="${cls}" data-idx="${i}">`;
      html += `<span class="marker">${marker}</span>`;
      html += `<span class="item-label">${esc(it.label)}</span>`;
      if (it.sublabel) html += `<span class="item-id">${esc(it.sublabel)}</span>`;
      if (it.workspace) html += `<span class="item-id" title="${esc(it.workspace)}">📁 ${esc(shortPath(it.workspace))}</span>`;
      html += `</div>`;
    }
    html += `</div>`;
    html += `<div class="picker-footer">↑↓ navigate &middot; Enter select &middot; Esc cancel</div>`;
    html += `</div>`;
    modalOverlay.innerHTML = html;
    modalOverlay.classList.add('active');
    const selected = modalOverlay.querySelector('.picker-item.selected');
    if (selected) selected.scrollIntoView({ block: 'nearest' });
    modalOverlay.querySelectorAll('.picker-item').forEach(el => {
      el.addEventListener('click', () => { const idx = parseInt(el.dataset.idx); closeModal(); onSelect(items[idx]); });
    });
    modalOverlay.addEventListener('click', function bd(e) {
      if (e.target === modalOverlay) { modalOverlay.removeEventListener('click', bd); closeModal(); }
    });
  }

  render();

  function onKey(e) {
    if (!isModalOpen()) return;
    if (e.key === 'ArrowDown' || e.key === 'j') { e.preventDefault(); cursorIdx = Math.min(cursorIdx + 1, items.length - 1); render(); }
    else if (e.key === 'ArrowUp' || e.key === 'k') { e.preventDefault(); cursorIdx = Math.max(cursorIdx - 1, 0); render(); }
    else if (e.key === 'Enter') { e.preventDefault(); closeModal(); onSelect(items[cursorIdx]); }
    else if (e.key === 'Escape') { e.preventDefault(); closeModal(); }
  }
  document.addEventListener('keydown', onKey);
  pickerCleanup = () => document.removeEventListener('keydown', onKey);
}

// ── Fuzzy finder ──
// Subsequence match with light scoring (consecutive + word-boundary bonuses).
function fuzzyScore(query, text) {
  query = query.toLowerCase();
  text = text.toLowerCase();
  if (!query) return 0;            // empty query -> keep all items
  let qi = 0, score = 0, prev = -2;
  for (let ti = 0; ti < text.length && qi < query.length; ti++) {
    if (text[ti] === query[qi]) {
      score += 1;
      if (ti === prev + 1) score += 2;                       // consecutive
      const c = text[ti - 1];
      if (ti === 0 || c === ' ' || c === '/' || c === '-' || c === '_' || c === '.') score += 3; // word start
      prev = ti;
      qi++;
    }
  }
  return qi === query.length ? score : -1;  // -1 => not a subsequence
}

function fuzzyFilter(query, items) {
  if (!query) return items.slice();
  const out = [];
  for (const it of items) {
    const hay = [it.label, it.sublabel || '', it.category || '', it.workspace || ''].join(' ');
    const s = fuzzyScore(query, hay);
    if (s >= 0) out.push({ it, s });
  }
  out.sort((a, b) => b.s - a.s);
  return out.map(x => x.it);
}

// Command-palette style picker with live fuzzy filtering via a text input.
function showFuzzyPicker(title, items, activeId, onSelect, placeholder, initialQuery) {
  let query = initialQuery || '';
  let filtered = fuzzyFilter(query, items);
  let cursorIdx = Math.max(0, filtered.findIndex(i => i.id === activeId));
  if (cursorIdx < 0) cursorIdx = 0;

  modalOverlay.innerHTML = `
    <div class="picker-modal fuzzy-picker">
      <div class="picker-header">${esc(title)}</div>
      <input class="rename-input fuzzy-input" id="fuzzy-input" type="text"
             placeholder="${esc(placeholder || 'Type to filter...')}" value="${esc(query)}" autofocus />
      <div class="picker-body" id="fuzzy-body"></div>
      <div class="picker-footer">Type to filter &middot; &uarr;&darr; navigate &middot; Enter select &middot; Esc cancel</div>
    </div>`;
  modalOverlay.classList.add('active');
  const input = document.getElementById('fuzzy-input');
  const body = document.getElementById('fuzzy-body');

  function renderBody() {
    filtered = fuzzyFilter(query, items);
    if (cursorIdx >= filtered.length) cursorIdx = Math.max(0, filtered.length - 1);
    if (cursorIdx < 0) cursorIdx = 0;
    if (filtered.length === 0) {
      body.innerHTML = '<div class="picker-empty">No matches</div>';
      return;
    }
    let html = '';
    let lastCat = '';
    for (let i = 0; i < filtered.length; i++) {
      const it = filtered[i];
      if (it.category && it.category !== lastCat) {
        lastCat = it.category;
        html += `<div class="picker-category">${esc(lastCat)}</div>`;
      }
      const cls = i === cursorIdx ? 'picker-item selected' : 'picker-item';
      const marker = i === cursorIdx ? '▶' : (it.isActive ? '●' : '');
      html += `<div class="${cls}" data-idx="${i}">`;
      html += `<span class="marker">${marker}</span>`;
      html += `<span class="item-label">${esc(it.label)}</span>`;
      if (it.sublabel) html += `<span class="item-id">${esc(it.sublabel)}</span>`;
      if (it.workspace) html += `<span class="item-id" title="${esc(it.workspace)}">📁 ${esc(shortPath(it.workspace))}</span>`;
      html += `</div>`;
    }
    body.innerHTML = html;
    const sel = body.querySelector('.picker-item.selected');
    if (sel) sel.scrollIntoView({ block: 'nearest' });
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
    else if (ev.key === 'Escape') { ev.preventDefault(); closeModal(); }
  });
  input.focus();
  input.setSelectionRange(input.value.length, input.value.length);
}

function shortPath(p) {
  if (!p) return '';
  return p.length > 30 ? '…' + p.slice(-28) : p;
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
    modalOverlay.addEventListener('click', function bd(e) { if (e.target === modalOverlay) { modalOverlay.removeEventListener('click', bd); closeModal(); } });
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
    if (!title) { showToast('Enter a title'); return; }
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
      else showToast('Failed: ' + await res.text());
    } catch (e) { showToast('Failed: ' + e.message); }
    closeModal();
  }
  document.getElementById('rename-ok-btn').addEventListener('click', doRename);
  document.getElementById('rename-cancel-btn').addEventListener('click', closeModal);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); doRename(); }
    if (e.key === 'Escape') { e.preventDefault(); closeModal(); }
    e.stopPropagation();
  });
  modalOverlay.addEventListener('click', function bd(e) { if (e.target === modalOverlay) { modalOverlay.removeEventListener('click', bd); closeModal(); } });
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
    if (e.key === 'Escape') { closeSlashMenu(); return; }
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
  slashMenuEl.innerHTML = matches.map((m, i) =>
    `<div class="slash-menu-item${i === 0 ? ' active' : ''}" data-cmd="${m.name}"><span class="cmd-name">${m.name}</span><span class="cmd-desc">${m.desc}</span></div>`
  ).join('');
  slashMenuEl.style.display = 'block';
  slashMenuEl.querySelectorAll('.slash-menu-item').forEach(el => {
    el.addEventListener('click', () => { const cmd = el.dataset.cmd; promptInput.value = ''; closeSlashMenu(); handleSlashCommand(cmd); });
  });
}

function updateSlashMenuSelection(items) { items.forEach((el, i) => { el.classList.toggle('active', i === slashActiveIdx); }); }
function closeSlashMenu() { if (slashMenuEl) { slashMenuEl.style.display = 'none'; slashMenuEl.innerHTML = ''; } slashActiveIdx = -1; }

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

// ── Slash command dispatch ──
function handleSlashCommand(text) {
  const raw = text.slice(1).trim();
  const spaceIdx = raw.indexOf(' ');
  const cmd = (spaceIdx >= 0 ? raw.slice(0, spaceIdx) : raw).toLowerCase();
  const args = spaceIdx >= 0 ? raw.slice(spaceIdx + 1).trim() : '';
  switch (cmd) {
    case 'help': case '?': handleHelpCommand(); break;
    case 'model': case 'models': handleModelCommand(args); break;
    case 'new': handleNewCommand(); break;
    case 'reasoning': handleReasoningCommand(args); break;
    case 'tools': handleToolsCommand(args); break;
    case 'rename': handleRenameCommand(args); break;
    case 'session': case 'load': handleSessionCommand(args); break;
    case 'compact': handleCompactCommand(); break;
    default: addSystemMessage('Unknown command: /' + cmd + '. Type /help for available commands.');
  }
}

function handleHelpCommand() {
  addSystemMessage(
    'Available commands:\n' +
    '  /model [list]              - select provider/model\n' +
    '  /new                       - start a new session\n' +
    '  /reasoning [off|low|medium|high] - set reasoning effort\n' +
    '  /tools [on|off]            - toggle tool use\n' +
    '  /rename [title]            - rename current session\n' +
    '  /session [list]            - manage saved sessions\n' +
    '  /compact                   - compress conversation into handoff\n' +
    '  /help                      - show this help'
  );
}

// ── /model ──
function handleModelCommand(args) {
  const items = [];
  for (const p of state.providers) {
    for (const m of p.models) {
      items.push({ id: p.id + '||' + m.id, label: m.name, sublabel: m.id !== m.name ? m.id : '', category: p.name, isActive: p.id === state.provider && m.id === state.model });
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
  try { const res = await fetch('/sessions'); sessions = await res.json(); } catch (e) { showToast('Failed: ' + e.message); return; }
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

function parseMessages(msgs, session) {
  session.messages = [];
  let currentAssistantMsg = null;
  for (const m of msgs) {
    if (m.role === 'User' || m.role === 'user') {
      currentAssistantMsg = null;
      session.messages.push({ role: 'user', content: m.content });
    } else if (m.role === 'Assistant' || m.role === 'assistant') {
      if (!currentAssistantMsg) {
        currentAssistantMsg = { role: 'assistant', content: '', toolEvents: [] };
        session.messages.push(currentAssistantMsg);
      }
      currentAssistantMsg.content = m.content;
    } else if (m.role === 'ToolCall') {
      try {
        const tc = JSON.parse(m.content);
        if (!currentAssistantMsg) {
          currentAssistantMsg = { role: 'assistant', content: '', toolEvents: [] };
          session.messages.push(currentAssistantMsg);
        }
        currentAssistantMsg.toolEvents.push({
          type: 'tool_call',
          tool_call_id: tc.id,
          tool_name: tc.name,
          arguments: tc.arguments,
          status: 'running'
        });
      } catch (e) {}
    } else if (m.role === 'ToolResult') {
      try {
        const tr = JSON.parse(m.content);
        if (!currentAssistantMsg) {
          currentAssistantMsg = { role: 'assistant', content: '', toolEvents: [] };
          session.messages.push(currentAssistantMsg);
        }
        const tc = currentAssistantMsg.toolEvents.find(t => t.tool_call_id === tr.tool_call_id);
        if (tc) {
          tc.status = tr.is_error ? 'error' : 'success';
          tc.result = tr.result;
          tc.duration_ms = tr.duration_ms;
        } else {
          currentAssistantMsg.toolEvents.push({
            type: 'tool_call',
            tool_call_id: tr.tool_call_id,
            tool_name: 'tool',
            arguments: null,
            status: tr.is_error ? 'error' : 'success',
            result: tr.result,
            duration_ms: tr.duration_ms
          });
        }
      } catch (e) {}
    } else if (m.role === 'Reasoning') {
      if (!currentAssistantMsg) {
        currentAssistantMsg = { role: 'assistant', content: '', toolEvents: [] };
        session.messages.push(currentAssistantMsg);
      }
      try {
        const r = JSON.parse(m.content);
        currentAssistantMsg.reasoning = r.text || '';
      } catch (e) {
        currentAssistantMsg.reasoning = m.content;
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
    reader: null,
    provider: '',
    model: ''
  };

  try {
    const res = await fetch('/session/' + id);
    if (res.ok) {
      const info = await res.json();
      session.title = info.title || '';
      session.workspace = info.workspace || '';
      session.provider = info.provider || '';
      session.model = info.model || '';
    }
  } catch (e) {}

  try {
    const res = await fetch('/session/' + id + '/messages');
    if (res.ok) {
      const msgs = await res.json();
      if (Array.isArray(msgs)) {
        parseMessages(msgs, session);
      }
    }
  } catch (e) {}

  return session;
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
function handleReasoningCommand(args) {
  const lvl = args || 'off';
  if (!['off', 'low', 'medium', 'high'].includes(lvl)) { addSystemMessage('Invalid level. Use off|low|medium|high.'); return; }
  state.reasoning = lvl; reasoningSelect.value = lvl; showToast('Reasoning: ' + lvl);
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
    else showToast('Failed: ' + await res.text());
  } catch (e) { showToast('Failed: ' + e.message); }
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

async function sendMessage() {
  if (isModalOpen()) return;
  const text = promptInput.value.trim();
  if (!text) return;
  if (text.startsWith('/')) { promptInput.value = ''; handleSlashCommand(text); return; }

  const session = state.openSessions.find(s => s.id === state.sessionId);
  if (!session || session.generating) return;

  promptInput.value = '';
  
  session.messages.push({ role: 'user', content: text });
  
  const assistantMsg = { role: 'assistant', content: '', toolEvents: [] };
  session.messages.push(assistantMsg);

  if (session.id === state.sessionId) {
    renderMessages();
    scrollToBottom();
    setGenerating(true);
  }
  
  session.generating = true;
  renderSessionTabs();

  try {
    const body = JSON.stringify({
      text,
      provider: session.provider || state.provider,
      model: session.model || state.model,
      reasoning_mode: state.reasoning,
      session_id: session.id
    });
    
    const res = await fetch(`/session/${session.id}/generate`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
    if (!res.ok) throw new Error(await res.text());
    
    const reader = res.body.getReader();
    session.reader = reader;
    const decoder = new TextDecoder();
    let buffer = '';
    
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      const lines = buffer.split('\n');
      buffer = lines.pop() || '';
      for (const line of lines) {
        if (!line.trim()) continue;
        try {
          handleEvent(JSON.parse(line), assistantMsg, session);
        } catch (e) {}
      }
    }
  } catch (e) {
    if (e.name !== 'AbortError') {
      showToast('Error: ' + e.message);
      assistantMsg.content = 'Error: ' + e.message;
    }
  }

  session.generating = false;
  session.reader = null;
  renderSessionTabs();

  if (session.id === state.sessionId) {
    setGenerating(false);
    renderMessages();
    scrollToBottom();
  }
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
    case 'backend.message.delta':
      // Append (do NOT overwrite): backend sends incremental chunks and a
      // final empty-text delta with done=true, which would otherwise wipe
      // the accumulated assistant text (leaving only the token usage line).
      if (evt.text) msg.content = (msg.content || '') + evt.text;
      if (session.id === state.sessionId) {
        renderMessages();
        scrollToBottom();
      }
      break;
    case 'backend.reasoning.delta':
      if (!msg.reasoning) msg.reasoning = '';
      if (evt.text) msg.reasoning += evt.text;
      if (session.id === state.sessionId && !evt.done) { renderMessages(); scrollToBottom(); }
      break;
    case 'backend.token.usage.updated': {
      const parts = [];
      if (evt.prompt_tokens != null) parts.push('prompt ' + evt.prompt_tokens);
      if (evt.completion_tokens != null) parts.push('completion ' + evt.completion_tokens);
      if (evt.total_tokens != null) parts.push('total ' + evt.total_tokens);
      msg.usage = parts.length ? 'Tokens — ' + parts.join(' · ') : '';
      if (session.id === state.sessionId) { renderMessages(); scrollToBottom(); }
      break;
    }
    case 'backend.tool.call.started':
      if (!msg.toolEvents) msg.toolEvents = [];
      msg.toolEvents.push({ type: 'tool_call', tool_call_id: evt.tool_call_id, tool_name: evt.tool_name, arguments: evt.arguments, status: 'running' });
      if (session.id === state.sessionId) {
        renderMessages();
        scrollToBottom();
      }
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
      if (session.id === state.sessionId) {
        renderMessages();
        scrollToBottom();
      }
      break;
    case 'backend.error.occurred':
      showToast(evt.message);
      break;
  }
}

// ═══════════════════════════════════════════════════════════════════
//  RENDERING
// ═══════════════════════════════════════════════════════════════════

async function loadFilesTab() {
  if (!state.sessionId) {
    filesContent.innerHTML = '<div class="files-empty">No active session.</div>';
    return;
  }
  filesContent.innerHTML = '<div class="files-empty">Loading working tree…</div>';
  try {
    const res = await fetch('/session/' + state.sessionId + '/files');
    if (!res.ok) throw new Error(await res.text());
    const data = await res.json();
    renderFilesTab(data);
  } catch (e) {
    filesContent.innerHTML = '<div class="files-empty">Failed to load files: ' + esc(e.message) + '</div>';
  }
}

function renderFilesTab(data) {
  const ws = data.workspace || '';
  let html = '';
  html += '<div class="files-workspace">📁 ' + esc(ws) + '</div>';

  if (!data.is_git_repo) {
    html += '<div class="files-empty">Not a git repository. Initialize one with <code>git init</code> to see working-tree diffs here.</div>';
    filesContent.innerHTML = html;
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
      const ins = f.insertions || 0;
      const del = f.deletions || 0;
      const counts = (ins || del)
        ? ' <span class="file-counts"><span class="add">+' + ins + '</span> <span class="del">-' + del + '</span></span>'
        : '';
      html += '<div class="file-row">'
        + '<span class="file-type ' + esc(type) + '">' + esc(label) + '</span>'
        + '<span class="file-path">' + esc(f.path || '') + '</span>'
        + counts
        + '</div>';
    }
    html += '</div>';
  }

  const diff = data.diff || '';
  if (diff.trim().length > 0) {
    html += '<div class="stat-section-title">Unified Diff</div>';
    html += '<div class="diff-block"><pre>' + esc(diff) + '</pre></div>';
  } else if (files.length === 0) {
    html += '<div class="files-empty">Working tree clean — no modified files.</div>';
  }

  filesContent.innerHTML = html;
}

async function loadStatsTab() {
  if (!state.sessionId) {
    statsContent.innerHTML = '<div class="stats-empty">No active session.</div>';
    return;
  }
  statsContent.innerHTML = '<div class="stats-empty">Loading stats…</div>';
  try {
    const res = await fetch('/session/' + state.sessionId + '/stats');
    if (!res.ok) throw new Error(await res.text());
    const data = await res.json();
    renderStatsTab(data);
  } catch (e) {
    statsContent.innerHTML = '<div class="stats-empty">Failed to load stats: ' + esc(e.message) + '</div>';
  }
}

function renderStatsTab(data) {
  const cards = [];
  const card = (label, value, accent) =>
    '<div class="stat-card"><div class="stat-label">' + esc(label) + '</div>'
    + '<div class="stat-value' + (accent ? ' accent' : '') + '">' + esc(String(value)) + '</div></div>';

  cards.push(card('Title', data.title || '—'));
  cards.push(card('Provider', data.provider || '—'));
  cards.push(card('Model', data.model || '—', true));

  let created = '—';
  if (data.created_at) {
    try { created = new Date(data.created_at * 1000).toLocaleString(); } catch (e) {}
  }
  cards.push(card('Created', created));

  cards.push(card('Messages', data.message_count || 0));
  cards.push(card('User / Assistant', (data.user_messages || 0) + ' / ' + (data.assistant_messages || 0)));
  cards.push(card('Tool Calls', data.tool_calls || 0));
  cards.push(card('Total Tool Time', formatMs(data.total_tool_time_ms || 0)));

  cards.push(card('Prompt Tokens', data.prompt_tokens || 0));
  cards.push(card('Completion Tokens', data.completion_tokens || 0));
  cards.push(card('Total Tokens', data.total_tokens || 0, true));

  let html = '<div class="stat-grid">' + cards.join('') + '</div>';

  const ws = data.workspace || '';
  if (ws) {
    html += '<div class="stat-section-title">Workspace</div>';
    html += '<div class="stat-card"><div class="stat-label">Directory</div><div class="stat-value">' + esc(ws) + '</div></div>';
  }

  statsContent.innerHTML = html;
}

function formatMs(ms) {
  ms = Number(ms) || 0;
  if (ms < 1000) return ms.toFixed(0) + ' ms';
  const s = ms / 1000;
  if (s < 60) return s.toFixed(1) + ' s';
  const m = Math.floor(s / 60);
  return m + 'm ' + (s % 60).toFixed(0) + 's';
}

function renderMessages() {
  messagesEl.innerHTML = '';
  const session = state.openSessions.find(s => s.id === state.sessionId);
  if (!session) return;
  for (const msg of session.messages) {
    messagesEl.appendChild(renderMessage(msg));
  }
}

function renderMessage(msg) {
  const div = document.createElement('div'); div.className = 'message ' + msg.role;
  const header = document.createElement('div'); header.className = 'message-header';
  const icons = { user: '👤', assistant: '🤖', system: 'ℹ️', tool: '🔧' };
  header.innerHTML = '<span class="role-icon">' + (icons[msg.role] || '') + '</span> ' + capitalize(msg.role);
  div.appendChild(header);
  const content = document.createElement('div'); content.className = 'message-content';
  if (msg.toolEvents && msg.toolEvents.length > 0) {
    const tc = document.createElement('div'); tc.className = 'tool-events';
    for (const t of msg.toolEvents) tc.appendChild(renderToolBlock(t));
    content.appendChild(tc);
  }
  if (msg.reasoning) {
    const rc = document.createElement('div'); rc.className = 'reasoning-block';
    const rl = document.createElement('div'); rl.className = 'reasoning-label'; rl.innerHTML = '💭 Thinking';
    const rt = document.createElement('div'); rt.className = 'reasoning-text'; rt.innerHTML = renderMarkdown(msg.reasoning);
    rc.appendChild(rl); rc.appendChild(rt);
    content.appendChild(rc);
  }
  if (msg.usage) {
    const uc = document.createElement('div'); uc.className = 'usage-block';
    uc.textContent = msg.usage;
    content.appendChild(uc);
  }
  if (msg.content) {
    const textEl = document.createElement('div'); textEl.className = 'md-content';
    textEl.innerHTML = renderMarkdown(msg.content);
    
    const activeSession = state.openSessions.find(s => s.id === state.sessionId);
    const isGenerating = activeSession && activeSession.generating;
    const isLastMsg = activeSession && msg === activeSession.messages[activeSession.messages.length - 1];
    
    if (isGenerating && isLastMsg) textEl.className += ' streaming-cursor';
    content.appendChild(textEl);
  }
  div.appendChild(content); return div;
}

function renderToolBlock(tc) {
  const toolName = tc.tool_name || '';
  
  // Extract command
  let command = toolName;
  if (tc.arguments) {
    const args = typeof tc.arguments === 'string' ? JSON.parse(tc.arguments) : tc.arguments;
    if (toolName === 'bash' || toolName === 'shell' || toolName === 'run_command') {
      command = args.command || args.cmd || args.script || '';
    } else if (['read_file', 'view_file', 'write_file', 'edit_file'].includes(toolName)) {
      command = toolName + ' ' + (args.path || args.file || args.file_path || args.filename || '');
    } else if (['search', 'grep', 'ripgrep'].includes(toolName)) {
      command = toolName + ' "' + (args.query || args.pattern || '') + '"';
    } else if (toolName === 'task' || toolName === 'dispatch_agent') {
      command = 'task ' + (args.description || args.prompt || '');
    } else {
      command = toolName + ' ' + JSON.stringify(args);
    }
  }

  // Extract description
  let desc = '';
  if (tc.arguments) {
    const args = typeof tc.arguments === 'string' ? JSON.parse(tc.arguments) : tc.arguments;
    desc = args.description || args.desc || args.prompt || '';
  }

  // Extract workdir
  let workdir = '';
  if (tc.arguments && (toolName === 'bash' || toolName === 'shell' || toolName === 'run_command')) {
    const args = typeof tc.arguments === 'string' ? JSON.parse(tc.arguments) : tc.arguments;
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

  const icons = { bash: '$', task: '🤖', read_file: '📄', write_file: '✏️', view_file: '📄', edit_file: '✏️', search: '🔍', grep: '🔍', ripgrep: '🔍' };
  const icon = icons[toolName] || '⚙';
  const displayNames = { bash: 'Bash', task: 'Task', read_file: 'Read File', write_file: 'Write File', view_file: 'Read File', edit_file: 'Edit File', search: 'Search' };
  const displayName = displayNames[toolName] || capitalize(toolName);

  const block = document.createElement('div');
  block.className = 'tool-block ' + tc.status; // success, error, running
  
  const header = document.createElement('div');
  header.className = 'tool-header';
  
  const chevron = document.createElement('span');
  chevron.className = 'tool-chevron';
  chevron.textContent = '▸';
  
  const heading = document.createElement('span');
  heading.className = 'tool-heading';
  if (icon && icon !== '$') {
    heading.innerHTML = `<span class="tool-type-icon" style="margin-right: 6px; opacity: 0.8;">${icon}</span># ${desc || displayName}`;
  } else {
    heading.textContent = `# ${desc || displayName}`;
  }
  
  const timing = document.createElement('span');
  timing.className = 'tool-duration';
  timing.textContent = tc.duration_ms ? `${Math.round(tc.duration_ms)}ms` : '';
  
  const statusEl = document.createElement('span');
  statusEl.className = 'tool-status-icon';
  if (tc.status === 'running') statusEl.textContent = '⠋';
  else if (tc.status === 'error') statusEl.textContent = '✗';
  else statusEl.textContent = '✓';

  header.appendChild(chevron);
  header.appendChild(heading);
  header.appendChild(timing);
  header.appendChild(statusEl);

  const body = document.createElement('div');
  body.className = 'tool-body collapsed';

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

  let expanded = false;
  header.addEventListener('click', () => {
    expanded = !expanded;
    body.classList.toggle('collapsed', !expanded);
    chevron.textContent = expanded ? '▾' : '▸';
  });

  block.appendChild(header);
  block.appendChild(body);
  return block;
}

function addMessage(role, content) { const div = renderMessage({ role, content }); messagesEl.appendChild(div); scrollToBottom(); }

function renderMarkdown(text) {
  if (typeof marked === 'undefined') {
    return esc(text).replace(/\n/g, '<br>');
  }
  
  const renderer = new marked.Renderer();
  
  renderer.code = function(code, lang) {
    const displayLang = lang || 'code';
    const codeStr = typeof code === 'object' ? code.text : code;
    const cleanCode = codeStr.replace(/\n$/, '');
    return `<div class="code-block-container">
      <div class="code-block-header">
        <span class="code-block-lang">${displayLang}</span>
        <button class="copy-code-btn" onclick="navigator.clipboard.writeText(this.closest('.code-block-container').querySelector('code').innerText).then(() => { this.innerText = 'Copied!'; setTimeout(() => this.innerText = 'Copy', 2000); })">Copy</button>
      </div>
      <pre class="md-code"><code>${esc(cleanCode)}</code></pre>
    </div>`;
  };

  renderer.codespan = function(code) {
    const text = typeof code === 'object' ? code.text : code;
    return `<code class="md-inline-code">${esc(text)}</code>`;
  };

  marked.setOptions({
    renderer: renderer,
    gfm: true,
    breaks: true,
    headerIds: false,
    mangle: false
  });

  return marked.parse(text);
}

function setGenerating(on) { sendBtn.disabled = on; sendBtn.textContent = on ? 'Generating...' : 'Send'; promptInput.disabled = on; }
function scrollToBottom() { document.getElementById('chat-container').scrollTop = document.getElementById('chat-container').scrollHeight; }
function capitalize(s) { return s.charAt(0).toUpperCase() + s.slice(1); }
function esc(s) { const d = document.createElement('div'); d.textContent = s; return d.innerHTML; }
function showToast(msg) { const t = document.createElement('div'); t.className = 'toast'; t.textContent = msg; document.body.appendChild(t); setTimeout(() => t.remove(), 4000); }


// ═══════════════════════════════════════════════════════════════════
//  MULTI-SESSION TABS HELPERS
// ═══════════════════════════════════════════════════════════════════

async function createNewSession(title = '', workspace = '') {
  try {
    const res = await fetch('/sessions', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ provider: state.provider, model: state.model, workspace })
    });
    if (res.ok) {
      const data = await res.json();
      const newSession = {
        id: data.id,
        title: title || 'Session - ' + state.model,
        workspace: workspace || '',
        messages: [],
        generating: false,
        reader: null,
        provider: state.provider,
        model: state.model
      };
      
      if (title) {
        await fetch('/rename', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ session_id: data.id, title })
        });
      }

      state.openSessions.push(newSession);
      switchSession(data.id);
      showToast('New session created');
      if (state.terminalOpen && workspace) {
        await startTerminal(workspace);
      }
    }
  } catch (e) {
    showToast('Failed to create session: ' + e.message);
  }
}

async function cancelSession(id) {
  const session = state.openSessions.find(s => s.id === id);
  if (!session) return;

  if (session.reader) {
    try {
      await session.reader.cancel();
    } catch (e) {}
    session.reader = null;
  }

  try {
    await fetch('/session/cancel', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ session_id: id })
    });
  } catch (e) {}

  session.generating = false;
  renderSessionTabs();

  if (id === state.sessionId) {
    setGenerating(false);
    renderMessages();
  }
}

async function switchSession(id) {
  if (id === 'terminal') {
    if (window.location.hash !== '#/terminal') {
      window.location.hash = '/terminal';
    }
    switchTab('terminal');
    return;
  }

  if (state.layoutMode === 'tab') {
    switchTab('chat');
  }

  const session = state.openSessions.find(s => s.id === id);
  if (!session) return;

  state.sessionId = id;
  state.sessionTitle = session.title || '';
  state.sessionWorkspace = session.workspace || '';
  if (session.provider && session.model) {
    applyProviderModel(session.provider, session.model);
  }

  const expectedHash = `#/session/${id}`;
  if (window.location.hash !== expectedHash) {
    window.location.hash = `/session/${id}`;
  }

  setGenerating(session.generating);
  renderMessages();
  renderSessionTabs();
  updateStatusBar();
  closeMobileSidebar();

  // Refresh the auxiliary tabs for the newly active session.
  if (state.activeTab === 'files') loadFilesTab();
  else if (state.activeTab === 'stats') loadStatsTab();
}

async function deleteSessionPermanently(id) {
  try {
    const res = await fetch(`/session/${id}`, { method: 'DELETE' });
    if (!res.ok) {
      throw new Error(await res.text());
    }
    const index = state.openSessions.findIndex(s => s.id === id);
    if (index !== -1) {
      state.openSessions.splice(index, 1);
    }
    if (state.sessionId === id) {
      if (state.openSessions.length > 0) {
        const nextIndex = Math.min(index, state.openSessions.length - 1);
        switchSession(state.openSessions[nextIndex].id);
      } else {
        state.sessionId = null;
        await createNewSession();
      }
    } else {
      renderSessionTabs();
    }
    showToast("Session permanently deleted.");
  } catch (e) {
    showToast("Failed to delete session: " + e.message);
  }
}

async function closeSessionTab(id) {
  const index = state.openSessions.findIndex(s => s.id === id);
  if (index === -1) return;

  const session = state.openSessions[index];
  if (session.generating) {
    await cancelSession(session.id);
  }

  state.openSessions.splice(index, 1);

  if (state.sessionId === id) {
    if (state.openSessions.length > 0) {
      const nextIndex = Math.min(index, state.openSessions.length - 1);
      switchSession(state.openSessions[nextIndex].id);
    } else {
      state.sessionId = null;
      await createNewSession();
    }
  } else {
    renderSessionTabs();
  }
}

function renderSessionTabs() {
  if (!sessionTabsContainer) return;

  sessionTabsContainer.innerHTML = state.openSessions.map(session => {
    const isActive = session.id === state.sessionId;
    const activeClass = isActive && (state.layoutMode === 'split' || state.activeTab === 'chat') ? 'active' : '';
    const title = session.title || 'Session';
    const genIndicator = session.generating ? '<span class="session-gen-indicator">⏳</span> ' : '';
    const ws = session.workspace ? shortPath(session.workspace) : '';
    
    return `
      <div class="session-item-wrapper ${activeClass ? 'active' : ''}" data-id="${session.id}">
        <div class="session-item-main" data-id="${session.id}">
          <div class="session-item-title-row">
            <span class="session-icon">💬</span>
            <span class="session-title-text" title="${esc(title)}">${genIndicator}${esc(title)}</span>
          </div>
          ${ws ? `<div class="session-item-workspace" title="${esc(session.workspace)}">📁 ${esc(ws)}</div>` : ''}
        </div>
        <div class="session-item-actions">
          <button class="session-action-btn rename-session-btn" data-id="${session.id}" data-title="${esc(title)}" title="Rename">✏️</button>
          <button class="session-action-btn delete-session-btn" data-id="${session.id}" title="Delete permanently">🗑️</button>
          <button class="session-action-btn close-session-btn" data-id="${session.id}" title="Close sidebar tab">&times;</button>
        </div>
      </div>
    `;
  }).join('');

  sessionTabsContainer.querySelectorAll('.session-item-main').forEach(el => {
    el.addEventListener('click', () => {
      switchSession(el.dataset.id);
    });
  });

  sessionTabsContainer.querySelectorAll('.rename-session-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      showRenameModal(btn.dataset.id, btn.dataset.title);
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

  sessionTabsContainer.querySelectorAll('.close-session-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      closeSessionTab(btn.dataset.id);
    });
  });
}


async function sendResize(cols, rows) {
  if (!termId) return;
  try {
    await fetch('/terminal/' + termId + '/resize', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cols, rows })
    });
  } catch (e) {}
}

window.addEventListener('resize', () => {
  if (term && fitAddon && state.terminalOpen) {
    try {
      fitAddon.fit();
    } catch (e) {}
  }
});
