// ── SVG Icons ──
const SVG_ICONS = {
  chat: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/></svg>`,
  terminal: `<svg class="ui-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/></svg>`,
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

function resizePromptInput() {
  if (!promptInput) return;
  promptInput.style.height = 'auto';
  promptInput.style.height = Math.min(promptInput.scrollHeight, 180) + 'px';
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
  terminalOpen: false,
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
  fsRawEntries: []
};

// ── DOM refs ──
const messagesEl = document.getElementById('messages');
const promptInput = document.getElementById('prompt-input');
const sendBtn = document.getElementById('send-btn');
const pauseBtn = document.getElementById('pause-btn');
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
const filesExplorer = document.getElementById('files-explorer');
const filesGit = document.getElementById('files-git');
const filesSubtabExplorer = document.getElementById('files-subtab-explorer');
const filesSubtabGit = document.getElementById('files-subtab-git');
const fsBreadcrumb = document.getElementById('fs-breadcrumb');
const fsListing = document.getElementById('fs-listing');
const fsFilterInput = document.getElementById('fs-filter-input');
const fsEditorPane = document.getElementById('fs-editor-pane');
const fsBackBtn = document.getElementById('fs-back-btn');
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
let termFontSize = window.innerWidth <= 600 ? 12 : 13;
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
  providerSelect.value = pidx;
  onProviderChange(pidx);
  modelSelect.value = resolved.model;
  state.provider = resolved.provider;
  state.model = resolved.model;
  return resolved.provider === providerId ||
    state.providers[pidx].name === providerId;
}

function setupEventListeners() {
  sendBtn.addEventListener('click', sendMessage);
  promptInput.addEventListener('keydown', handleInputKeydown);
  clearBtn.addEventListener('click', async () => {
    if (!state.sessionId) { showToast('No active session'); return; }
    if (!confirm('Clear all messages in this session? This cannot be undone.')) return;
    try {
      const res = await fetch('/session/' + state.sessionId + '/clear', {
        method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}'
      });
      if (!res.ok) { showToast('Failed: ' + await res.text()); return; }
    } catch (e) {
      showToast('Failed: ' + e.message);
      return;
    }
    const session = state.openSessions.find(s => s.id === state.sessionId);
    if (session) { session.messages = []; }
    renderMessages();
    showToast('Chat cleared');
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
    resizePromptInput();
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
  const termZoomOutBtn = document.getElementById('term-zoom-out-btn');
  const termZoomInBtn = document.getElementById('term-zoom-in-btn');
  if (termZoomOutBtn) {
    termZoomOutBtn.addEventListener('click', () => changeTerminalFontSize(-1));
  }
  if (termZoomInBtn) {
    termZoomInBtn.addEventListener('click', () => changeTerminalFontSize(1));
  }
  const virtualKeysContainer = document.getElementById('terminal-virtual-keys');
  if (virtualKeysContainer) {
    const keyMap = {
      'Esc': '',
      'Tab': '	',
      'CtrlC': '',
      'CtrlD': '',
      'Up': '[A',
      'Down': '[B',
      'Left': '[D',
      'Right': '[C'
    };
    virtualKeysContainer.addEventListener('click', (e) => {
      const btn = e.target.closest('.term-key-btn');
      if (!btn) return;
      const keyName = btn.dataset.key;
      const seq = keyMap[keyName];
      if (seq) {
        sendTerminalInput(seq);
        if (term) term.focus();
      }
    });
  }
  if (pauseBtn) {
    pauseBtn.addEventListener('click', () => pauseActiveGeneration());
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
  if (fsBackBtn) {
    fsBackBtn.addEventListener('click', () => setFsMobileView('browser'));
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
      const text = state.fsViewMode === 'editor' ? fsEditor.value : state.fsSavedContent;
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
      if (state.fsOpenPath) updateMarkdownViewer(state.fsSavedContent);
    });
  }
  if (fsMdCodeBtn) {
    fsMdCodeBtn.addEventListener('click', () => {
      state.fsMdMode = 'code';
      if (fsMdCodeBtn) fsMdCodeBtn.classList.add('active');
      if (fsMdPreviewBtn) fsMdPreviewBtn.classList.remove('active');
      if (state.fsOpenPath) updateMarkdownViewer(state.fsSavedContent);
    });
  }
  if (fsWrapBtn) {
    fsWrapBtn.addEventListener('click', () => {
      state.fsLineWrap = !state.fsLineWrap;
      fsWrapBtn.classList.toggle('active', state.fsLineWrap);
      if (fsCodeWrapper) fsCodeWrapper.classList.toggle('wrap-lines', state.fsLineWrap);
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
      scheduleFsHighlight();
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

  // Escape key pauses/cancels the active generation from anywhere
  // (document level catches input textarea, FS editor, and unfocused UI).
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && e.type === 'keydown') {
      if (isModalOpen()) return;
      pauseActiveGeneration();
    }
  });

  window.addEventListener('hashchange', handleHashChange);
  window.addEventListener('online', updateConnectionStatus);
  window.addEventListener('offline', updateConnectionStatus);
  updateConnectionStatus();
}

// ── Status Bar ──
function updateStatusBar() {
  statusSession.textContent = state.sessionTitle || (state.sessionId ? 'Session: ' + state.sessionId.substring(0,8) + '…' : 'No session');
  statusWorkspace.textContent = state.sessionWorkspace ? SVG_ICONS.folder + ' ' + state.sessionWorkspace : '';
}

function updateConnectionStatus() {
  const status = document.getElementById('connection-status');
  if (!status) return;
  const online = navigator.onLine;
  status.classList.toggle('offline', !online);
  status.querySelector('.connection-label').textContent = online ? 'Connected' : 'Offline';
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

function changeTerminalFontSize(delta) {
  if (!term) return;
  termFontSize = Math.min(24, Math.max(9, termFontSize + delta));
  term.options.fontSize = termFontSize;
  if (fitAddon) {
    try {
      fitAddon.fit();
      sendResize(term.cols, term.rows);
    } catch (e) {}
  }
}

function sendTerminalInput(data) {
  if (!termId) return;
  fetch('/terminal/' + termId + '/input', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ data })
  }).catch(() => {});
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

    // Create xterm with optimized responsive font size & line height
    termFontSize = window.innerWidth <= 600 ? 12 : 13;
    term = new Terminal({
      fontFamily: '"Symbols Nerd Font", "SymbolsNerdFontMono", "FiraCode Nerd Font", "Fira Code", "DejaVu Sans Mono", "Cascadia Code", "Segoe UI Emoji", "Apple Color Emoji", "Noto Color Emoji", monospace',
      fontSize: termFontSize,
      lineHeight: 1.25,
      cursorBlink: true,
      allowProposedApi: true,
      customGlyphs: true,
      rescaleOverlappingGlyphs: true,
      theme: { background: '#0d1117', foreground: '#c9d1d9', cursor: '#64ffda' }
    });
    fitAddon = new FitAddon.FitAddon();
    term.loadAddon(fitAddon);
    term.open(terminalContainer);

    // xterm swallows key events, so intercept Escape here too and route
    // it through the same global pause path used by the rest of the UI.
    term.attachCustomKeyEventHandler((e) => {
      if (e.key === 'Escape' && e.type === 'keydown') {
        pauseActiveGeneration();
      }
      return true;
    });
    
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
      sendTerminalInput(data);
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
    if (layoutToggleBtn) layoutToggleBtn.innerHTML = '<span class="layout-icon">' + SVG_ICONS.tab + '</span> Tab View';
  } else {
    if (mainContentWrapperEl) mainContentWrapperEl.classList.remove('split-view');
    if (layoutToggleBtn) layoutToggleBtn.innerHTML = '<span class="layout-icon">' + SVG_ICONS.split + '</span> Split View';

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

async function runGeneration(session, text) {
  session.cancelRequested = false;
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

  let receivedComplete = false;
  try {
    // Prefer the live dropdown selection, then the session's stored pair.
    // Always resolve against /providers so stale/empty DB values cannot
    // produce "Unknown provider:".
    const resolved = resolveProviderModel(
      state.provider || session.provider,
      state.model || session.model
    );
    if (!resolved.ok) {
      throw new Error('No provider/model configured. Check the sidebar selectors.');
    }
    session.provider = resolved.provider;
    session.model = resolved.model;
    state.provider = resolved.provider;
    state.model = resolved.model;

    const body = JSON.stringify({
      text,
      provider: resolved.provider,
      model: resolved.model,
      reasoning_mode: state.reasoning,
      session_id: session.id
    });
    
    const res = await fetch(`/session/${session.id}/generate`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
    if (!res.ok) throw new Error(await res.text());
    
    if (!res.body) throw new Error('The server returned an empty response stream');
    const reader = res.body.getReader();
    session.reader = reader;
    const decoder = new TextDecoder();
    let buffer = '';

    const consumeLine = (line) => {
      if (!line.trim()) return;
      try {
        const event = JSON.parse(line);
        handleEvent(event, assistantMsg, session);
        if (event.type === 'generation.complete') receivedComplete = true;
      } catch (error) {
        console.warn('Ignoring malformed stream event:', error, line);
      }
    };
    
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      const lines = buffer.split('\n');
      buffer = lines.pop() || '';
      for (const line of lines) consumeLine(line);
    }
    buffer += decoder.decode();
    consumeLine(buffer);

    if (!receivedComplete && !session.cancelRequested) {
      throw new Error('The response stream ended before generation completed');
    }
  } catch (e) {
    // Some browsers report a transport error after receiving the final chunk.
    // The generation.complete event is authoritative in that case.
    if (e.name !== 'AbortError' && !session.cancelRequested && !receivedComplete) {
      const message = e.message || 'Connection interrupted';
      showToast('Generation interrupted: ' + message);
      assistantMsg.streamError = message;
    }
  } finally {
    session.reader = null;

    const exists = state.openSessions.some(s => s.id === session.id);
    const hasNext = exists && !session.cancelRequested && session.promptQueue && session.promptQueue.length > 0;

    if (hasNext) {
      session.generating = true;
      const nextPrompt = session.promptQueue.shift();
      renderSessionTabs();
      updateQueueIndicator();
      setTimeout(() => {
        if (session.cancelRequested) {
          session.generating = false;
          renderSessionTabs();
          if (session.id === state.sessionId) {
            setGenerating(false);
          }
          return;
        }
        runGeneration(session, nextPrompt);
      }, 50);
    } else {
      session.generating = false;
      renderSessionTabs();
      if (session.id === state.sessionId) {
        setGenerating(false);
        renderMessages();
        scrollToBottom();
      }
      updateQueueIndicator();
    }
  }
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

  if (session.generating) {
    if (!session.promptQueue) {
      session.promptQueue = [];
    }
    session.promptQueue.push(text);
    promptInput.value = '';
    resizePromptInput();
    showToast('Prompt queued');
    updateQueueIndicator();
    return;
  }

  promptInput.value = '';
  resizePromptInput();
  await runGeneration(session, text);
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
      msg.streamError = evt.message || 'The backend reported an error';
      if (session.id === state.sessionId) renderMessages();
      break;
    case 'generation.complete':
      if (evt.error) {
        msg.streamError = evt.error;
        showToast('Generation failed: ' + evt.error);
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
    filesGit.innerHTML = '<div class="files-empty">No active session.</div>';
    return;
  }
  filesGit.innerHTML = '<div class="files-empty">Loading working tree…</div>';
  try {
    const res = await fetch('/session/' + state.sessionId + '/files');
    if (!res.ok) throw new Error(await res.text());
    const data = await res.json();
    renderGitChanges(data);
  } catch (e) {
    filesGit.innerHTML = '<div class="files-empty">Failed to load files: ' + esc(e.message) + '</div>';
  }
}

function renderGitChanges(data) {
  if (!filesGit) return;
  const ws = data.workspace || '';
  let html = '';
  html += '<div class="files-workspace">📁 ' + esc(ws) + '</div>';

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
    html += '<div class="stat-section-title">Unified Diff</div>';
    html += '<div class="diff-block"><pre>' + esc(diff) + '</pre></div>';
  } else if (files.length === 0) {
    html += '<div class="files-empty">Working tree clean — no modified files.</div>';
  }

  filesGit.innerHTML = html;
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

function getFsFileIcon(path, isDir) {
  if (isDir) return '📁';
  const name = (path || '').split('/').pop() || '';
  const lower = name.toLowerCase();
  if (lower === 'cmakelists.txt' || lower.endsWith('.cmake')) return '⚙️';
  if (lower === 'dockerfile' || lower.startsWith('dockerfile.')) return '🐳';
  if (lower === 'makefile' || lower === 'gnumakefile') return '🛠️';
  if (lower.startsWith('.git')) return '🌱';

  const dot = lower.lastIndexOf('.');
  const ext = dot >= 0 ? lower.slice(dot + 1) : '';

  switch (ext) {
    case 'js': case 'mjs': case 'cjs': case 'jsx': return '⚡';
    case 'ts': case 'tsx': case 'mts': case 'cts': return '📘';
    case 'py': case 'pyw': return '🐍';
    case 'cpp': case 'cxx': case 'cc': case 'c': case 'h': case 'hpp': case 'hh': case 'hxx': return '⚙️';
    case 'html': case 'htm': case 'xml': case 'svg': return '🌐';
    case 'css': case 'scss': case 'less': return '🎨';
    case 'json': case 'jsonc': case 'yaml': case 'yml': case 'toml': case 'ini': return '📋';
    case 'md': case 'markdown': return '📝';
    case 'sh': case 'bash': case 'zsh': case 'fish': return '💻';
    case 'png': case 'jpg': case 'jpeg': case 'gif': case 'webp': case 'ico': case 'bmp': return '🖼️';
    case 'rs': case 'go': case 'java': case 'kt': case 'swift': return '📦';
    case 'pdf': return '📕';
    default: return '📄';
  }
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

function formatBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
  return (n / (1024 * 1024)).toFixed(1) + ' MB';
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
    fsListing.innerHTML = '<div class="files-empty">No active session.</div>';
    if (fsBreadcrumb) fsBreadcrumb.innerHTML = '';
    return;
  }
  state.fsDir = relPath || '';
  renderFsBreadcrumb(state.fsDir);
  fsListing.innerHTML = '<div class="files-empty">Loading…</div>';
  try {
    const q = state.fsDir ? ('?path=' + encodeURIComponent(state.fsDir)) : '';
    const res = await fetch('/session/' + state.sessionId + '/fs/list' + q);
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(data.error || res.statusText || 'list failed');
    state.fsRawEntries = Array.isArray(data.entries) ? data.entries : [];
    renderFsListingEntries();
  } catch (e) {
    fsListing.innerHTML = '<div class="files-empty">Failed to list directory: ' + esc(e.message) + '</div>';
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
      + '<span class="fs-icon">⬆</span>'
      + '<span class="fs-name">..</span>'
      + '</button>';
  }

  if (entries.length === 0) {
    html += '<div class="files-empty">' + (filter ? 'No matching files.' : 'Empty directory.') + '</div>';
  } else {
    for (const e of entries) {
      const isDir = e.type === 'dir';
      const icon = getFsFileIcon(e.path, isDir);
      const size = (!isDir && e.size != null) ? '<span class="fs-size">' + formatBytes(e.size) + '</span>' : '';
      const active = (!isDir && state.fsOpenPath === e.path) ? ' active' : '';
      html += '<button type="button" class="fs-entry' + active + '" data-type="' + (isDir ? 'dir' : 'file')
        + '" data-path="' + esc(e.path || '') + '">'
        + '<span class="fs-icon">' + icon + '</span>'
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
      else openFsFile(path);
    });
  });
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

  if (fsCodeWrapper) {
    fsCodeWrapper.classList.toggle('wrap-lines', !!state.fsLineWrap);
  }
}

function updateMarkdownViewer(content) {
  if (state.fsMdMode === 'preview') {
    setFsViewMode('markdown');
    if (fsViewMarkdown) {
      if (typeof marked !== 'undefined' && marked.parse) {
        try {
          fsViewMarkdown.innerHTML = marked.parse(content);
        } catch (e) {
          fsViewMarkdown.textContent = content;
        }
      } else {
        fsViewMarkdown.textContent = content;
      }
    }
  } else {
    updateCodeViewer(content, 'markdown');
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
    const isMd = state.fsOpenPath.toLowerCase().endsWith('.md');
    if (isMd) updateMarkdownViewer(state.fsSavedContent);
    else updateCodeViewer(state.fsSavedContent, fsLang);
    if (fsEditBtn) fsEditBtn.textContent = 'Edit';
  } else {
    setFsViewMode('editor');
    setFsEditorContent(state.fsSavedContent, state.fsOpenPath);
    if (fsEditBtn) fsEditBtn.textContent = 'Read';
  }
  setFsDirty(state.fsDirty);
}

async function confirmDiscardIfDirty() {
  if (!state.fsDirty) return true;
  return window.confirm('You have unsaved changes. Discard them?');
}

// Map file path → highlight.js language id (empty = plain / auto).
function detectFsLanguage(path) {
  const name = (path || '').split('/').pop() || '';
  const lower = name.toLowerCase();
  if (lower === 'cmakelists.txt' || lower.endsWith('.cmake')) return 'cmake';
  if (lower === 'dockerfile' || lower.startsWith('dockerfile.')) return 'dockerfile';
  if (lower === 'makefile' || lower === 'gnumakefile') return 'makefile';
  const dot = lower.lastIndexOf('.');
  const ext = dot >= 0 ? lower.slice(dot + 1) : '';
  const map = {
    js: 'javascript', mjs: 'javascript', cjs: 'javascript', jsx: 'javascript',
    ts: 'typescript', tsx: 'typescript', mts: 'typescript', cts: 'typescript',
    py: 'python', pyw: 'python',
    c: 'c', h: 'c',
    cc: 'cpp', cpp: 'cpp', cxx: 'cpp', hpp: 'cpp', hh: 'cpp', hxx: 'cpp',
    rs: 'rust', go: 'go', java: 'java', kt: 'kotlin', kts: 'kotlin',
    rb: 'ruby', php: 'php', swift: 'swift',
    cs: 'csharp', fs: 'fsharp',
    sh: 'bash', bash: 'bash', zsh: 'bash', fish: 'bash',
    json: 'json', jsonc: 'json',
    yml: 'yaml', yaml: 'yaml',
    toml: 'ini', ini: 'ini', conf: 'ini', cfg: 'ini',
    md: 'markdown', markdown: 'markdown',
    html: 'xml', htm: 'xml', xhtml: 'xml', svg: 'xml', xml: 'xml',
    css: 'css', scss: 'scss', less: 'less',
    sql: 'sql', graphql: 'graphql', gql: 'graphql',
    diff: 'diff', patch: 'diff',
    r: 'r', lua: 'lua', pl: 'perl', pm: 'perl',
    vim: 'vim', proto: 'protobuf',
    txt: 'plaintext', log: 'plaintext',
  };
  return map[ext] || '';
}

function syncFsHighlightScroll() {
  if (!fsEditor || !fsHighlight) return;
  fsHighlight.scrollTop = fsEditor.scrollTop;
  fsHighlight.scrollLeft = fsEditor.scrollLeft;
}

function scheduleFsHighlight() {
  if (fsHighlightTimer) clearTimeout(fsHighlightTimer);
  fsHighlightTimer = setTimeout(() => {
    fsHighlightTimer = null;
    updateFsHighlight();
  }, 40);
}

function getHljs() {
  if (typeof window !== 'undefined' && window.hljs) return window.hljs;
  if (typeof hljs !== 'undefined') return hljs;
  return null;
}

function applyFsPlainOverlay(source) {
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
    fsEditor.classList.add('fs-editor-plain');
    fsEditor.classList.remove('fs-editor-highlighting');
    fsHighlightCode.textContent = '';
    fsHighlightCode.className = 'hljs';
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

async function openFsFile(relPath) {
  if (!relPath || !state.sessionId) return;
  if (state.fsOpenPath === relPath && !state.fsDirty) {
    setFsMobileView('viewer');
    return;
  }
  if (!(await confirmDiscardIfDirty())) return;

  const name = relPath.split('/').pop() || '';
  const lower = name.toLowerCase();
  const ext = lower.lastIndexOf('.') >= 0 ? lower.slice(lower.lastIndexOf('.') + 1) : '';
  const isImage = ['png', 'jpg', 'jpeg', 'gif', 'webp', 'ico', 'bmp', 'svg'].includes(ext);
  const isMd = ext === 'md' || ext === 'markdown';

  state.fsOpenPath = relPath;
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

  if (fsMdToggle) fsMdToggle.classList.toggle('hidden', !isMd);

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

  if (fsEditorStatus) fsEditorStatus.textContent = 'Loading…';
  try {
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
    setFsEditorContent(state.fsSavedContent, state.fsOpenPath);
    setFsDirty(false);

    const lineCount = (state.fsSavedContent.split('\n')).length;
    const szStr = data.size != null ? formatBytes(data.size) : formatBytes(state.fsSavedContent.length);
    if (fsFileBadge) fsFileBadge.textContent = szStr + ' · ' + lineCount + ' lines · ' + (fsLang || 'text');

    if (isMd) {
      updateMarkdownViewer(state.fsSavedContent);
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
  if (session.messages.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'empty-chat';
    empty.innerHTML = '<div class="empty-chat-mark">Q</div>'
      + '<h2>What are we building?</h2>'
      + '<p>Ask QCode to explore the workspace, write code, or run a command.</p>'
      + '<div class="empty-chat-hint"><kbd>/</kbd> Browse commands <span>•</span> <kbd>Enter</kbd> Send</div>';
    messagesEl.appendChild(empty);
    return;
  }
  for (const msg of session.messages) {
    messagesEl.appendChild(renderMessage(msg));
  }
}

function renderMessage(msg) {
  const div = document.createElement('div'); div.className = 'message ' + msg.role;
  const header = document.createElement('div'); header.className = 'message-header';
  const icons = { user: SVG_ICONS.user, assistant: SVG_ICONS.assistant, system: SVG_ICONS.system, tool: SVG_ICONS.tool };
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
    const rl = document.createElement('div'); rl.className = 'reasoning-label'; rl.innerHTML = SVG_ICONS.reasoning + ' Thinking';
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
  if (msg.streamError) {
    const error = document.createElement('div');
    error.className = 'stream-error';
    error.innerHTML = '<span>!</span><div><strong>Response interrupted</strong><br>'
      + esc(msg.streamError) + '</div>';
    content.appendChild(error);
  }
  div.appendChild(content); return div;
}

function parseToolValue(value) {
  if (typeof value !== 'string') return value || {};
  try {
    return JSON.parse(value);
  } catch (error) {
    return { raw: value };
  }
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
  if (typeof marked === 'undefined' || !marked.Renderer) {
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

function setGenerating(on) {
  sendBtn.disabled = false;
  promptInput.disabled = false;
  sendBtn.innerHTML = on ? '<span>Queue</span><span class="send-icon">＋</span>' : '<span>Send</span><span class="send-icon">↑</span>';
  document.body.classList.toggle('is-generating', Boolean(on));
  sendBtn.setAttribute('aria-label', on ? 'Queue prompt' : 'Send message');
  if (pauseBtn) pauseBtn.classList.toggle('hidden', !on);
}
function scrollToBottom() { document.getElementById('chat-container').scrollTop = document.getElementById('chat-container').scrollHeight; }
function capitalize(s) { return s.charAt(0).toUpperCase() + s.slice(1); }
function esc(s) { const d = document.createElement('div'); d.textContent = s; return d.innerHTML; }
function showToast(msg) { const t = document.createElement('div'); t.className = 'toast'; t.textContent = msg; document.body.appendChild(t); setTimeout(() => t.remove(), 4000); }


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
      body: JSON.stringify({ provider: resolved.provider, model: resolved.model, workspace, custom_id: title })
    });
    if (res.ok) {
      const data = await res.json();
      const newSession = {
        id: data.id,
        title: data.title || title || 'Session - ' + resolved.model,
        workspace: workspace || '',
        messages: [],
        generating: false,
        reader: null,
        provider: resolved.provider,
        model: resolved.model
      };

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

async function cancelSession(id) {
  const session = state.openSessions.find(s => s.id === id);
  if (!session) return;
  session.cancelRequested = true;

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
  session.promptQueue = [];
  renderSessionTabs();

  if (id === state.sessionId) {
    setGenerating(false);
    renderMessages();
    updateQueueIndicator();
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
  // Reset file browser/editor when switching sessions (discard unsaved).
  state.fsDir = '';
  state.fsOpenPath = null;
  state.fsSavedContent = '';
  state.fsDirty = false;
  clearFsEditor();
  if (fsEditorPath) fsEditorPath.textContent = 'No file open';
  if (fsCloseBtn) fsCloseBtn.disabled = true;
  if (fsSaveBtn) fsSaveBtn.disabled = true;
  if (fsDirtyBadge) fsDirtyBadge.classList.add('hidden');
  if (fsEditorStatus) fsEditorStatus.textContent = '';
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

  setGenerating(session.generating);
  renderMessages();
  scrollToBottom();
  renderSessionTabs();
  updateStatusBar();
  closeMobileSidebar();
  updateQueueIndicator();

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
            <span class="session-icon">${SVG_ICONS.chat}</span>
            <span class="session-title-text" title="${esc(title)}">${genIndicator}${esc(title)}</span>
          </div>
          ${ws ? `<div class="session-item-workspace" title="${esc(session.workspace)}">📁 ${esc(ws)}</div>` : ''}
        </div>
        <div class="session-item-actions">
          <button class="session-action-btn rename-session-btn" data-id="${session.id}" data-title="${esc(title)}" title="Rename">${SVG_ICONS.rename}</button>
          <button class="session-action-btn delete-session-btn" data-id="${session.id}" title="Delete permanently">${SVG_ICONS.delete}</button>
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
