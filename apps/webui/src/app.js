import { fuzzyScore, fuzzyFilter, shortPath, formatBytes, formatMs, formatNumber, detectFsLanguage, parseToolValue, sessionIdFromTaskResult, extractChildSessionId, esc, capitalize, relTime, extractFrontmatter, stripFrontmatter, transformWikilinks, SLASH_COMMANDS, PALETTE_COMMANDS } from './utils.js';
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
  activeTab: 'chat', // 'chat' | 'terminal' | 'files' | 'stats' | 'sessions'
  parentSessionId: null,
  agentMode: 'orchestrator',
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
const clearBtn = document.getElementById('clear-btn');
const providerSelect = document.getElementById('provider-select');
const modelSelect = document.getElementById('model-select');
const reasoningSelect = document.getElementById('reasoning-select');
const modalOverlay = document.getElementById('modal-overlay');
const statusSession = document.getElementById('header-session-title');
const statusWorkspace = document.getElementById('header-session-workspace');
const newSessionBtn = document.getElementById('new-session-btn');
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
const statsCopyBtn = document.getElementById('stats-copy-btn');
const tabFilesBtn = document.getElementById('tab-files-btn');
const tabStatsBtn = document.getElementById('tab-stats-btn');
const tabSessionsBtn = document.getElementById('tab-sessions-btn');
const sessionsPanel = document.getElementById('sessions-panel');
const sessionsContent = document.getElementById('sessions-content');
const sessionsRefreshBtn = document.getElementById('sessions-refresh-btn');
const parentBackBtn = document.getElementById('parent-back-btn');
const subagentBadge = document.getElementById('subagent-badge');
const agentModeBtn = document.getElementById('agent-mode-btn');

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
let termPollFails = 0; // T6.1 consecutive stream failures
let termPollTimer = null;

// ── Slash commands ──




const THEMES = {
  "qcode": {
    "title": "QCode Web",
    "bg": "#080B12",
    "surface": "#0E131E",
    "raised": "#141B28",
    "text": "#E7EDF7",
    "muted": "#8995A8",
    "accent": "#6EE7D8",
    "accent2": "#2DD4BF",
    "danger": "#FB7185",
    "warning": "#FBBF24",
    "success": "#3ECF8E"
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
    const savedTheme = localStorage.getItem('qcode-theme');
    if (savedTheme && THEMES[savedTheme]) applyTheme(savedTheme);
  } catch (_) {}
  restorePrefs();
  if (state.theme && THEMES[state.theme]) applyTheme(state.theme);
  if (state.reasoning && reasoningSelect) reasoningSelect.value = state.reasoning;
  if (state.provider && providerSelect) providerSelect.value = state.provider;
  try {
    const verRes = await fetch("/api/version");
    if (verRes.ok) {
      const verData = await verRes.json();
      const sub = document.getElementById("brand-sub");
      if (sub && verData.version) {
        sub.textContent = "v" + verData.version;
        sub.title = (verData.name || "qcode-server") + " v" + verData.version;
      }
    }
  } catch (_) {}

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
    persistPrefs();
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
    persistDraft();
  });
    const fsFilter = document.getElementById('fs-filter-input');
  if (fsFilter) fsFilter.addEventListener('input', () => { state.fsFilterText = fsFilter.value; renderFsListingEntries(); });
  const sessFilter = document.getElementById('sessions-filter-input');
  if (sessFilter) sessFilter.addEventListener('input', () => { state.sessionsFilterText = sessFilter.value; renderSessionTabs(); });
  const chatC = document.getElementById('chat-container');
  if (chatC) chatC.addEventListener('scroll', () => updateJumpPill(), { passive: true });
  document.addEventListener('click', (e) => {
    if (slashMenuEl && !slashMenuEl.contains(e.target) && e.target !== promptInput) {
      closeSlashMenu();
    }
  });
  document.addEventListener('click', onMarkdownLinkClick);
  if (newSessionBtn) {
    newSessionBtn.addEventListener('click', showNewSessionModal);
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
  if (tabSessionsBtn) {
    tabSessionsBtn.addEventListener('click', () => switchTab('sessions'));
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
  if (agentModeBtn) {
    agentModeBtn.addEventListener('click', () => toggleAgentMode());
  }
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
    const key = e.key;
    const mod = e.ctrlKey || e.metaKey;
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
}

// ── Status Bar ──
function updateStatusBar() {
  statusSession.textContent = state.sessionTitle || (state.sessionId ? 'Session: ' + state.sessionId.substring(0,8) + '…' : 'No session');
  statusWorkspace.textContent = state.sessionWorkspace ? SVG_ICONS.folder + ' ' + state.sessionWorkspace : '';
  const isSub = state.agentMode === 'subagent';
  if (subagentBadge) subagentBadge.classList.toggle('hidden', !isSub);
  if (parentBackBtn) {
    parentBackBtn.classList.toggle('hidden', !isSub && !state.parentSessionId);
    parentBackBtn.title = 'Return to parent session (Esc or b)';
  }
  if (agentModeBtn) {
    agentModeBtn.classList.toggle('hidden', isSub);
    if (!isSub) {
      const label = state.agentMode === 'plan' ? 'Plan' : 'Orchestrator';
      agentModeBtn.textContent = label;
      agentModeBtn.classList.toggle('plan', state.agentMode === 'plan');
      agentModeBtn.classList.remove('subagent');
      agentModeBtn.disabled = !state.sessionId;
    }
  }
}

async function updateConnectionStatus() {
  const status = document.getElementById('connection-status');
  if (!status) return;
  let online = navigator.onLine;
  if (online) {
    try {
      const res = await fetch('/api/health');
      online = res.ok;
    } catch (_) {
      online = false;
    }
  }
  status.classList.toggle('offline', !online);
  const label = status.querySelector('.connection-label');
  if (label) label.textContent = online ? 'Connected' : 'Offline';
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

    // Poll for output (T6.1: auto-reconnect after repeated failures)
    termPollFails = 0;
    termPollTimer = setInterval(async () => {
      if (!termId) return;
      try {
        const res = await fetch('/terminal/' + termId + '/stream');
        if (res.ok) {
          termPollFails = 0;
          const text = await res.text();
          if (text) term.write(text);
        } else {
          termPollFails = (termPollFails || 0) + 1;
        }
      } catch (e) { termPollFails = (termPollFails || 0) + 1; }
      if ((termPollFails || 0) >= 12) {
        termPollFails = 0;
        if (term) term.write('\r\n[reconnecting…]\r\n');
        try { await startTerminal(state.sessionWorkspace || ''); }
        catch (_) { showToast('Terminal reconnect failed'); }
      }
    }, 80);

  } catch (e) {
    showToast('Terminal error: ' + e.message);
    terminalContainer.innerHTML = '<div class="error-panel">Terminal failed: ' + esc(e.message)
      + '<br><button type="button" class="msg-action-btn" data-retry="term">Retry</button></div>';
    const tb = terminalContainer.querySelector('[data-retry="term"]');
    if (tb) tb.addEventListener('click', () => startTerminal(state.sessionWorkspace || ''));
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
  state.activeTab = tab;
  persistPrefs();
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
  if (tab === 'sessions') loadDelegatedSessionsTab();
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
    if (sessionsPanel) sessionsPanel.classList.add('hidden');

    [tabChatBtn, tabTerminalBtn, tabFilesBtn, tabStatsBtn, tabSessionsBtn].forEach(b => {
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
    if (sessionsPanel) sessionsPanel.classList.add('hidden');
    [tabChatBtn, tabTerminalBtn, tabFilesBtn, tabStatsBtn, tabSessionsBtn].forEach(b => {
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
    } else if (state.activeTab === 'sessions') {
      if (sessionsPanel) sessionsPanel.classList.remove('hidden');
      if (tabSessionsBtn) tabSessionsBtn.classList.add('active');
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
    case 'variant': handleVariantCommand(args); break;
    case 'tools': handleToolsCommand(args); break;
    case 'rename': handleRenameCommand(args); break;
    case 'session': case 'load': handleSessionCommand(args); break;
    case 'compact': handleCompactCommand(); break;
    case 'theme': case 'themes': handleThemeCommand(args); break;
    case 'agent': handleAgentCommand(args); break;
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
        <div class="help-row"><kbd>Ctrl</kbd>+<kbd>N</kbd><span>New session</span></div>
        <div class="help-row"><kbd>/new</kbd><span>Start a new session</span></div>
        <div class="help-row"><kbd>/session</kbd><span>Switch session</span></div>
        <div class="help-row"><kbd>/rename</kbd><span>Rename current session</span></div>
        <div class="help-row"><kbd>/compact</kbd><span>Summarize context</span></div>
        <div class="help-section">Model</div>
        <div class="help-row"><kbd>/model</kbd><span>Switch model</span></div>
        <div class="help-row"><kbd>/variant</kbd><span>Reasoning effort</span></div>
        <div class="help-row"><kbd>/agent</kbd><span>Orchestrator / Plan</span></div>
        <div class="help-section">View</div>
        <div class="help-row"><kbd>Esc</kbd><span>Stop generation</span></div>
        <div class="help-row"><kbd>/retry</kbd><span>Retry last prompt</span></div>
        <div class="help-row"><kbd>F2</kbd><span>Toggle thinking</span></div>
        <div class="help-row"><kbd>Alt</kbd>+<kbd>1</kbd>…<kbd>4</kbd><span>Chat / Files / Stats / Sessions</span></div>
        <div class="help-row"><kbd>/theme</kbd><span>Switch theme</span></div>
        <div class="help-row"><kbd>/queue</kbd><span>List or drop queued prompts</span></div>
        <div class="help-row"><kbd>/clear-queue</kbd><span>Clear all queued prompts</span></div>
        <div class="help-row"><kbd>/tools</kbd><span>Toggle tool use on|off</span></div>
      </div>
      <div class="picker-footer">esc/enter close</div>
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

function applyTheme(name) {
  const t = THEMES[name];
  if (!t) return false;
  state.theme = name;
  const root = document.documentElement;
  root.dataset.theme = name;
  const set = (k, v) => root.style.setProperty(k, v);
  set('--bg', t.bg);
  set('--surface', t.surface);
  set('--surface-raised', t.raised);
  set('--surface-soft', t.surface);
  set('--text', t.text);
  set('--muted', t.muted);
  set('--faint', t.muted);
  set('--accent', t.accent);
  set('--accent-strong', t.accent2);
  set('--blue', t.accent2);
  set('--danger', t.danger);
  set('--warning', t.warning);
  set('--success', t.success);
  try { localStorage.setItem('qcode-theme', name); } catch (_) {}
  persistPrefs();
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

function handleAgentCommand(args) {
  let name = (args || '').trim().toLowerCase();
  if (!name) {
    showToast('Agent: ' + state.agentMode + ' (/agent orchestrator|plan)');
    return;
  }
  if (name === 'build') name = 'orchestrator';
  if (name !== 'orchestrator' && name !== 'plan') {
    showToast("Unknown agent '" + name + "'. Use orchestrator|plan.");
    return;
  }
  if (state.agentMode === name) {
    showToast('Agent: ' + name);
    return;
  }
  if (state.agentMode === 'subagent') {
    showToast('Subagent sessions cannot change mode');
    return;
  }
  toggleAgentModeTo(name);
}

async function toggleAgentModeTo(next) {
  if (!state.sessionId || state.agentMode === 'subagent') return;
  if (!next) next = state.agentMode === 'plan' ? 'orchestrator' : 'plan';
  try {
    const res = await fetch('/session/' + state.sessionId + '/mode', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ agent_mode: next })
    });
    if (!res.ok) throw new Error(await res.text());
    state.agentMode = next;
    const session = state.openSessions.find(s => s.id === state.sessionId);
    if (session) session.agentMode = next;
    updateStatusBar();
    showToast(next === 'plan' ? 'Plan mode: read-only research, no edits' : 'Orchestrator: lead coordinator with parallel subagents');
  } catch (e) {
    showToast('Failed to set mode: ' + e.message);
  }
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
    session.promptQueue.splice(n - 1, 1);
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
  renderMessages();
  showToast(state.showThinking ? 'Thinking: shown' : 'Thinking: hidden');
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
    case 'model_select': handleModelCommand(''); break;
    case 'model_variant': handleVariantCommand(''); break;
    case 'agent_mode_toggle': toggleAgentMode(); break;
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
  let html = '<div class="queued-head">⏳ Queued <span class="queued-hint">· /clear-queue to cancel</span></div>';
  queue.forEach((text, i) => {
    if (i > 0) html += '<div class="queued-sep">---</div>';
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

function closeOpenThought(msg) {
  if (!msg.timeline) return;
  const last = msg.timeline[msg.timeline.length - 1];
  if (last && last.kind === 'thought') last.closed = true;
}

function renderThoughtBlock(text) {
  const rc = document.createElement('div'); rc.className = 'reasoning-block collapsed';
  const rl = document.createElement('div'); rl.className = 'reasoning-label';
  const est = Math.max(1, Math.floor((text.length + 3) / 4));
  const compact = est >= 1000 ? Math.round(est / 1000) + 'k' : String(est);
  rl.innerHTML = '+ Thought <span class="thought-tokens">· ' + compact + '</span>';
  const rt = document.createElement('div'); rt.className = 'reasoning-text';
  rt.innerHTML = renderMarkdown(text); tagMarkdownLinks(rt);
  rl.addEventListener('click', () => {
    const open = rc.classList.toggle('collapsed');
    rl.innerHTML = (open ? '+ Thought' : '- Thought') + ' <span class="thought-tokens">· ' + compact + '</span>';
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
      } else if (currentAssistantMsg.content && currentAssistantMsg.toolEvents && currentAssistantMsg.toolEvents.length > 0) {
        currentAssistantMsg = { role: 'assistant', content: '', toolEvents: [], timeline: [] };
        session.messages.push(currentAssistantMsg);
      }
      if (currentAssistantMsg.content) {
        currentAssistantMsg.content += '\n\n' + m.content;
      } else {
        currentAssistantMsg.content = m.content;
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
        const tc = currentAssistantMsg.toolEvents.find(t => t.tool_call_id === tr.tool_call_id);
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
        text = r.text || '';
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
      session.agentMode = info.agent_mode || (id.indexOf('ses_') === 0 ? 'subagent' : 'orchestrator');
      session.reasoning = info.reasoning_mode || session.reasoning || 'off';
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

  if (!session.title && session.id) {
    session.title = (session.agentMode === 'subagent' ? 'Subagent ' : 'Session ') + session.id.substring(0, 8);
  }
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
  session.lastUserPrompt = text;
  session.messages.push({ role: 'user', content: text, createdAt: Date.now() });
  
  const assistantMsg = { role: 'assistant', content: '', toolEvents: [], timeline: [], createdAt: Date.now() };
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
    session._retries = session._retries || 0;
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

    if (!receivedComplete && !session.cancelRequested && !assistantMsg.streamError) {
      assistantMsg.stoppedEarly = true;
      throw new Error('The response stream ended before generation completed');
    }
  } catch (e) {
    // Automatic resume on transport abort (keeps same assistantMsg, no duplicate user prompt)
    if ((e.name === 'AbortError' || /network|fetch|aborted|interrupted/i.test(e.message || '')) && !session.cancelRequested && !receivedComplete && (session._retries || 0) < 1) {
      session._retries = (session._retries || 0) + 1;
      showToast('Connection dropped — reconnecting…');
      await new Promise(r => setTimeout(r, 600));
      session.reader = null;
      return runGenerationResume(session, assistantMsg);
    }
    // Some browsers report a transport error after receiving the final chunk.
    // The generation.complete event is authoritative in that case.
    if (e.name !== 'AbortError' && !session.cancelRequested && !receivedComplete && !assistantMsg.streamError) {
      const isNet = /network|fetch|abort|interrupted|failed/i.test(e.message || '');
      const message = isNet ? 'Connection lost — click Retry to continue' : (e.message || 'Connection interrupted');
      showToast(isNet ? 'Connection lost' : ('Generation interrupted: ' + message));
      assistantMsg.streamError = message;
      assistantMsg.stoppedEarly = true;
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

// Resume streaming into the SAME assistantMsg (no duplicate user prompt).
async function runGenerationResume(session, assistantMsg) {
  try {
    const res = await fetch(`/session/${session.id}/generate?resume=1`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        text: '',
        resume: true,
        provider: session.provider,
        model: session.model,
        reasoning_mode: state.reasoning,
        session_id: session.id
      })
    });
    if (!res.ok) throw new Error(await res.text());
    if (!res.body) throw new Error('empty resume stream');
    const reader = res.body.getReader();
    session.reader = reader;
    const decoder = new TextDecoder();
    let buffer = '';
    let gotComplete = false;
    const consume = (line) => {
      if (!line.trim()) return;
      try {
        const event = JSON.parse(line);
        handleEvent(event, assistantMsg, session);
        if (event.type === 'generation.complete') gotComplete = true;
      } catch (_) {}
    };
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      const lines = buffer.split('\n');
      buffer = lines.pop() || '';
      for (const line of lines) consume(line);
    }
    buffer += decoder.decode();
    consume(buffer);
    if (!gotComplete) {
      assistantMsg.stoppedEarly = true;
    } else {
      assistantMsg.streamError = null;
      assistantMsg.stoppedEarly = false;
    }
  } catch (e2) {
    assistantMsg.stoppedEarly = true;
    const isNet = /network|fetch|abort|interrupted|failed/i.test(e2.message || '');
    const msg = isNet ? 'Connection lost — click Retry to continue' : (e2.message || 'Connection interrupted');
    assistantMsg.streamError = assistantMsg.streamError || msg;
    showToast(isNet ? 'Connection lost' : ('Resume failed: ' + assistantMsg.streamError));
  } finally {
    session.reader = null;
    session._retries = 0;
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

  if (session.generating) {
    if (!session.promptQueue) session.promptQueue = [];
    if (session.promptQueue.length > 0) {
      session.promptQueue[session.promptQueue.length - 1] += '\n' + text;
      showToast('Prompt merged into queued message');
    } else {
      session.promptQueue.push(text);
      showToast('Prompt queued');
    }
    promptInput.value = '';
    resizePromptInput();
    updateQueueIndicator();
    renderMessages();
    scrollToBottom();
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
    case 'backend.heartbeat':
      // Periodic keepalive from server — stream is healthy
      break;
    case 'backend.message.delta':
      // Append (do NOT overwrite): backend sends incremental chunks and a
      // final empty-text delta with done=true, which would otherwise wipe
      // the accumulated assistant text (leaving only the token usage line).
      if (evt.text) {
        msg.content = (msg.content || '') + evt.text;
        msg.streamError = null;
      }
      if (session.id === state.sessionId) {
        renderMessages();
        scrollToBottom();
      }
      break;
    case 'backend.reasoning.delta':
      if (evt.text) {
        appendThoughtChunk(msg, evt.text);
        msg.streamError = null;
      }
      if (session.id === state.sessionId && !evt.done) { renderMessages(); scrollToBottom(); }
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
        if (session.id === state.sessionId) { renderMessages(); scrollToBottom(); }
      }
      break;
    }
    case 'backend.tool.call.started':
      if (!msg.toolEvents) msg.toolEvents = [];
      closeOpenThought(msg);
      msg.toolEvents.push({ type: 'tool_call', tool_call_id: evt.tool_call_id, tool_name: evt.tool_name, arguments: evt.arguments, status: 'running' });
      if (!msg.timeline) msg.timeline = [];
      msg.timeline.push({ kind: 'tool', tool_call_id: evt.tool_call_id });
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
      if (evt.severity === 'info') {
        // Heartbeat or status notice — do not flag generation as an error
        break;
      }
      if (evt.severity === 'warning') {
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
    html += '<div class="stat-section-title diff-toggle" tabindex="0">Unified Diff (click to toggle)</div>';
    html += '<div class="diff-view diff-block"><pre>' + esc(diff) + '</pre></div>';
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
    fsListing.innerHTML = '<div class="files-empty">No active session.</div>';
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
  fsListing.innerHTML = '<div class="files-empty">Loading…</div>';
  const runIndex = (state.fsListingRunIndex || 0) + 1;
  state.fsListingRunIndex = runIndex;
  try {
    const q = state.fsDir ? ('?path=' + encodeURIComponent(state.fsDir)) : '';
    const res = await fetch('/session/' + state.sessionId + '/fs/list' + q);
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(data.error || res.statusText || 'list failed');
    if (runIndex !== state.fsListingRunIndex) return; // stale response
    state.fsRawEntries = Array.isArray(data.entries) ? data.entries : [];
    renderFsListingEntries();
  } catch (e) {
    if (runIndex === state.fsListingRunIndex) {
      fsListing.innerHTML = '<div class="files-empty">Failed to list directory: ' + esc(e.message) + '</div>';
    }
  }
}

function fsMatchesFilter(name) {
  const q = (state.fsFilterText || '').toLowerCase();
  if (!q) return true;
  return name.toLowerCase().includes(q);
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
      const linkCls = (!isDir && state.sessionId) ? ' md-internal-link' : '';
      html += '<button type="button" class="fs-entry' + active + linkCls + '" data-type="' + (isDir ? 'dir' : 'file')
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
  if (typeof marked === 'undefined' || !marked.parse) {
    fsViewMarkdown.textContent = content;
    return;
  }
  try {
    const { frontmatter, md } = prepareMarkdownBody(content, { wikilinks: true });
    const renderer = getMarkedRenderer();
    let html = marked.parse(md, { renderer: renderer, gfm: true, breaks: true, headerIds: false, mangle: false });
    if (frontmatter) html = renderFrontmatterBox(frontmatter) + html;
    fsViewMarkdown.innerHTML = html;
    bindFrontmatterToggles(fsViewMarkdown);
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

function scheduleFsHighlight() {
  if (fsHighlightTimer) clearTimeout(fsHighlightTimer);
  fsHighlightTimer = setTimeout(() => {
    fsHighlightTimer = null;
    updateFsHighlight();
  }, 20);
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

  if (fsEditorStatus) fsEditorStatus.textContent = 'Loading…';
  try {
    const readyState = await fetch('/session/' + state.sessionId + '/fs',
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
      const res = await fetch('/session/last');
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

async function toggleAgentMode() {
  if (!state.sessionId || state.agentMode === 'subagent') return;
  const next = state.agentMode === 'plan' ? 'orchestrator' : 'plan';
  await toggleAgentModeTo(next);
}

async function loadDelegatedSessionsTab() {
  if (!sessionsContent) return;
  sessionsContent.innerHTML = '<div class="delegated-empty">Loading child sessions…</div>';
  try {
    let tasks = [];
    try {
      const res = await fetch('/tasks');
      if (res.ok) {
        const data = await res.json();
        if (data.metadata && Array.isArray(data.metadata.tasks)) {
          tasks = data.metadata.tasks;
        }
      }
    } catch (_) {}

    let savedSubagents = [];
    try {
      const resSub = await fetch('/sessions?include_subagents=1');
      if (resSub.ok) {
        const all = await resSub.json();
        if (Array.isArray(all)) {
          savedSubagents = all.filter(s => (s.agent_mode === 'subagent' || (s.id && s.id.startsWith('ses_'))));
        }
      }
    } catch (_) {}

    const seenSids = new Set();
    const combined = [];

    // Active / recent tasks from in-memory registry first
    for (const task of tasks) {
      const sid = task.task_id || task.sessionId || '';
      if (sid) seenSids.add(sid);
      combined.push({
        sid,
        bg: task.background_task_id || '',
        status: task.status || 'running',
        agent: task.agent || 'general',
        mode: task.mode || '',
        model: task.model || '',
        description: task.description || sid
      });
    }

    // Persisted subagent sessions from database
    for (const sub of savedSubagents) {
      if (!sub.id || seenSids.has(sub.id)) continue;
      seenSids.add(sub.id);
      combined.push({
        sid: sub.id,
        bg: '',
        status: 'saved',
        agent: sub.provider || 'subagent',
        mode: sub.agent_mode || 'subagent',
        model: sub.model || '',
        description: sub.title || sub.id
      });
    }

    const totalCount = combined.length;
    let filtered = combined;
    if (state.sessionsFilterText) {
      filtered = combined.filter(c => {
        const text = `${c.sid} ${c.bg} ${c.status} ${c.agent} ${c.mode} ${c.model} ${c.description}`.toLowerCase();
        return text.includes(state.sessionsFilterText);
      });
    }

    if (totalCount === 0) {
      sessionsContent.innerHTML = '<div class="delegated-empty">No delegated child sessions.<br>Spawn with the task tool. Saved parent sessions stay in the session picker.</div>';
      return;
    }

    sessionsContent.innerHTML = '';
    const heading = document.createElement('div');
    heading.className = 'delegated-empty';
    heading.textContent = state.sessionsFilterText
      ? `CHILDREN (${filtered.length} of ${totalCount}) — click to open in chat`
      : `CHILDREN (${totalCount}) — click to open in chat`;
    sessionsContent.appendChild(heading);

    if (filtered.length === 0) {
      const noMatch = document.createElement('div');
      noMatch.className = 'delegated-empty';
      noMatch.textContent = 'No child sessions match your filter.';
      sessionsContent.appendChild(noMatch);
      return;
    }

    for (const item of filtered) {
      const sid = item.sid;
      const row = document.createElement('div');
      row.className = 'delegated-row';
      const isCurrent = sid && sid === state.sessionId;
      if (isCurrent) row.classList.add('current');
      const bg = item.bg;
      const model = item.model;
      const status = item.status;
      row.innerHTML = (isCurrent ? '<span class="child-marker">●</span>' : '')
        + (bg ? '<span class="child-bg">' + esc(bg) + '</span>' : '')
        + '<span class="child-status ' + esc(status) + '">[' + esc(status) + ']</span>'
        + '<span class="child-meta">(' + esc(item.agent) + (item.mode ? ' · ' + esc(item.mode) : '') + ')</span>'
        + (model ? '<span class="child-model">{' + esc(model) + '}</span>' : '')
        + '<span class="child-desc">' + esc(item.description) + '</span>'
        + (isCurrent ? '<span class="child-open">[open]</span>' : '<span class="child-action">↗</span>');

      if (sid) {
        row.setAttribute('role', 'button');
        row.setAttribute('tabindex', '0');
        row.title = 'Open child session ' + sid;
        row.addEventListener('click', () => openChildSession(sid));
        row.addEventListener('keydown', (ev) => {
          if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); openChildSession(sid); }
        });
      } else {
        row.classList.add('no-open');
        row.title = 'No session id for this task';
      }
      sessionsContent.appendChild(row);
    }
  } catch (e) {
    sessionsContent.innerHTML = '<div class="error-panel">Failed to load children: ' + esc(e.message)
      + '<br><button type="button" class="msg-action-btn" data-retry="sessions">Retry</button></div>';
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
  const promptTok = d.prompt_tokens || 0;
  const compTok = d.completion_tokens || 0;
  const totTok = d.total_tokens || 0;
  const toolCalls = d.tool_calls || 0;
  const toolTime = d.total_tool_time_ms || 0;
  const avgToolMs = toolCalls > 0 ? (toolTime / toolCalls).toFixed(0) + ' ms' : '0 ms';
  const md = [
    `# Session Stats: ${d.title || d.id || 'Active Session'}`,
    `- **Session ID:** \`${d.id || state.sessionId || '—'}\``,
    `- **Provider / Model:** ${d.provider || '—'} / \`${d.model || '—'}\``,
    `- **Workspace:** \`${d.workspace || '—'}\``,
    `- **Messages:** ${d.message_count || 0} (${d.user_messages || 0} user, ${d.assistant_messages || 0} assistant)`,
    `- **Tokens:** ${formatNumber(totTok)} total (${formatNumber(promptTok)} prompt, ${formatNumber(compTok)} completion)`,
    `- **Tool Invocations:** ${toolCalls} calls across ${formatMs(toolTime)} (avg ${avgToolMs}/call)`,
  ].join('\n');
  copyText(md, 'Stats summary copied to clipboard');
}

async function loadStatsTab() {
  if (!state.sessionId) {
    lastStatsData = null;
    statsContent.innerHTML = `
      <div class="stats-empty-container">
        <div class="stats-empty-icon">📊</div>
        <div class="stats-empty-title">No Active Session</div>
        <div class="stats-empty-desc">Open or create a session to view real-time token usage, message activity, and tool execution metrics.</div>
      </div>`;
    return;
  }
  statsContent.innerHTML = `
    <div class="stats-loading">
      <div class="stats-skeleton-banner"></div>
      <div class="stats-skeleton-grid">
        <div class="stats-skeleton-card"></div>
        <div class="stats-skeleton-card"></div>
        <div class="stats-skeleton-card"></div>
        <div class="stats-skeleton-card"></div>
      </div>
      <div class="stats-skeleton-card tall"></div>
    </div>`;
  try {
    const res = await fetch('/session/' + state.sessionId + '/stats');
    if (!res.ok) throw new Error(await res.text());
    const data = await res.json();
    lastStatsData = data;
    renderStatsTab(data);
  } catch (e) {
    lastStatsData = null;
    statsContent.innerHTML = '<div class="error-panel">Failed to load stats: ' + esc(e.message)
      + '<br><button type="button" class="msg-action-btn" data-retry="stats">Retry</button></div>';
    const b = statsContent.querySelector('[data-retry="stats"]');
    if (b) b.addEventListener('click', loadStatsTab);
  }
}

function renderStatsTab(data) {
  const promptTokens = Number(data.prompt_tokens) || 0;
  const completionTokens = Number(data.completion_tokens) || 0;
  const totalTokens = Number(data.total_tokens) || (promptTokens + completionTokens);
  const toolCalls = Number(data.tool_calls) || 0;
  const totalToolTimeMs = Number(data.total_tool_time_ms) || 0;
  const msgCount = Number(data.message_count) || 0;
  const userMsgs = Number(data.user_messages) || 0;
  const assistantMsgs = Number(data.assistant_messages) || 0;
  const avgToolTime = toolCalls > 0 ? (totalToolTimeMs / toolCalls) : 0;

  // Percentage calculations
  const promptPct = totalTokens > 0 ? Math.round((promptTokens / totalTokens) * 100) : 0;
  const compPct = totalTokens > 0 ? Math.max(0, 100 - promptPct) : 0;
  const totalDialogue = (userMsgs + assistantMsgs);
  const userPct = totalDialogue > 0 ? Math.round((userMsgs / totalDialogue) * 100) : 50;
  const asstPct = totalDialogue > 0 ? Math.max(0, 100 - userPct) : 50;

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

  const sid = data.id || state.sessionId || '';
  const title = data.title || 'Untitled Session';
  const provider = data.provider || '—';
  const model = data.model || '—';
  const workspace = data.workspace || '';

  const html = `
    <div class="stats-container">
      <!-- Session Header & Identity Card -->
      <div class="stats-session-hero">
        <div class="stats-hero-top">
          <div class="stats-hero-title-group">
            <span class="stats-hero-badge">Active Session</span>
            <h3 class="stats-hero-title" title="${esc(title)}">${esc(title)}</h3>
          </div>
          <div class="stats-hero-id-badge" title="Click to copy Session ID" data-copy-sid="${esc(sid)}">
            <span class="stats-hero-id-label">ID:</span>
            <code class="stats-hero-id-val">${esc(sid || '—')}</code>
            <svg class="ui-icon stats-copy-icon" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>
          </div>
        </div>

        <div class="stats-hero-meta-row">
          <div class="stats-hero-pill" title="Inference Provider">
            <span class="stats-pill-icon">⚡</span>
            <span class="stats-pill-label">Provider:</span>
            <strong class="stats-pill-value">${esc(provider)}</strong>
          </div>
          <div class="stats-hero-pill accent" title="Active Model">
            <span class="stats-pill-icon">🧠</span>
            <span class="stats-pill-label">Model:</span>
            <strong class="stats-pill-value">${esc(model)}</strong>
          </div>
          ${createdFormatted !== '—' ? `
            <div class="stats-hero-pill" title="Created on ${esc(createdFormatted)}">
              <span class="stats-pill-icon">🕒</span>
              <span class="stats-pill-label">Started:</span>
              <strong class="stats-pill-value">${esc(createdRelative || createdFormatted)}</strong>
            </div>
          ` : ''}
        </div>

        ${workspace ? `
          <div class="stats-hero-workspace-row" title="${esc(workspace)}">
            <span class="stats-ws-icon"><svg class="ui-icon" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/></svg></span>
            <span class="stats-ws-label">Workspace:</span>
            <code class="stats-ws-path">${esc(workspace)}</code>
          </div>
        ` : ''}
      </div>

      <!-- KPI Summary Cards Grid -->
      <div class="stat-grid stats-kpi-grid">
        <div class="stat-card kpi-card tokens">
          <div class="stat-card-header">
            <span class="stat-card-icon tokens">🪙</span>
            <span class="stat-label">Total Tokens</span>
          </div>
          <div class="stat-value accent">${esc(formatNumber(totalTokens))}</div>
          <div class="stat-subtext">${esc(formatNumber(promptTokens))} in / ${esc(formatNumber(completionTokens))} out</div>
        </div>

        <div class="stat-card kpi-card messages">
          <div class="stat-card-header">
            <span class="stat-card-icon messages">💬</span>
            <span class="stat-label">Messages</span>
          </div>
          <div class="stat-value">${esc(formatNumber(msgCount))}</div>
          <div class="stat-subtext">${esc(formatNumber(userMsgs))} user • ${esc(formatNumber(assistantMsgs))} assistant</div>
        </div>

        <div class="stat-card kpi-card tools">
          <div class="stat-card-header">
            <span class="stat-card-icon tools">🛠️</span>
            <span class="stat-label">Tool Calls</span>
          </div>
          <div class="stat-value">${esc(formatNumber(toolCalls))}</div>
          <div class="stat-subtext">${toolCalls > 0 ? (toolCalls / Math.max(1, userMsgs)).toFixed(1) + ' calls/prompt' : 'No tools run'}</div>
        </div>

        <div class="stat-card kpi-card duration">
          <div class="stat-card-header">
            <span class="stat-card-icon duration">⏱️</span>
            <span class="stat-label">Tool Execution</span>
          </div>
          <div class="stat-value">${esc(formatMs(totalToolTimeMs))}</div>
          <div class="stat-subtext">${toolCalls > 0 ? 'avg ' + esc(formatMs(avgToolTime)) + '/call' : '0 ms latency'}</div>
        </div>
      </div>

      <!-- Token Distribution Breakdown Card -->
      <div class="stat-card section-card">
        <div class="stat-section-header">
          <div class="stat-section-title-wrap">
            <span class="stat-section-icon">📈</span>
            <span class="stat-section-title">Token Economy Breakdown</span>
          </div>
          <span class="stat-section-badge">${totalTokens > 0 ? 'Active' : 'Idle'}</span>
        </div>

        <div class="stat-progress-container">
          <div class="stat-progress-bar" role="progressbar" aria-valuenow="${promptPct}" aria-valuemin="0" aria-valuemax="100">
            <div class="stat-progress-segment prompt" style="width: ${promptPct}%" title="Prompt Tokens: ${promptPct}%"></div>
            <div class="stat-progress-segment completion" style="width: ${compPct}%" title="Completion Tokens: ${compPct}%"></div>
          </div>
          <div class="stat-progress-legend">
            <div class="stat-legend-item">
              <span class="legend-dot prompt"></span>
              <span class="legend-label">Prompt Tokens:</span>
              <strong class="legend-val">${esc(formatNumber(promptTokens))}</strong>
              <span class="legend-pct">(${promptPct}%)</span>
            </div>
            <div class="stat-legend-item">
              <span class="legend-dot completion"></span>
              <span class="legend-label">Completion Tokens:</span>
              <strong class="legend-val">${esc(formatNumber(completionTokens))}</strong>
              <span class="legend-pct">(${compPct}%)</span>
            </div>
          </div>
        </div>
      </div>

      <!-- Dialogue & Tool Activity Breakdown Cards -->
      <div class="stats-two-col-grid">
        <!-- Dialogue Balance -->
        <div class="stat-card section-card">
          <div class="stat-section-header">
            <div class="stat-section-title-wrap">
              <span class="stat-section-icon">🗣️</span>
              <span class="stat-section-title">Conversation Balance</span>
            </div>
          </div>
          <div class="stat-progress-container">
            <div class="stat-progress-bar small">
              <div class="stat-progress-segment user-segment" style="width: ${userPct}%" title="User msgs: ${userPct}%"></div>
              <div class="stat-progress-segment asst-segment" style="width: ${asstPct}%" title="Assistant msgs: ${asstPct}%"></div>
            </div>
            <div class="stat-progress-legend">
              <div class="stat-legend-item">
                <span class="legend-dot user-segment"></span>
                <span class="legend-label">User:</span>
                <strong class="legend-val">${esc(formatNumber(userMsgs))}</strong>
              </div>
              <div class="stat-legend-item">
                <span class="legend-dot asst-segment"></span>
                <span class="legend-label">Assistant:</span>
                <strong class="legend-val">${esc(formatNumber(assistantMsgs))}</strong>
              </div>
            </div>
          </div>
          <div class="stat-detail-item">
            <span class="detail-label">Avg Tokens / Message:</span>
            <span class="detail-val">${msgCount > 0 ? esc(formatNumber(Math.round(totalTokens / msgCount))) : 0} tok</span>
          </div>
        </div>

        <!-- Tool Performance Details -->
        <div class="stat-card section-card">
          <div class="stat-section-header">
            <div class="stat-section-title-wrap">
              <span class="stat-section-icon">⚙️</span>
              <span class="stat-section-title">Tool Execution Stats</span>
            </div>
          </div>
          <div class="stat-details-list">
            <div class="stat-detail-item">
              <span class="detail-label">Total Invocations:</span>
              <span class="detail-val">${esc(formatNumber(toolCalls))}</span>
            </div>
            <div class="stat-detail-item">
              <span class="detail-label">Accumulated Runtime:</span>
              <span class="detail-val">${esc(formatMs(totalToolTimeMs))}</span>
            </div>
            <div class="stat-detail-item">
              <span class="detail-label">Mean Latency per Tool:</span>
              <span class="detail-val">${esc(formatMs(avgToolTime))}</span>
            </div>
          </div>
        </div>
      </div>
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



function renderMessages() {
  messagesEl.innerHTML = '';
  const session = state.openSessions.find(s => s.id === state.sessionId);
  if (!session) return;

  const isSub = session.agentMode === 'subagent' || (session.id && session.id.startsWith('ses_'));
  if (isSub) {
    const banner = document.createElement('div');
    banner.className = 'subagent-session-banner';
    const modelLabel = session.model ? `${session.provider ? session.provider + ' / ' : ''}${session.model}` : '';
    banner.innerHTML = `
      <div class="subagent-banner-info">
        <span class="subagent-banner-icon">🤖</span>
        <span class="subagent-banner-title">Delegated Child Session</span>
        <span class="subagent-banner-id">${esc(session.id)}</span>
        ${modelLabel ? `<span class="subagent-banner-model">${esc(modelLabel)}</span>` : ''}
      </div>
      <button type="button" class="subagent-banner-back-btn" title="Return to parent session (Esc or b)">← Back to Parent</button>
    `;
    const backBtn = banner.querySelector('.subagent-banner-back-btn');
    if (backBtn) backBtn.addEventListener('click', returnToParentSession);
    messagesEl.appendChild(banner);
  }

  const queue = session.promptQueue || [];
  if (state.reasoning === 'off' && session.messages.length > 0) {
    const hint = document.createElement('div');
    hint.className = 'thinking-hint';
    hint.textContent = 'Reasoning is Off — set Low/Med/High or /variant for thinking trace.';
    messagesEl.appendChild(hint);
  }
  if (session.messages.length === 0 && queue.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'empty-chat';
    empty.innerHTML = '<div class="empty-chat-mark">Q</div>'
      + '<h2>What are we building?</h2>'
      + '<p>Ask QCode to explore the workspace, write code, or run a command.</p>'
      + '<div class="empty-chat-hint"><kbd>/</kbd> Commands <span>•</span> <kbd>Ctrl</kbd>+<kbd>P</kbd> Palette <span>•</span> <kbd>Enter</kbd> Send</div>';
    messagesEl.appendChild(empty);
    return;
  }
  for (const msg of session.messages) {
    messagesEl.appendChild(renderMessage(msg));
  }
  if (queue.length > 0) messagesEl.appendChild(renderQueuedBlock(queue));
}

function renderMessage(msg) {
  const div = document.createElement('div'); div.className = 'message ' + msg.role;
  const content = document.createElement('div'); content.className = 'message-content';
  if (msg.role !== 'user') {
    const header = document.createElement('div'); header.className = 'message-header';
    const icons = { user: SVG_ICONS.user, assistant: SVG_ICONS.assistant, system: SVG_ICONS.system, tool: SVG_ICONS.tool };
    header.innerHTML = '<span class="role-icon">' + (icons[msg.role] || '') + '</span> ' + capitalize(msg.role);
    if (msg.createdAt) {
      const ts = document.createElement('span'); ts.className = 'msg-ts'; ts.title = new Date(msg.createdAt).toLocaleString();
      ts.textContent = relTime(msg.createdAt); header.appendChild(ts);
    }
    div.appendChild(header);
  }
  // Interleaved thinking/tool timeline: each thought renders adjacent to the
  // tool call it preceded. Falls back to legacy order when no timeline exists.
  if (msg.timeline && msg.timeline.length > 0) {
    const byId = {};
    if (msg.toolEvents) for (const t of msg.toolEvents) byId[t.tool_call_id] = t;
    for (const entry of msg.timeline) {
      if (entry.kind === 'tool') {
        const t = byId[entry.tool_call_id];
        if (!t) continue;
        const tc = document.createElement('div'); tc.className = 'tool-events';
        tc.appendChild(renderToolBlock(t));
        content.appendChild(tc);
      } else if (entry.kind === 'thought' && entry.text && state.showThinking) {
        content.appendChild(renderThoughtBlock(entry.text));
      }
    }
  } else {
    if (msg.toolEvents && msg.toolEvents.length > 0) {
      const tc = document.createElement('div'); tc.className = 'tool-events';
      for (const t of msg.toolEvents) tc.appendChild(renderToolBlock(t));
      content.appendChild(tc);
    }
    if (msg.reasoning && state.showThinking) {
      content.appendChild(renderThoughtBlock(msg.reasoning));
    }
  }
  if (msg.usage) {
    const uc = document.createElement('div'); uc.className = 'usage-block';
    uc.textContent = msg.usage;
    content.appendChild(uc);
  }
  if (msg.content) {
    const textEl = document.createElement('div'); textEl.className = 'md-content';
    textEl.innerHTML = renderMarkdown(msg.content);
    tagMarkdownLinks(textEl);
    
    const activeSession = state.openSessions.find(s => s.id === state.sessionId);
    const isGenerating = activeSession && activeSession.generating;
    const isLastMsg = activeSession && msg === activeSession.messages[activeSession.messages.length - 1];
    
    if (isGenerating && isLastMsg) textEl.className += ' streaming-cursor';
    content.appendChild(textEl);
  }
  if (msg.stoppedEarly && !msg.streamError) {
    const warn = document.createElement('div');
    warn.className = 'stopped-early';
    warn.textContent = 'Stopped early — partial response kept. Retry to continue.';
    content.appendChild(warn);
  }
  if (msg.streamError) {
    const isInterrupted = /interrupted|stream ended|abort|cancel|network|fetch|connection|offline/i.test(msg.streamError);
    const title = isInterrupted ? 'Response interrupted' : 'Generation error';
    const error = document.createElement('div');
    error.className = 'stream-error' + (isInterrupted ? ' interrupted' : '');
    error.innerHTML = '<span>!</span><div><strong>' + title + '</strong><br>'
      + esc(msg.streamError) + '</div>';
    if (isInterrupted) {
      const retryBtn = document.createElement('button');
      retryBtn.type = 'button';
      retryBtn.className = 'msg-action-btn error-retry-btn';
      retryBtn.textContent = 'Retry';
      retryBtn.title = 'Retry generation';
      retryBtn.addEventListener('click', () => handleRetryCommand());
      error.appendChild(retryBtn);
    }
    content.appendChild(error);
  }
  const acts = document.createElement('div'); acts.className = 'msg-actions';
  const mkBtn = (label, title, fn) => {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'msg-action-btn';
    b.textContent = label; b.title = title; b.setAttribute('aria-label', title); b.addEventListener('click', fn); acts.appendChild(b); return b;
  };
  mkBtn('Copy', 'Copy message text', () => copyText(msg.content || '', 'Message copied'));
  if (msg.role === 'assistant' && msg.content) {
    mkBtn('Raw', 'Toggle raw markdown', () => {
      const pre = div.querySelector('.md-content');
      if (pre) { const showing = pre.dataset.raw === '1'; pre.dataset.raw = showing ? '0' : '1'; pre.textContent = showing ? '' : (msg.content || ''); if (showing) { pre.innerHTML = renderMarkdown(msg.content); tagMarkdownLinks(pre); } }
    });
    mkBtn('Rerun', 'Retry from this prompt', () => handleRetryCommand());
  }
  content.appendChild(acts);
  // T3.1 copy-code buttons
  content.querySelectorAll('pre code').forEach((code) => {
    const pre = code.parentElement;
    if (!pre || pre.querySelector('.copy-code-btn')) return;
    pre.style.position = 'relative';
    const cb = document.createElement('button'); cb.type = 'button'; cb.className = 'copy-code-btn'; cb.textContent = 'Copy'; cb.setAttribute('aria-label', 'Copy code block');
    cb.addEventListener('click', (e) => { e.stopPropagation(); copyText(code.innerText, 'Code copied'); });
    pre.appendChild(cb);
  });
  div.appendChild(content); return div;
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

  const icons = { bash: '$', task: '🤖', read_file: '📄', write_file: '✏️', view_file: '📄', edit_file: '✏️', search: '🔍', grep: '🔍', ripgrep: '🔍' };
  const icon = icons[toolName] || '⚙';
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
  if (childSid) {
    const openEl = document.createElement('button');
    openEl.type = 'button';
    openEl.className = 'task-open-child';
    openEl.textContent = '↗ open child';
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
    childActionRow.innerHTML = `<button type="button" class="tool-open-child-btn"><span>🤖 Open child session (${esc(childSid)})</span> <span>→</span></button>`;
    const openBtn = childActionRow.querySelector('.tool-open-child-btn');
    if (openBtn) {
      openBtn.addEventListener('click', (ev) => {
        ev.stopPropagation();
        openChildSession(childSid);
      });
    }
    body.appendChild(childActionRow);
  }

  let expanded = false;
  header.addEventListener('click', (ev) => {
    if (childSid && ev.target.classList && ev.target.classList.contains('task-open-child')) {
      openChildSession(childSid);
      return;
    }
    expanded = !expanded;
    body.classList.toggle('collapsed', !expanded);
    chevron.textContent = expanded ? '▾' : '▸';
  });

  block.appendChild(header);
  block.appendChild(body);
  return block;
}

function addMessage(role, content) { const div = renderMessage({ role, content }); messagesEl.appendChild(div); scrollToBottom(); }


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

function resolveMdLink(href) {
  if (!href) return null;
  const isAnchor = href.startsWith('#');
  const isFullPath = href.startsWith('/');
  if (/^(https?:|mailto:|tel:|data:|\/\/)/i.test(href)) return null;
  const hashIdx = href.indexOf('#');
  const pathPart = (hashIdx >= 0 ? href.slice(0, hashIdx) : href).split('?')[0];
  const fragment = hashIdx >= 0 ? href.slice(hashIdx + 1) : '';
  let baseDir = '';
  if (state.fsOpenPath) {
    const idx = state.fsOpenPath.lastIndexOf('/');
    baseDir = idx >= 0 ? state.fsOpenPath.slice(0, idx) : '';
  } else if (state.fsDir) {
    baseDir = state.fsDir;
  }
  // A path-less `#heading` refers to the currently open document.
  const rel = isAnchor && !pathPart ? (state.fsOpenPath || '') : normalizeRelPath(baseDir, pathPart);
  return { rel, fragment, isAnchor, isFullPath };
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
  const { frontmatter, md } = prepareMarkdownBody(text, { wikilinks: true });
  const renderer = getMarkedRenderer();
  const parseOptions = { renderer: renderer, gfm: true, breaks: true, headerIds: false, mangle: false };
  let html = marked.parse(md, parseOptions);
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
function tagFsLinks(container, mdOnly) {
  if (!container || !state.sessionId) return;
  container.querySelectorAll('a[href]').forEach((a) => {
    const href = a.getAttribute('href') || '';
    if (/^(https?:|mailto:|tel:|data:|\/\/)/i.test(href) || href.startsWith('/')) return;
    if (MD_IMG_EXTS.test(href.split('?')[0].split('#')[0].split('.').pop() || '')) return;
    a.classList.add('md-internal-link');
  });
  container.querySelectorAll('img[src]').forEach((img) => {
    const src = img.getAttribute('src') || '';
    if (src && !/^(https?:|data:|\/\/|\/)/i.test(src)) {
      let baseDir = '';
      if (state.fsOpenPath) {
        const idx = state.fsOpenPath.lastIndexOf('/');
        baseDir = idx >= 0 ? state.fsOpenPath.slice(0, idx) : '';
      }
      const relPath = normalizeRelPath(baseDir, src) || src;
      img.src = '/session/' + state.sessionId + '/fs/raw?path=' + encodeURIComponent(relPath);
    }
  });
}

function setGenerating(on) {
  sendBtn.disabled = false;
  promptInput.disabled = false;
  sendBtn.innerHTML = on ? '<span>Queue</span><span class="send-icon">＋</span>' : '<span>Send</span><span class="send-icon">↑</span>';
  document.body.classList.toggle('is-generating', Boolean(on));
  sendBtn.setAttribute('aria-label', on ? 'Queue prompt' : 'Send message');
  if (pauseBtn) pauseBtn.classList.toggle('hidden', !on);
  if (typeof retryBtn !== 'undefined' && retryBtn) retryBtn.disabled = Boolean(on);
}
function nearBottom() {
  const c = document.getElementById('chat-container');
  if (!c) return true;
  return (c.scrollHeight - c.scrollTop - c.clientHeight) < 120;
}
function scrollToBottom(force) {
  const c = document.getElementById('chat-container');
  if (!c) return;
  if (force || nearBottom() || document.body.classList.contains('is-generating')) {
    c.scrollTop = c.scrollHeight;
  }
  updateJumpPill();
}
function updateJumpPill() {
  const c = document.getElementById('chat-container');
  if (!c || !document.getElementById('main-area')) return;
  let pill = document.getElementById('jump-latest');
  const show = (c.scrollHeight - c.scrollTop - c.clientHeight) > 300;
  if (show && !pill) {
    pill = document.createElement('button'); pill.id = 'jump-latest'; /* id="jump-latest" */ pill.className = 'jump-latest';
    pill.textContent = '↓ Latest'; pill.type = 'button'; pill.setAttribute('aria-label', 'Jump to latest message');
    pill.addEventListener('click', () => { c.scrollTop = c.scrollHeight; updateJumpPill(); });
    document.getElementById('main-area').appendChild(pill);
  } else if (!show && pill) pill.remove();
}


function showToast(msg) { const t = document.createElement('div'); t.className = 'toast'; t.textContent = msg; document.body.appendChild(t); setTimeout(() => t.remove(), 4000); }

// ── T2 Persistence (qcode.* localStorage) ──
const PERSIST_KEYS = ['provider','model','reasoning','theme','activeTab','showThinking','layoutMode','filesSubtab','sessionId'];
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
  state.agentMode = session.agentMode || (id.indexOf('ses_') === 0 ? 'subagent' : 'orchestrator');
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
  scrollToBottom();
  renderSessionTabs();
  updateStatusBar();
  closeMobileSidebar();
  updateQueueIndicator();
  restoreDraft();

  // Refresh the auxiliary tabs for the newly active session.
  if (state.activeTab === 'files') loadFilesTab();
  else if (state.activeTab === 'stats') loadStatsTab();
  else if (state.activeTab === 'sessions') loadDelegatedSessionsTab();
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

  state.pinned = state.pinned || [];
  let list = [...state.openSessions];
  const q = (state.sessionsFilterText || '').toLowerCase().trim();
  if (q) list = list.filter(x => ((x.title||'') + ' ' + (x.workspace||'') + ' ' + x.id).toLowerCase().includes(q));
  list.sort((a, b) => ((state.pinned.includes(b.id)?1:0) - (state.pinned.includes(a.id)?1:0)));
  try {
    const raw = localStorage.getItem('qcode-pinned');
    if (raw) state.pinned = JSON.parse(raw);
  } catch (_) {}
  sessionTabsContainer.innerHTML = list.map(session => {
    const isActive = session.id === state.sessionId;
    const pinned = (state.pinned||[]).includes(session.id) ? ' pinned' : '';
    const activeClass = isActive && (state.layoutMode === 'split' || state.activeTab === 'chat') ? 'active' : '';
    const title = session.title || 'Session';
    const genIndicator = session.generating ? '<span class="session-gen-indicator">⏳</span> ' : '';
    const ws = session.workspace ? shortPath(session.workspace) : '';
    
    const isSub = session.agentMode === 'subagent' || (session.id && session.id.startsWith('ses_'));
    const iconHtml = isSub ? '<span class="session-subagent-icon">🤖</span>' : SVG_ICONS.chat;
    const subPill = isSub ? '<span class="session-subagent-pill">child</span>' : '';
    return `
      <div class="session-item-wrapper ${activeClass ? 'active' : ''} ${isSub ? 'is-subagent' : ''}${pinned}" data-id="${session.id}">
        <div class="session-item-main" data-id="${session.id}">
          <div class="session-item-title-row">
            <span class="session-icon">${iconHtml}</span>
            <span class="session-title-text" title="${esc(title)}">${genIndicator}${esc(title)}</span>
            ${subPill}
          </div>
          ${ws ? `<div class="session-item-workspace" title="${esc(session.workspace)}">📁 ${esc(ws)}</div>` : ''}
        </div>
        <div class="session-item-actions">
          <button class="session-action-btn pin-session-btn" data-id="${session.id}" title="Pin/unpin">${(state.pinned||[]).includes(session.id) ? '★' : '☆'}</button>
          ${isSub
            ? `<button class="session-action-btn close-session-tab-btn" data-id="${session.id}" title="Close tab (keeps saved session)">&times;</button>`
            : `<button class="session-action-btn rename-session-btn" data-id="${session.id}" data-title="${esc(title)}" title="Rename">${SVG_ICONS.rename}</button>`}
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
