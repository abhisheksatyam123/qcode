# Architecture & Directory Structure

## Overview
`qcode` is a high-performance C++20 LLM workspace and terminal tool matching modern OpenCode/Cursor paradigms.

Public headers and implementations live in the **same named folders**. On host builds with CMake 3.28+, those folders are also C++20 **named modules**:

```cpp
import qcode.config;
import qcode.transform;
import qcode.generation;
import qcode;  // re-exports the three above
```

Android NDK still uses headers (`#include <qcode/...>`) and the `qcode::compat::jthread` polyfill. Host named modules require Clang 18+ and `clang-scan-deps` (GCC keeps the header surface). Do not use `import std` (C++23) or `std::format` in this layer.

## Repository Layout
- `libqcode/`: Core libraries `qcode-engine` (headless) and `qcode-ui` (FTXUI).
  - `include/qcode/<module>/`: public headers
  - `src/<module>/`: matching implementations
- `apps/`: front-end targets consuming `libqcode`.
  - `apps/tui`: FTXUI terminal UI (`qcode-tui`) — root CMake when `QCODE_BUILD_TUI`
  - `apps/server`: HTTP / WebSocket API (`qcode-server`) — `QCODE_BUILD_SERVER`
  - `apps/cli`: lightweight CLI (`qcode-cli`) — `QCODE_BUILD_CLI`
  - `apps/webui`: Vite / JavaScript client; not a CMake target; copied next to `qcode-server`
  - `apps/android`: Gradle/JNI wrapper; not `add_subdirectory` from root CMake
- `docs/`: architecture, build guides, and [server HTTP API](server-api.md)
- `tests/`: Google Test suite (`unit/`, `integration/`, `utils/`)
- `third_party/`: vendored httplib, nlohmann_json, sqlite, googletest, zlib, brotli, … (FTXUI is fetched via CMake FetchContent, not this tree)
- `scripts/`: `build.py`, `format.py`, `lint.py`, plus Android Python fetch and a model-capabilities helper
- `cmake/`: `find_package` install config template (`qcode-config.cmake.in`)
- `build/`: ignored output root (`build/<preset>/`; see `CMakePresets.json` and `docs/building.md`)
- `test-services/`: optional ClickHouse docker-compose for `BUILD_CLICKHOUSE_TESTS`

## `libqcode` Modules

`include/qcode/` and `src/` use the same folders:

```
libqcode/
  include/qcode/          src/
    core/                   core/          # message, bus_port, http, uuid, ssl
    config/                 config/        # opencode.json, ProviderInfo, ModelInfo
    providers/              providers/     # openai/anthropic/cursor/antigravity/zen/registry
    transform/              transform/     # wire transforms + reasoning variants
    generation/             generation/    # generation_service / controller / continue
    session/                session/       # session_store, system_prompt, git_workspace
    tools/                  tools/         # bash/task/catalog/executor
    ui/                     ui/            # commands, app_store, markdown, message_render
```

Include examples:

- `import qcode.config;` or `#include <qcode/config/config.h>` — load `~/.config/opencode/opencode.json`
- `import qcode.transform;` or `#include <qcode/transform/provider_transform.h>` — effort clamp/cycle, wire options
- `import qcode.generation;` or `#include <qcode/generation/generation_service.h>` — LLM turn
- `#include <qcode/config/provider_info.h>` — `ModelInfo` / `ProviderInfo`
- `#include <qcode/ui/chat_state.h>` — TUI `ChatState`
- `#include <qcode/ui/commands.h>` — slash commands and pickers

`qcode-engine` compiles core, config, providers, transform, generation, session, and tools.
`qcode-ui` compiles `src/ui/` and links FTXUI.

Catalog (providers, models, protocols, reasoning efforts) comes from `opencode.json`, not hardcoded C++ tables.

## Session & Storage Architecture

### 1. Storage Engine
`qcode` uses an embedded relational **SQLite 3** database located at `~/.qcode.db` (overrideable with `$QCODE_DB_PATH` or `$OPENCODE_DB_PATH`).

Key database characteristics:
- **Write-Ahead Logging (WAL)**: `PRAGMA journal_mode=WAL;` enables non-blocking concurrent reads while background threads or tool execution write messages.
- **Synchronous Mode**: `PRAGMA synchronous=NORMAL;` guarantees data integrity while maximizing write throughput.
- **Foreign Key Cascades**: `PRAGMA foreign_keys = ON;` ensures atomic cascade deletions (`ON DELETE CASCADE`) when parent sessions are deleted.
- **Thread Safety**: `SharedDbHandle` provides process-wide serialized database handle locking with a 5000 ms busy timeout (`sqlite3_busy_timeout(db, 5000)`).
- **Schema Migrations**: Safe, idempotent upgrades tracked via `PRAGMA user_version`.

### 2. Core Relational Schema
- **`sessions`**: Primary table for conversation threads.
  - `id` (TEXT PRIMARY KEY): RFC 4122 v4 UUID.
  - `title` (TEXT): Display title for TUI, WebUI, and tabs.
  - `provider` / `model` (TEXT): Model and provider used for the session.
  - `workspace` (TEXT): Absolute path to working directory.
  - `parent_session_id` (TEXT): Links delegated subagent sessions to their parent session.
  - `agent_mode` (TEXT): `orchestrator` or `plan`.
  - `reasoning_mode` (TEXT): `off`, `low`, `medium`, or `high`.
- **`messages`**: Chronological chat history.
  - `id` (INTEGER PRIMARY KEY AUTOINCREMENT).
  - `session_id` (TEXT REFERENCES sessions(id) ON DELETE CASCADE).
  - `sender` (TEXT): `user`, `assistant`, `system`, or `thought`.
  - `content` (TEXT): Message markdown, code blocks, or structured tool invocation/result JSON.
- **`queued_prompts`**: Queued user prompts awaiting completion of active generation turns.
- **`model_capabilities` & `model_runtime_stats`**: Telemetry on context window limits, benchmark scores, turn latencies, and user thumbs-up/down ratings.
- **`study_*`**: legacy tables from the removed study feature (left in place; unused).

### 3. Comparison with Upstream OpenCode (`anomalyco/opencode`)
- **Storage Strategy**:
  - OpenCode uses a **filesystem JSON tree** (`storage/project/*.json`, `storage/session/*/*.json`, `storage/message/*/*.json`, `storage/part/*/*.json`).
  - QCode replaces the filesystem JSON tree with a unified **relational SQLite database**. This eliminates inode exhaustion, avoids slow recursive directory walks (`fs.glob`), and prevents corrupted state from interrupted file writes.
- **Configuration Compatibility**:
  - QCode directly reads `~/.config/opencode/opencode.json` (or `$OPENCODE_CONFIG`) for provider API keys, model endpoints, and options.
  - Falls back to `$OPENCODE_DB_PATH` if `$QCODE_DB_PATH` is not explicitly set.

## Subagent & Task Architecture

There are two kinds of agent: the orchestrator (a top-level session, addressed as `lead`) and its subagents (child sessions, addressed by `task_id`). Both have one tool for working with each other, `task` (`qcode/tools/task_tool.h`); there are no modes, no model router and no sandbox. Like Claude Code agent teams, messages and reports are delivered automatically and agents address each other by id.
1. **Model choice**: the orchestrator's system prompt lists every configured `provider:model` from opencode.json (with prices when configured; `format_provider_catalog_for_prompt`). It picks a model per task: `task { prompt, model: "provider:model" }`; without `model` the subagent runs on the caller's own model. One model per run, no fallback: errors go back to the caller.
2. **Team**: all subagents of an orchestrator are children of its session (`parent_session_id`). A subagent has the same tool: it can list the team (`action: "status"`), message the lead or a running sister, and start or resume a sister (also a child of the lead).
3. **Actions**: `run` (default; blocking, or `background: true`), `status`, `wait` (until a background task finishes or a message arrives), `kill` (background tasks) and `message` (`task_id` = a running subagent or `"lead"`, `prompt` = text).
4. **Inbox**: every session has one (`Team` in `task_tool.cpp`). Background reports go to whoever started the task (or the lead when that subagent has finished); messages go to the recipient. The lead's tool loop and each subagent step (`MultiStepCoordinator`) inject the inbox as user messages before the next request; an idle TUI queues the lead's as a prompt; the server delivers at the next turn.
5. **Durable sessions**: each run is a child session (`ses_...`) with its prompt, tool calls and report; `TaskTool::list_tasks()` (and `GET /tasks`) shows running and finished ones. Deleting the orchestrator session stops and deletes its subagents.
6. **Background tasks** run on their own thread with their own abort flag (the lead's turn ending or Esc does not stop them; `kill` or deleting the session does). A blocking task is part of the caller's turn.
7. **Resume**: `task { task_id, prompt }` continues a finished subagent of the same team with its stored conversation (from its latest compaction summary on), on the model it last used unless `model` is given. A running subagent gets a `message` instead.
8. **Session titles**: the first prompt of a top-level session whose title is still the default (`Session - <model>`) goes to opencode.json `small_model` (`"provider/model"`), else the session's own model, with opencode's title-agent prompt (`qcode/session/session_title.h`). The rename is published as `SessionTitleChanged`.
