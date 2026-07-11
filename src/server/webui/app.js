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
  activeTab: 'chat', // 'chat' or 'terminal'
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
const statusSession = document.getElementById('status-session');
const statusWorkspace = document.getElementById('status-workspace');
const newSessionBtn = document.getElementById('new-session-btn');
const toggleTerminalBtn = document.getElementById('toggle-terminal-btn');
const terminalPanel = document.getElementById('terminal-panel');
const terminalContainer = document.getElementById('terminal-container');
const terminalCloseBtn = document.getElementById('terminal-close-btn');

// New DOM refs for tabs & layout toggle
const mainEl = document.getElementById('main');
const mainContentWrapperEl = document.getElementById('main-content-wrapper');
const tabTerminalBtn = document.getElementById('tab-terminal-btn');
const layoutToggleBtn = document.getElementById('layout-toggle-btn');
const sessionTabsContainer = document.getElementById('session-tabs-container');

// ── Modal state ──
let pickerCleanup = null;

// ── Terminal state ──
let term = null;       // xterm instance
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

// ── Init ──
async function init() {
  await loadProviders();
  setupEventListeners();
  updateStatusBar();
  // Auto-restore the last active session so a page reload keeps context.
  try {
    const res = await fetch('/session/last');
    if (res.ok) {
      const data = await res.json();
      if (data && data.id) {
        await loadSessionById(data.id);
        return;
      }
    }
  } catch (e) {}
  // If no last session, create a default session
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
  const pidx = state.providers.findIndex(p => p.id === providerId);
  if (pidx < 0) { state.provider = providerId; state.model = modelId; return false; }
  const p = state.providers[pidx];
  providerSelect.value = pidx;
  onProviderChange(pidx);
  const midx = p.models.findIndex(m => m.id === modelId);
  if (midx >= 0) modelSelect.value = modelId;
  state.provider = providerId;
  state.model = midx >= 0 ? modelId : (p.models.length > 0 ? p.models[0].id : modelId);
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
  newSessionBtn.addEventListener('click', showNewSessionModal);
  
  // Tab '+' button listener
  const newTabBtn = document.getElementById('new-tab-btn');
  if (newTabBtn) {
    newTabBtn.addEventListener('click', showNewSessionModal);
  }

  toggleTerminalBtn.addEventListener('click', toggleTerminal);
  terminalCloseBtn.addEventListener('click', closeTerminal);
  
  // Tab and layout event listeners
  tabTerminalBtn.addEventListener('click', () => switchTab('terminal'));
  layoutToggleBtn.addEventListener('click', toggleLayoutMode);

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

    // Create xterm
    term = new Terminal({ theme: { background: '#0d1117', foreground: '#c9d1d9', cursor: '#64ffda' } });
    term.open(terminalContainer);

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
  updateLayoutUI();
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
    mainContentWrapperEl.classList.add('split-view');
    mainEl.classList.remove('hidden');
    terminalPanel.classList.remove('hidden');
    
    tabTerminalBtn.classList.add('active');
    layoutToggleBtn.innerHTML = '<span class="layout-icon">🔲</span> Tab View';
  } else {
    mainContentWrapperEl.classList.remove('split-view');
    layoutToggleBtn.innerHTML = '<span class="layout-icon">🔲</span> Split View';
    
    if (state.activeTab === 'chat') {
      mainEl.classList.remove('hidden');
      terminalPanel.classList.add('hidden');
      tabTerminalBtn.classList.remove('active');
    } else {
      mainEl.classList.add('hidden');
      terminalPanel.classList.remove('hidden');
      tabTerminalBtn.classList.add('active');
    }
  }
  renderSessionTabs();
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

function showRenameModal() {
  if (!state.sessionId) {
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
      <input class="rename-input" id="rename-input" type="text" placeholder="Enter new title..." value="${esc(state.sessionTitle || '')}" autofocus />
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
        body: JSON.stringify({ session_id: state.sessionId, title })
      });
      if (res.ok) {
        state.sessionTitle = title;
        const session = state.openSessions.find(s => s.id === state.sessionId);
        if (session) {
          session.title = title;
        }
        updateStatusBar();
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

async function loadSessionById(id) {
  // Check if already open
  let session = state.openSessions.find(s => s.id === id);
  if (session) {
    switchSession(id);
    return;
  }

  // If not open, fetch info and messages
  session = {
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
        for (const m of msgs) {
          const role = m.role === 'assistant' ? 'assistant' : (m.role === 'user' ? 'user' : 'system');
          session.messages.push({ role, content: m.content });
        }
      }
    }
  } catch (e) {}

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
function handleCompactCommand() { addSystemMessage('Compact: not yet implemented.'); }

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
    
    const res = await fetch('/generate', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
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
      msg.content = evt.text;
      if (session.id === state.sessionId) {
        renderMessages();
        scrollToBottom();
      }
      break;
    case 'backend.reasoning.delta':
      if (!msg.reasoning) msg.reasoning = '';
      msg.reasoning = evt.text;
      break;
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
  const icons = { bash: '⚡', task: '🤖', read_file: '📄', write_file: '📝', view_file: '📄', edit_file: '✏️', search: '🔍', grep: '🔍', ripgrep: '🔍' };
  const icon = icons[tc.tool_name] || '🔧';
  const statusIcons = { running: '⏳', success: '✅', error: '❌' };
  const block = document.createElement('div'); block.className = 'tool-block';
  const header = document.createElement('div'); header.className = 'tool-header';
  header.innerHTML = `<span class="tool-chevron">↓</span><span class="tool-icon">${icon}</span><span class="tool-name">${capitalize(tc.tool_name)}</span>${tc.duration_ms ? '<span class="tool-duration">' + Math.round(tc.duration_ms) + 'ms</span>' : ''}<span class="tool-status ${tc.status}">${statusIcons[tc.status] || ''} ${tc.status}</span>`;
  const body = document.createElement('div'); body.className = 'tool-body collapsed';
  if (tc.arguments) { const a = typeof tc.arguments === 'string' ? tc.arguments : JSON.stringify(tc.arguments, null, 2); body.innerHTML += '<div class="input-label">Input:</div>' + esc(a); }
  if (tc.result) { const r = typeof tc.result === 'string' ? tc.result : JSON.stringify(tc.result, null, 2); body.innerHTML += '\n\n' + esc(r); }
  let expanded = false;
  header.addEventListener('click', () => { expanded = !expanded; body.classList.toggle('collapsed', !expanded); header.querySelector('.tool-chevron').textContent = expanded ? '↑' : '↓'; });
  block.appendChild(header); block.appendChild(body); return block;
}

function addMessage(role, content) { const div = renderMessage({ role, content }); messagesEl.appendChild(div); scrollToBottom(); }

function renderMarkdown(text) {
  let html = esc(text);
  // Fenced code blocks
  html = html.replace(/```(\w*)\n([\s\S]*?)```/g,
    (m, lang, code) => '<pre class="md-code"><code>' + code.replace(/\n$/, '') + '</code></pre>');
  // Headings
  html = html.replace(/^### (.*)$/gm, '<h3>$1</h3>')
             .replace(/^## (.*)$/gm, '<h2>$1</h2>')
             .replace(/^# (.*)$/gm, '<h1>$1</h1>');
  // Blockquote (esc converts '>' to '&gt;')
  html = html.replace(/^&gt; (.*)$/gm, '<blockquote>$1</blockquote>');
  // Horizontal rule
  html = html.replace(/^(?:\*\*\*|---|___)$/gm, '<hr>');
  // Unordered lists (group consecutive "- "/"*" items)
  html = html.replace(/^(?:-|\*)\s+.*(?:\n(?:-|\*)\s+.*)*/gm, (m) => {
    const items = m.split('\n').map(l => '<li>' + l.replace(/^[-*]\s+/, '') + '</li>').join('');
    return '<ul>' + items + '</ul>';
  });
  // Ordered lists (group consecutive "1. " items)
  html = html.replace(/^\d+\.\s+.*(?:\n\d+\.\s+.*)*/gm, (m) => {
    const items = m.split('\n').map(l => '<li>' + l.replace(/^\d+\.\s+/, '') + '</li>').join('');
    return '<ol>' + items + '</ol>';
  });
  // Inline code
  html = html.replace(/`([^`]+)`/g, '<code class="md-inline-code">$1</code>');
  // Bold / italic
  html = html.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
             .replace(/\*([^*]+)\*/g, '<em>$1</em>');
  // Links
  html = html.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>');
  // Newlines -> <br>
  html = html.replace(/\n/g, '<br>');
  return html;
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

  setGenerating(session.generating);
  renderMessages();
  renderSessionTabs();
  updateStatusBar();
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
    const activeClass = session.id === state.sessionId && state.activeTab === 'chat' ? 'active' : '';
    const title = session.title || 'Session';
    const genIndicator = session.generating ? '<span class="session-gen-indicator">⏳</span> ' : '';
    return `
      <div class="session-tab-wrapper ${activeClass ? 'active-wrapper' : ''}">
        <button class="session-tab ${activeClass}" data-id="${session.id}">
          💬 ${genIndicator}${esc(title)}
        </button>
        <span class="close-session-btn" data-id="${session.id}">&times;</span>
      </div>
    `;
  }).join('');

  sessionTabsContainer.querySelectorAll('.session-tab').forEach(btn => {
    btn.addEventListener('click', () => {
      switchSession(btn.dataset.id);
    });
  });

  sessionTabsContainer.querySelectorAll('.close-session-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      closeSessionTab(btn.dataset.id);
    });
  });
}
