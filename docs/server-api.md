# qcode-server HTTP API

Unversioned JSON API served by `qcode-server` (default port 9080). WebUI static files are mounted at `/` when present:
`vendor-*` files are sent with `Cache-Control: public, max-age=86400` (one day; the names carry no version), all others
(`index.html`, `app.js`, `style.css`, ...) with `Cache-Control: no-cache`.

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
| GET | `/session/last` | Last active session metadata and message history; `?messages=0` omits `messages` |
| GET | `/session/:id` | Session metadata (includes subagent rows, `agent_mode`, `reasoning_mode`, `parent_session_id`) plus `generating`, `turn`, `message_count`, `updated_at`; no history, cheap to poll |
| POST | `/rename` | Rename session: `{"session_id": "...", "title": "..."}` |
| POST | `/session/cancel` | Stop the running turn: `{"session_id": "..."}`; also `POST /session/:id/cancel` (alias `/abort`) |
| POST | `/session/:id/mode` | Set agent mode: `{"agent_mode": "plan"|"orchestrator"}` |
| DELETE | `/session/:id` | Permanently delete session and cascade delete all messages |
| POST | `/session/:id/clear` | Truncate message history for a session |
| GET | `/session/:id/messages` | List historical messages for session |
| POST | `/session/:id/compact` | Summarize/compact session context with LLM (the session's provider/model, resolved as for generate); optional `{"keep": N}`. See [Compaction](#compaction) |
| GET | `/session/:id/stats` | Session statistics read from persisted rows (a running turn's tool calls are saved as they happen). See [Session stats](#session-stats) |
| GET | `/session/:id/logs` | Retrieve isolated session logs; optional `?raw=1` or `?lines=N` |
| GET | `/logs` | Retrieve server unscoped log; optional `?raw=1` or `?lines=N` |
| POST | `/session/:id/generate` | NDJSON stream of generation bus events (tokens, tool calls, results); see [Generation turns](#generation-turns) |
| DELETE | `/session/:id/queue` | Withdraw prompts the running turn has not taken yet: `{"text": "..."}` drops that prompt, empty body drops all |
| POST | `/generate` | Legacy single-turn generate: `{"session_id": "...", "text": "..."}` |

### Session stats

`GET /session/:id/stats` returns the session row counts (`message_count`,
`user_messages`, `assistant_messages`, `tool_calls`, `prompt_tokens`,
`completion_tokens`, `total_tokens`, `total_tool_time_ms`) plus:

- `usage`: every model call of the session (turn steps, compaction, step-cap
  summaries; subagent calls count in their child session). Totals
  `model_calls`, `input_tokens` (whole prompts incl. cache), `cache_read_tokens`,
  `cache_write_tokens`, `uncached_input_tokens`, `output_tokens`,
  `reasoning_tokens`, `cache_hit_pct`; latency `avg_call_ms`, `avg_ttft_ms`,
  `output_tok_per_s`, `model_ms_max`/`_last`; `last_input_tokens`,
  `last_effort`, `last_variant`; `cost` (`input`, `cache_read`, `cache_write`,
  `output`, USD), `priced_calls`, `unpriced_calls`, `legacy_calls`; `by_model`
  keyed `"provider/model"` with the same token and cost fields; and
  `session_cost` {`total`, `available`, `estimated`, `unpriced_calls`,
  `legacy_calls`, parts}. Each call is priced when it runs at its model's
  `cost` from opencode.json; an all-zero `cost` is a free model.
- `model_info`: the session model's config (`context_window`, `output_limit`,
  `max_tokens`, `cost`, `thinking`, `reasoning_default`, `variants`), or `null`.
- `context`: `{"used": <last prompt tokens>, "window": <limit.context, 0 = unknown>}`.

### Compaction

`POST /session/:id/compact` replays the conversation as the next turn would send it
(history after the latest summary, the turn's system prompt, tool schemas, output budget
and the session variant's thinking settings) with the compaction directive as the final
user message, so the summarizer reuses the provider's prompt cache of the last turn. The
variant is the session's `reasoning_mode`, stored by every generate call (override with
`{"reasoning_mode": ...}`). The summary replaces the history with a handoff note plus the
last `keep` messages and is written to `<workspace>/scratchpad/handoff-<id>.md`; the call
is billed in the session's `usage`.

### Generation turns

A turn runs on the server and persists itself (user prompt, reasoning, tool calls/results,
assistant reply) whether or not an HTTP client stays attached; the NDJSON stream only
mirrors it. Every stream line carries `"turn": <id>`; ids are unique across sessions and
restarts, so a client drops lines whose `turn` is not the one it follows (e.g. late events
of a turn it stopped).

- `POST /session/:id/generate {"text": ...}` when idle starts a turn. The stream opens with
  `{"type":"session.started","turn":N}` and ends with `generation.complete`
  (`error` set when the turn failed).
- `provider` / `model` (id or name) default to the session's stored ones, then to the first
  configured provider. `agent_mode` and `reasoning_mode` (default `"off"`) are stored on
  the session (`GET /session/:id`) and reused by `/compact`. A provider that is given or stored but not configured is rejected with
  **400** `{"error":"provider '<p>' is not configured","providers":["<id>",...]}` (also for
  `/compact`); an unknown model falls back to the provider's first model (logged).
- The same call while a turn runs returns **202** `{"queued":true,"turn":N}`: the running
  turn takes the prompt at its next model request and emits
  `{"type":"backend.user.message.injected","text":...}` on its stream (the reply so far
  and the prompt are persisted at that point). Attachments are rejected with **409**.
  A turn being stopped is awaited first (up to 10 s, then **409**), so a prompt sent right
  after Stop starts a new turn.
- `{"resume": true}` reattaches to the running turn (reload, dropped stream, other tab).
  It opens with `{"type":"session.snapshot","messages":[...],"reasoning_text":...,
  "assistant_text":...}` — the persisted history plus the turn's not-yet-persisted text —
  followed by the turn's later events, with no gap or overlap between the two. When nothing
  runs it returns only `generation.complete`.
- `POST /session/:id/cancel` sets the turn's abort flag, drops its queued prompts and waits
  briefly (≤ 3 s) for it to end; the partial reply is persisted.

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
| GET | `/session/:id/files` | Git changes in the session workspace: `files` (porcelain status + per-file `insertions`/`deletions`), counts, and `diff` against `HEAD`. Runs `git add -N .` so untracked files show in the diff. `diff` is cut at 512 KiB (whole lines); `diff_truncated` is then `true` |
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
| GET | `/terminal/:id/stream` | Poll: the PTY output available since the last call, as `text/plain` (empty when none) |

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

## Vision & Images

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/vision/providers` | Configured vision-capable providers/models (incl. local Ollama) |
| POST | `/api/vision/ocr` | Image → Markdown (OCR/diagram transcription): `{image, mode, provider, model}` |
| POST | `/api/images/generate` | **Generate an image**: `{prompt, provider?, model?, size?, workspace?}` |

### Image attachments on generation

`POST /session/:id/generate` accepts an optional `attachments` array; each image rides
the user message in the provider's own format (OpenAI `image_url` data URL / Responses
`input_image`, Anthropic base64 `image` block, Gemini `inlineData`):

```json
{
  "session_id": "...",
  "text": "Describe this screenshot",
  "attachments": [
    {"path": "docs/diagram.png", "description": "architecture draft"},
    {"mime_type": "image/png", "data": "<base64>"}
  ]
}
```

`path` resolves against the session workspace (or absolute) and loads like the agent's
`image` tool: png/jpeg/webp/gif, max 3.75 MB. Raw `data` requires `mime_type` (`image/*`,
max 10 MiB decoded). Attachments persist in the session history across reloads and
compaction.

### Generate image (`POST /api/images/generate`)

| Provider | Transport | Status (verified through qcode) |
|----------|-----------|--------------------------------|
| `antigravity` | Gemini `generationConfig.responseModalities: ["TEXT","IMAGE"]` via `/v1internal:generateContent` | **200** — `inlineData` when present, otherwise the model's inline `<svg>` text block is extracted as `image/svg+xml` |
| `openrouter` | `POST /api/v1/images/generations` (`response_format: b64_json`) | Provider-dependent: 404 when the model has no images endpoint (`502` relayed with provider error) |
| `opencode` (Zen) | `POST /zen/v1/images/generations` | 404 — no images endpoint (relayed as `502`) |
| `cursor` / others | — | `501 {"image_generation": false}` (text-only transport) |

Response `200`: `{status, provider, model, mime_type, data, data_url, path, text}` —
`data` is base64, `data_url` a ready-to-render data URL, `path` the file written to the
workspace (`generated-<epoch>.<ext>`).
