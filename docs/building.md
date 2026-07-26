# Building & Development Guide

## Prerequisites
- C++17 compliant compiler (`gcc` 9+, `clang` 10+, or MSVC)
- CMake 3.16 or newer
- Ninja or Make build tool
- Node.js & npm / bun (for WebUI development)

## Build Commands
```bash
# Configure out-of-source build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build all targets (qcode-tui, qcode-server, qcode-cli)
cmake --build . -j$(nproc)

# Run tests
ctest --output-on-failure
```

## Note on Out-of-Source Builds
Always execute CMake from inside the `build/` directory to prevent cluttering the root source tree with build artifacts.
