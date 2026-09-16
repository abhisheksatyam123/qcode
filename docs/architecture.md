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
    session/                session/       # session_store, study, system_prompt, git_workspace
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
- **`study_*`**: Curriculum courses, chapters, topics, questions, and attempt history.

### 3. Comparison with Upstream OpenCode (`anomalyco/opencode`)
- **Storage Strategy**:
  - OpenCode uses a **filesystem JSON tree** (`storage/project/*.json`, `storage/session/*/*.json`, `storage/message/*/*.json`, `storage/part/*/*.json`).
  - QCode replaces the filesystem JSON tree with a unified **relational SQLite database**. This eliminates inode exhaustion, avoids slow recursive directory walks (`fs.glob`), and prevents corrupted state from interrupted file writes.
- **Configuration Compatibility**:
  - QCode directly reads `~/.config/opencode/opencode.json` (or `$OPENCODE_CONFIG`) for provider API keys, model endpoints, and options.
  - Falls back to `$OPENCODE_DB_PATH` if `$QCODE_DB_PATH` is not explicitly set.

## Subagent & Task Architecture

QCode supports concurrent subagent delegation via `TaskTool`:
1. **Live Registry**: Active in-flight subagents are registered in `TaskTool::active_tasks()` and visible via `GET /tasks`.
2. **Durable Subagent Sessions**: Child sessions receive a unique ID (`ses_...`) and persist with `parent_session_id` pointing to the orchestrator session.
3. **Cascade Deletion**: When an orchestrator session is deleted, all associated child subagent sessions are recursively deleted.
