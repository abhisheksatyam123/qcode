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
  - `apps/tui`: FTXUI terminal UI (`qcode-tui`)
  - `apps/server`: HTTP / WebSocket API (`qcode-server`)
  - `apps/webui`: Vite / JavaScript web client
  - `apps/cli`: lightweight CLI (`qcode-cli`)
  - `apps/android`: JNI wrapper
- `docs/`: architecture and build guides
- `tests/`: Google Test suite (`unit/`, `integration/`, `utils/`)
- `third_party/`: FTXUI, cpp-httplib, nlohmann_json, sqlite, ...
- `examples/`: API samples
- `cmake/`: helper scripts and toolchains
- `build/`: ignored output root (`build/<preset>/`; see `CMakePresets.json` and `docs/building.md`)

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
