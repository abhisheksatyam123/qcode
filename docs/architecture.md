# Architecture & Directory Structure

## Overview
`qcode` is a high-performance C++20 LLM workspace and terminal tool matching modern OpenCode/Cursor paradigms.

Public headers and implementations live in the **same named modules**. The CMake dialect is C++20 (`CMAKE_CXX_STANDARD 20`); stay on headers + CMake, not `import` modules (Android NDK still needs the `qcode::compat::jthread` polyfill).

## Repository Layout
- `libqcode/`: Core libraries `qcode-engine` (headless) and `qcode-ui` (FTXUI render + commands).
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
    core/                   core/          # types, bus, http, uuid, ssl
    config/                 config/        # opencode.json, ProviderInfo, ModelInfo
    providers/              providers/     # openai/anthropic/cursor/antigravity/zen/registry
    transform/              transform/     # wire transforms + reasoning variants
    generation/             generation/    # generation_service / controller / continue
    session/                session/       # sqlite, study, system prompt, workspace
    tools/                  tools/         # bash/task/catalog/executor
    ui/                     ui/            # commands, app_store, markdown, render
```

Include examples:

- `#include <qcode/config/config.h>` — load `~/.config/opencode/opencode.json`
- `#include <qcode/config/provider_info.h>` — `ModelInfo` / `ProviderInfo`
- `#include <qcode/transform/provider_transform.h>` — effort clamp/cycle, wire options
- `#include <qcode/generation/generation_service.h>` — LLM turn
- `#include <qcode/ui/chat_state.h>` — TUI `ChatState`
- `#include <qcode/ui/commands.h>` — slash commands and pickers

`qcode-engine` compiles core, config, providers, transform, generation, session, and tools.
`qcode-ui` compiles `src/ui/` and links FTXUI.

Catalog (providers, models, protocols, reasoning efforts) comes from `opencode.json`, not hardcoded C++ tables.
