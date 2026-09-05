# Contributing to qcode

Thank you for your interest in contributing to `qcode`!

## Development Workflow

### Prerequisites
- C++20 compiler (`gcc` 12+, `clang` 14+, or MSVC)
- CMake 3.21+ (presets), Ninja recommended
- Python 3 / `uv` for helper scripts

### Building the Project

```bash
uv run scripts/build.py --mode debug --tests
```

Or with CMake presets:

```bash
cmake --preset host-debug
cmake --build --preset host-debug --parallel
```

See [docs/building.md](docs/building.md) for release/Android presets and compile-commands export.

### Running Tests

```bash
ctest --preset host-debug --output-on-failure
```

Tests require configuring with `-DBUILD_TESTS=ON` (included when using `scripts/build.py --tests`).

### Formatting & Linting

```bash
python3 scripts/format.py
python3 scripts/lint.py
```

## Pull Request Guidelines
1. Ensure all existing tests pass (`ctest --output-on-failure`).
2. Add new unit tests for any new features or bug fixes.
3. Keep PRs focused on a single topic or fix.
4. Headless targets and examples should link `qcode::engine`; TUI code links `qcode::ui`.
