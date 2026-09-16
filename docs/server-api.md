# qcode-server HTTP API

Unversioned JSON API served by `qcode-server` (default port 9080). WebUI static files are mounted at `/` when present.

`GET /api/version` returns `{"name":"qcode-server","version":"<PROJECT_VERSION>"}`.

## System

| Method | Path | Notes |
|--------|------|--------|
| GET | `/health`, `/api/health` | `{"status":"ok"}` |
| GET | `/api/version` | server name + version |
| GET | `/providers` | provider/model catalog from config |
| GET | `/api/models/performance` | telemetry summaries |
| POST | `/api/models/feedback` | body: `{"model_id": "...", "rating": "up"|"down"}` |

## Sessions & Generation

| Method | Path | Notes |
|--------|------|--------|
| POST | `/sessions` | Create a new session with provider, model, workspace, and optional title |
| GET | `/sessions` | List parent sessions; `?include_subagents=1` includes child rows; `&parent_session_id=<id>` scopes to parent |
| GET | `/tasks` | Live `TaskTool` subagent registry (`metadata.tasks`); `?parent_session_id=<id>` scopes to parent session |
| GET | `/session/last` | Last active session metadata and message history |
| GET | `/session/:id` | Session metadata (includes subagent rows, `agent_mode`, `reasoning_mode`, `parent_session_id`) |
| POST | `/rename` | Rename session: `{"session_id": "...", "title": "..."}` |
| POST | `/session/cancel` | Cancel generation for active session: `{"session_id": "..."}` |
| POST | `/session/:id/mode` | Set agent mode: `{"agent_mode": "plan"|"orchestrator"}` |
| DELETE | `/session/:id` | Permanently delete session and cascade delete all messages |
| POST | `/session/:id/clear` | Truncate message history for a session |
| GET | `/session/:id/messages` | List historical messages for session |
| POST | `/session/:id/compact` | Summarize/compact session context with LLM; optional `{"keep": N}` |
| GET | `/session/:id/stats` | Aggregate session token and tool runtime statistics |
| POST | `/session/:id/generate` | NDJSON stream of generation bus events (tokens, tool calls, results) |
| POST | `/generate` | Legacy single-turn generate: `{"session_id": "...", "text": "..."}` |

### Session Endpoints Detail

#### Create Session (`POST /sessions`)
Creates a session row in SQLite and returns the generated UUID and resolved display title.

- **Request Body**:
  ```json
  {
    "provider": "anthropic",
    "model": "claude-sonnet-4-6",
    "workspace": "/home/user/project",
    "title": "My Feature Project"
  }
  ```
  *Note*: `custom_id` is supported as an alias/fallback for `title`. If `title` is omitted, the title defaults to `"Session - <model>"`.

- **Response (200 OK)**:
  ```json
  {
    "id": "3f6e4a2d-8b1c-4f9e-9d2a-1c5e7b3a9f0d",
    "workspace": "/home/user/project",
    "title": "My Feature Project"
  }
  ```

- **Error (400 Bad Request)**:
  ```json
  {"error": "provider and model required"}
  ```

#### List Sessions (`GET /sessions`)
- **Query Parameters**:
  - `include_subagents` (optional, boolean `1` or `true`): Include child/delegated subagent sessions.
  - `parent_session_id` (optional, string): Scope listed sessions to subagents of a specific parent.

- **Response (200 OK)**:
  ```json
  [
    {
      "id": "3f6e4a2d-8b1c-4f9e-9d2a-1c5e7b3a9f0d",
      "title": "My Feature Project",
      "workspace": "/home/user/project",
      "provider": "anthropic",
      "model": "claude-sonnet-4-6",
      "agent_mode": "orchestrator",
      "reasoning_mode": "off",
      "parent_session_id": ""
    }
  ]
  ```

#### Rename Session (`POST /rename`)
- **Request Body**:
  ```json
  {
    "session_id": "3f6e4a2d-8b1c-4f9e-9d2a-1c5e7b3a9f0d",
    "title": "Refactored Core Module"
  }
  ```
- **Response (200 OK)**:
  ```json
  {"ok": true}
  ```

## Filesystem (Session Workspace)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/session/:id/files` | Recursive file tree within session workspace |
| GET | `/session/:id/fs/list` | List directory contents (`?path=...`) |
| GET | `/session/:id/fs/read` | Read file text content (`?path=...`) |
| GET | `/session/:id/fs/raw` | Stream raw binary file (`?path=...`) |
| PUT | `/session/:id/fs/write` | Write file content (`?path=...`) |
| GET | `/session/:id/fs/exists` | Check file existence (`?path=...`) |

*Security*: Absolute paths, null bytes, and path escapes outside the session's workspace root are strictly rejected. File read payloads are capped at 2 MiB.

## Terminal (PTY Sessions)

| Method | Path | Description |
|--------|------|-------------|
| POST | `/terminal/create` | Spawn a pseudo-terminal process: `{"workspace": "...", "cols": 80, "rows": 24}` |
| DELETE | `/terminal/:id` | Terminate active terminal process |
| POST | `/terminal/:id/input` | Send raw keystrokes/input to terminal: `{"data": "..."}` |
| POST | `/terminal/:id/resize` | Resize terminal PTY window: `{"cols": 120, "rows": 36}` |
| GET | `/terminal/:id/stream` | EventStream/WebSocket raw output stream |

## Study

| Method | Path | Description |
|--------|------|-------------|
| GET | `/study/courses` | List ingested study courses |
| POST | `/study/courses/ingest` | Ingest markdown curriculum vault from root path |
| GET | `/study/courses/:id/chapters` | List chapters for course |
| GET | `/study/courses/:id/next` | Determine next recommended study chapter |
| POST | `/study/chapters/:id/prepare` | Prepare chapter quiz questions |
| GET | `/study/chapters/:id/quiz` | Fetch active chapter quiz |
| POST | `/study/quiz/submit` | Submit question attempt and record score |
