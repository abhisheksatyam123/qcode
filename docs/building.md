# Building & Development Guide

## Prerequisites
- C++20 compliant compiler (`gcc` 12+, `clang` 14+, or MSVC)
- CMake 3.21 or newer (presets support), Ninja recommended
- Python 3.10+ & [uv](https://docs.astral.sh/uv/) (for build automation)
- Node.js 18+ & npm (for WebUI development & testing)
- For Android: NDK r26+ with `ANDROID_NDK_HOME` set

## Layout: one `build/` root, per-platform subdirs

All CMake outputs live under `build/<preset>/`. Never create sibling `build-*` trees at the repo root.

| Preset | Directory | Description |
|---|---|---|
| `host-debug` | `build/host-debug/` | Default development build with debug symbols |
| `host-debug-asan` | `build/host-debug-asan/` | Debug build with AddressSanitizer and UndefinedBehaviorSanitizer |
| `host-release` | `build/host-release/` | Optimized release build |
| `android-arm64-v8a-debug` | `build/android-arm64-v8a-debug/` | Android NDK cross-compile, debug |
| `android-arm64-v8a-release` | `build/android-arm64-v8a-release/` | Android NDK cross-compile, release |

The Android host-side sysroot (OpenSSL/CURL unpack) lives at `build/android-deps/sysroot-<abi>/`.

## Build Commands

### 1. Scripted Build (Recommended)
Wraps CMake presets and automatically configures Ninja:

```bash
# Debug build (libqcode, TUI, server, CLI) -> build/host-debug/
uv run scripts/build.py --mode debug

# Release build with test suite -> build/host-release/
uv run scripts/build.py --mode release --tests

# Clean rebuild with clangd compile commands export
uv run scripts/build.py --mode debug --tests --clean --export-compile-commands

# Target specific components
uv run scripts/build.py --no-webui --no-cli
```

### 2. CMake Presets (Direct)
```bash
# Configure and build
cmake --preset host-debug
cmake --build --preset host-debug --parallel

# Release build
cmake --preset host-release
cmake --build --preset host-release --parallel
```

### 3. WebUI Build
WebUI sources live in `apps/webui/src/`. The build bundles assets into `apps/webui/dist/` via Vite:
```bash
# Install dependencies
cd apps/webui && npm install

# Production build
npm run build --prefix apps/webui

# Development server with hot reload
npm run dev --prefix apps/webui
```
*Note*: When `qcode-server` builds, it automatically mirrors WebUI assets to `<TARGET_DIR>/webui/` for immediate hosting at `http://localhost:9080`.

### 4. Android Build
Requires `ANDROID_NDK_HOME`:
```bash
# Build native C++ JNI libraries
uv run scripts/build.py --platform android --mode release

# Vendor portable Python into Android assets if needed
python3 scripts/fetch_android_python.py
```

## Running Tests

### C++ Test Suite (Google Test)
The test binary `qcode_tests` is built under `build/<preset>/tests/qcode_tests`:

```bash
# Run all tests
./build/host-debug/tests/qcode_tests

# Run filtered tests (e.g. Server routes)
./build/host-debug/tests/qcode_tests --gtest_filter="ServerRoutesTest.*"

# Run session store tests
./build/host-debug/tests/qcode_tests --gtest_filter="SessionStoreTest.*"

# Using ctest
ctest --test-dir build/host-debug --output-on-failure
```

### WebUI Test Suite (Node.js Test Runner)
32 comprehensive unit and integration tests covering utilities, DOM rendering, markdown formatting, endpoint conformance, and accessibility:

```bash
npm test --prefix apps/webui
```

## IDE & Language Server Support
Export compile commands for `clangd` / `clang-tidy`:
```bash
uv run scripts/build.py --export-compile-commands
```
This generates and links `compile_commands.json` at the project root.
