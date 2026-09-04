# Building & Development Guide

## Prerequisites
- C++20 compliant compiler (`gcc` 12+, `clang` 14+, or MSVC)
- CMake 3.21 or newer (presets), Ninja recommended
- Node.js & npm (for WebUI development)
- For Android: NDK r26+ with `ANDROID_NDK_HOME` set

## Layout: one `build/` root, per-platform subdirs

All CMake outputs live under `build/<preset>/`. Never create sibling
`build-*` trees at repo root (they are git-ignored legacy paths).

| Preset | Directory | Use |
|---|---|---|
| `host-debug` | `build/host-debug/` | default dev loop |
| `host-release` | `build/host-release/` | local release / CI |
| `android-arm64-v8a-debug` | `build/android-arm64-v8a-debug/` | NDK cross, debug |
| `android-arm64-v8a-release` | `build/android-arm64-v8a-release/` | NDK cross, release |

Android host-side sysroot (OpenSSL/CURL unpack) lives at
`build/android-deps/sysroot-<abi>/` (the JNI build falls back to the
legacy `build-android-deps/` path if present).

## Build commands

```bash
# Recommended: scripted build (wraps preset layout)
uv run scripts/build.py --mode debug                    # -> build/host-debug/
uv run scripts/build.py --mode release --tests          # -> build/host-release/
uv run scripts/build.py --mode debug --tests --clean --export-compile-commands

# Android cross (requires ANDROID_NDK_HOME)
uv run scripts/build.py --platform android --mode release

# Raw CMake presets (equivalent)
cmake --preset host-debug && cmake --build --preset host-debug
cmake --preset host-release && cmake --build --preset host-release

# Run tests
ctest --preset host-debug --output-on-failure
```

`--export-compile-commands` copies `build/<preset>/compile_commands.json`
to the repo root for clangd/clang-tidy.

## Note on out-of-source builds
Always configure via a preset (or `scripts/build.py`). This keeps the
source tree clean: the only ignored outputs are `build/`,
`compile_commands.json` (root copy), Gradle/`.cxx` trees, and
`apps/webui/{dist,node_modules}`.
