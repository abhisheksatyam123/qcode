# Contributing to qcode

Thank you for your interest in contributing to `qcode`!

## Development Workflow

### Prerequisites
- C++17 compliant compiler (`gcc` 9+, `clang` 10+, or MSVC)
- CMake 3.16+
- Python 3 / `uv` for helper scripts

### Building the Project
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build . -j$(nproc)
```

### Running Tests
```bash
cd build
ctest --output-on-failure
```

### Formatting & Linting
We enforce consistent formatting using `clang-format`. You can check or apply formatting using the helper script:
```bash
python3 scripts/format.py
python3 scripts/lint.py
```

## Pull Request Guidelines
1. Ensure all existing tests pass (`ctest --output-on-failure`).
2. Add new unit tests for any new features or bug fixes.
3. Keep PRs focused on a single topic or fix.
