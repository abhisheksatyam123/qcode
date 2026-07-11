# QCode — Terminal Access + Workspace-per-Session

## Goal
1. Workspace (cwd) tracked per session in DB ✅
2. Terminal access in Web UI (HTTP streaming) ✅
3. New-session UI with workspace directory picker ✅
4. Status bar showing session + workspace ✅
5. Resumable sessions: full history persisted (User+Assistant) & reloaded on resume ✅
6. Server self-bootstraps SQLite schema (init_database) — no TUI dependency ✅
7. Markdown: unordered/ordered lists, blockquote, hr, inline ✅
8. Auto-restore last session (+history) on page load ✅

## Systems

### Build
- Toolchain: `/home/abhi/envs/toolchain/bin/{cmake,ninja,g++}`
- `export PATH="/home/abhi/envs/toolchain/bin:$PATH" && cd build && ninja -j$(nproc)`
- **BUILD: OK**

### Changed Files (13 files, +1526 / -426 lines)

| File | Change |
|------|--------|
| `include/ai/tui/db.h` | workspace param, `SessionInfo` struct, `list_sessions_full()`, `get_session_workspace()` |
| `include/ai/tui/chat_bus.h` | `workspace` field in `GenerationContext` |
| `include/ai/tui/views.h` | `db::SessionInfo` type for render_view |
| `src/tui/db.cpp` | Schema v2 migration (workspace column), workspace in create/list queries |
| `src/tui/commands.cpp` | `/new [workspace]` accepts optional workspace |
| `src/tui/views.cpp` | Session popup shows workspace `[path]` |
| `src/tui/main.cpp` | Uses `list_sessions_full()`, `SessionInfo` type |
| `src/server/server_main.cpp` | PTY manager, terminal CRUD endpoints, POST /sessions, GET /session/:id, workspace in /generate |
| `src/server/webui/index.html` | Terminal panel, status bar, new session btn, xterm.js CDN |
| `src/server/webui/app.js` | Terminal (xterm + HTTP polling), new session modal, status bar, session picker w/ workspace |
| `src/server/webui/style.css` | Status bar, terminal panel, sidebar actions, modal styles |

### API Endpoints (new/updated)
- `POST /sessions` — create session `{provider, model, workspace}`
- `GET /sessions` — includes workspace field
- `GET /session/<id>` — session info with workspace
- `GET /session/<id>/messages` — full message history `[{role,content}]`
- `GET /session/last` — last active session + its history (client auto-restore)
- `POST /terminal/create` — create PTY `{workspace}`
- `POST /terminal/<id>/input` — send keystrokes `{data}`
- `GET /terminal/<id>/stream` — read terminal output
- `DELETE /terminal/<id>` — kill PTY

### Web UI Features
- **Status bar**: session title + workspace path below QCode header
- **"+ New Session" button**: modal with title + workspace path inputs
- **"Terminal" button**: toggles terminal panel (xterm.js, HTTP polling to PTY)
- **Session picker**: shows workspace path alongside session title
- **/new** slash command opens new session modal
- **/rename** opens rename dialog (shows current title)
- **Session resume**: loading a session via picker or `/session` restores its full chat history
- **Auto-restore**: page reload re-opens the last active session with its history
- **Markdown**: proper bullet/numbered lists, blockquotes, horizontal rules, inline code

### Fixes (this pass)
- **`/model` and `/session` now open a fuzzy-find popup**: instead of a static arrow-key list, typing `/model` (or `/session`) opens a command-palette modal with a live text input. As you type, items are filtered by subsequence fuzzy match (scored with consecutive/word-boundary bonuses) across name/id/provider/workspace, with ↑↓ navigation, Enter to select, Esc to cancel. `/model gpt` pre-fills the filter; clicking an item also selects.


- **`/model` (and other slash commands) didn't run on Enter**: the slash-menu autocomplete consumed the first Enter (it just inserted `/model ` into the box) instead of executing the command, so the model picker never opened. Now Enter/Tab on an active slash suggestion executes the command directly (opens the picker/modal), and clicking a suggestion does the same.


- **Model picker was broken**: `selectModel()` set `state.model` then called `onProviderChange()`, which clobbered it back to the first model and never restored the choice; `onProviderChange` also added a duplicate `change` listener on every call. Now a single `applyProviderModel()` handles dropdown + state consistently, and the model `change` listener is registered once.
- **Session load didn't restore the model**: session endpoints didn't expose provider/model, so loading/resuming a session couldn't switch the active model. Now `/sessions`, `/session/<id>`, and `/session/last` return `provider`+`model`, the server keeps them in sync on each `/generate` (`set_session_provider_model`), and `loadSessionById`/auto-restore apply them via `applyProviderModel()`.


- **Server now calls `db::init_database()` at startup** so sessions/messages tables always exist
  (previously relied on the TUI having created the schema first; fresh DB → all session I/O silently failed).
- **Multi-turn was broken after the first reply**: `generation_started` was never reset, so the 2nd+
  `/generate` on an in-memory session never started a new generation thread. Now reset per request.
- **History reload on resume**: when `/generate` targets a session that exists in DB but not in memory
  (server restart / switched client), prior User+Assistant turns are loaded so the model keeps context.
- **Assistant replies now persisted** to DB on `generation.complete` (only `User` was saved before).

### TUI Features
- `/new /path/to/workspace` — creates session with workspace
- Session popup shows `[workspace]` next to session titles
