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
- `examples/`: API samples (link `qcode::engine`)
- `scripts/`: `build.py`, `format.py`, `lint.py`, plus Android Python fetch and a model-capabilities helper
- `cmake/`: `find_package` install config template (`qcode-config.cmake.in`)
- `build/`: ignored output root (`build/<preset>/`; see `CMakePresets.json` and `docs/building.md`)
- `test-services/`: optional ClickHouse docker-compose for `BUILD_CLICKHOUSE_TESTS`


## `libqcode` modules

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
