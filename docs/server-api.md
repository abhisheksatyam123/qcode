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
| POST | `/api/models/feedback` | body: `{model_id, rating}` (`up`/`down`) |

## Sessions / generation

| Method | Path | Notes |
|--------|------|--------|
| POST | `/generate` | body must include `session_id` and `text` (legacy) |
| POST | `/session/:id/generate` | NDJSON stream of bus events |
| GET | `/sessions` | list parent sessions; `?include_subagents=1` includes child rows; `&parent_session_id=<id>` scopes subagents to a parent |
| GET | `/tasks` | live `TaskTool` subagent registry (`metadata.tasks`); `?parent_session_id=<id>` scopes to a parent session |
| POST | `/sessions` | create; requires `provider`, `model` |
| POST | `/rename` | `{session_id, title}` |
| POST | `/session/cancel` | `{session_id}` |
| GET | `/session/last` | last active session + messages |
| GET | `/session/:id` | session metadata (includes subagent rows, `agent_mode`, `reasoning_mode`) |
| POST | `/session/:id/mode` | `{agent_mode}` = `plan` or `orchestrator` |
| DELETE | `/session/:id` | delete session |
| POST | `/session/:id/clear` | truncate messages |
| GET | `/session/:id/messages` | history |
| POST | `/session/:id/compact` | LLM compaction; optional `{keep}` |
| GET | `/session/:id/stats` | aggregate stats |

## Filesystem (session workspace)

| Method | Path |
|--------|------|
| GET | `/session/:id/files` |
| GET | `/session/:id/fs/list` |
| GET | `/session/:id/fs/read` |
| GET | `/session/:id/fs/raw` |
| PUT | `/session/:id/fs/write` |
| GET | `/session/:id/fs/exists` |

Absolute paths and workspace escapes are rejected. Read payload is capped at 2 MiB.

## Terminal

| Method | Path |
|--------|------|
| POST | `/terminal/create` |
| DELETE | `/terminal/:id` |
| POST | `/terminal/:id/input` |
| POST | `/terminal/:id/resize` |
| GET | `/terminal/:id/stream` |

## Study

| Method | Path |
|--------|------|
| GET | `/study/courses` |
| POST | `/study/courses/ingest` |
| GET | `/study/courses/:id/chapters` |
| GET | `/study/courses/:id/next` |
| POST | `/study/chapters/:id/prepare` |
| GET | `/study/chapters/:id/quiz` |
| POST | `/study/quiz/submit` |
