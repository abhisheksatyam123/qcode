# Architecture & Directory Structure

## Overview
`qcode` is a high-performance C++ LLM workspace & terminal tool matching modern OpenCode/Cursor paradigms.

## Repository Layout
- `libqcode/`: Core C++ static/shared library (`qcode-core`).
  - `include/qcode/`: Public C++ headers organized by module (`generation`, `providers`, `bus`, `session`, `render`, `tools`, etc.).
  - `src/`: Implementation of `libqcode` components matching header module namespaces.
- `apps/`: Front-end application targets consuming `libqcode`.
  - `apps/tui`: Interactive FTXUI-based Terminal UI client (`qcode-tui`).
  - `apps/server`: Headless HTTP / WebSocket API server serving web clients (`qcode-server`).
  - `apps/webui`: Vite / JavaScript web application frontend.
  - `apps/cli`: Lightweight command-line interface binary (`qcode-cli`).
- `docs/`: System documentation, architectural guides, and build instructions.
- `tests/`: Automated unit and integration test suite using Google Test.
  - `tests/unit/`: Component-level unit tests.
  - `tests/integration/`: Provider and multi-step integration tests.
  - `tests/utils/`: Mock objects and test fixtures.
- `third_party/`: External dependencies (e.g. FTXUI, cpp-httplib, nlohmann_json).
- `examples/`: API use cases and code samples.
- `cmake/`: CMake helper scripts and toolchain configurations.
- `build/`: Ignored unified output root (`build/<preset>/` per platform/mode; see `CMakePresets.json` and `docs/building.md`).
