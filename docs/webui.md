# WebUI Architecture & Features

Stack: `apps/webui/src/` — `app.js`, `utils.js` (pure exports), `index.html`, `style.css`.
Build: `cd apps/webui && npm run build` (builds into `apps/webui/dist/` via Vite).
Tests: `npm test --prefix apps/webui` (32 tests: 16 integration + 16 unit).

## Modules
- `utils.js` (pure, no DOM): fuzzyScore/Filter, shortPath, formatBytes/Ms,
  detectFsLanguage, parseToolValue, sessionIdFromTaskResult,
  extractChildSessionId, esc, capitalize, relTime, extractFrontmatter,
  stripFrontmatter, transformWikilinks, SLASH_COMMANDS, PALETTE_COMMANDS.
  `app.js` imports these utilities; unit tests import `utils.js` directly.
- `app.js` (stateful UI & networking): parseFrontmatter (`window.jsyaml`),
  renderMarkdown/getMarkedRenderer/renderFrontmatterBox/tagMarkdownLinks,
  streaming message rendering, modal management, layout transitions, terminal, and file explorer.

## Layout & Navigation

### 1. Collapsible Left Navigation Sidebar
- Collapsible on all screens (desktop & mobile) via `Ctrl+B` (or `Cmd+B`), `#menu-toggle-btn` in `#main-header`, or `.sidebar-close-btn` inside `#sidebar`.
- When collapsed, `#main-area` expands to 100% full width, dedicating maximum screen real estate to chat, editor, and terminal.
- Anchored `#menu-toggle-btn` with `aria-expanded` and tooltip states ("Collapse sidebar" / "Expand sidebar").
- Collapsed state is persisted across reloads via `localStorage.getItem('qcode-sidebar-collapsed')`.

### 2. Dedicated Full-Screen Terminal
- Terminal operates as a dedicated full-screen tab view (`activeTab === 'terminal'`).
- Eliminated split-pane mode to maximize terminal viewing area and avoid resize jitter.
- Terminal automatically initializes, reconnects, and invokes `fitAddon.fit()` on layout transitions.

### 3. Collapsible File Tree in Files View
- File browser panel can be collapsed via `#fs-collapse-btn` or keyboard shortcut `Alt+F`.
- When the file tree is collapsed, the code editor and diff viewer expand to 100% full width.
- Collapsed state is persisted via `localStorage.getItem('qcode-fs-tree-collapsed')`.

### 4. Clean Header & Title Rendering
- HTML/SVG tags are sanitized using `stripTags()` to prevent escaped markup from leaking into titles.
- Workspace directory displays with an inline SVG folder icon and shortened path, with full path visible on hover tooltip.
- Browser `document.title` reflects `<Session Title> · QCode`.

### 5. New Session Modal & Title Reflection
- Modal provides dedicated inputs for session title (`#ns-title`) and workspace directory (`#ns-workspace`).
- Submitting via Enter or button sends `POST /sessions` with resolved title and workspace.
- The user-entered title is immediately reflected in the header, tab bar, sidebar session list, and document title without being overwritten by session UUIDs.

## Streaming & Generation
- `runGeneration` streams NDJSON `POST /session/:id/generate` via `getReader`.
- Transport abort automatically invokes `?resume=1` retry into the same `assistantMsg` without duplicating prompts.
- Inline stream errors surface gracefully via `.stream-error` banners.
- Server maintains 2.5s `backend.heartbeat` keepalives, per-turn abort flags, and graceful shutdown handling.

## Thinking & Reasoning Visibility
- Reasoning effort configurable via `/variant` or dropdown (`off`, `low`, `medium`, `high`).
- Thinking trace toggle (`#thinking-toggle-btn` / `F2`) allows instantly expanding or collapsing model reasoning blocks.
- Reasoning is visible by default when generated, styled inside collapsible `<details class="thought-block">`.

## Subagents & Delegated Tasks Tab
- Displays all delegated child sessions and live background tasks from `GET /tasks` and `GET /sessions?include_subagents=1`.
- Scoped to active parent session.
- Subagent sessions display a `🤖 child` badge.
- When viewing a subagent session, `#parent-back-btn` (`← parent`) enables one-click return to the orchestrator session.
- Permanently deleting an orchestrator session cascade-deletes all its child subagent sessions.

## UI Polish & Styling
- **Unified Tool Call Background**: All tool blocks (`.tool-block`, `.tool-header`, `.tool-body`, `.tool-output`, `.error`, and `.message.tool`) share `#0d131f` background for visual consistency.
- **Background Message Sync**: Automatically synchronizes session messages on window focus, online event, or visibility changes without interrupting active streaming turns.
- **Theme Engine**: Complete color theme switching via `/theme` (including `opencode`, `tokyonight`, `dracula`, `monokai`, etc.).

## Persistence & Local Storage
- `qcode-prefs`: Provider, model, reasoning, active tab, thinking toggle, theme.
- `qcode-sidebar-collapsed`: Navigation sidebar collapsed state.
- `qcode-fs-tree-collapsed`: File explorer tree collapsed state.
- `qcode-pinned`: Array of pinned/starred session IDs (`★/☆`).
- `qcode-draft-<sessionId>`: Per-session input draft autosave (cleared on send).

## Vendoring (Local-First)
All third-party frontend dependencies are vendored locally under `src/` to guarantee offline and headless operation:
- `vendor-xterm.min.js` & `vendor-xterm.min.css` (Terminal emulation)
- `vendor-addon-fit.min.js` (Xterm responsive layout fit)
- `vendor-hljs.min.js` & `vendor-hljs-github-dark.min.css` (Syntax highlighting for 32+ languages)
- `vendor-marked.min.js` (GFM Markdown parsing)
- `vendor-jsyaml.min.js` (YAML frontmatter parsing)
