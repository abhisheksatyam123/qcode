# qcode

C++20 toolkit for AI-powered coding agents. Shared engine library plus TUI, headless server, web UI, CLI, and Android JNI — providers come from `opencode.json` (OpenAI, Anthropic, Cursor, Antigravity, and others).

## Installation

You will need:

- A C++20 compatible compiler (`gcc` 12+, `clang` 14+, or MSVC)
- CMake 3.16+ (3.21+ for CMake presets)
- [uv](https://docs.astral.sh/uv/) (for the build script)
- OpenSSL development headers
- Ninja (used by the build script)

Clone with submodules:

```bash
git clone --recursive <repo-url>
cd qcode
```

## Building & Running

### Build

```bash
# Debug build (default) — libqcode, TUI, server, CLI → build/host-debug/
uv run scripts/build.py

# Release build → build/host-release/
uv run scripts/build.py --mode release
```

Or with CMake presets:

```bash
cmake --preset host-debug
cmake --build --preset host-debug --parallel --target qcode-tui
```

Server and CLI are on by default (`QCODE_BUILD_SERVER` / `QCODE_BUILD_CLI`). Disable them if you only want the TUI:

```bash
cmake -B build/host-debug -G Ninja -DQCODE_BUILD_SERVER=OFF -DQCODE_BUILD_CLI=OFF
cmake --build build/host-debug --parallel --target qcode-tui
```

Binaries (debug preset):

| Target | Path |
|--------|------|
| TUI | `build/host-debug/apps/tui/qcode-tui` |
| Server | `build/host-debug/apps/server/qcode-server` |
| CLI client | `build/host-debug/apps/cli/qcode-cli` |

`apps/webui` (Vite) is served by `qcode-server`. `apps/android` is a separate Gradle/JNI project; see [docs/building.md](docs/building.md).

Headless code should link `qcode::engine`. The TUI links `qcode::ui` (FTXUI).

### Configuration

TUI and server load providers from a single `opencode.json`:

- `$OPENCODE_CONFIG` if set
- otherwise `~/.config/opencode/opencode.json` (`$XDG_CONFIG_HOME/opencode/opencode.json` when XDG is set)

Set provider API keys via environment variables (e.g. `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`) or in that file’s provider options.

### Start the TUI

```bash
./build/host-debug/apps/tui/qcode-tui
```

Logs go to `/tmp/qcode.log`.

### Start the server

```bash
# Default port 9080
./build/host-debug/apps/server/qcode-server

# Custom port
./build/host-debug/apps/server/qcode-server --port 9080
```

Then open `http://localhost:9080` for the Web UI, or use the CLI client:

```bash
./build/host-debug/apps/cli/qcode-cli --prompt "Hello"
```

Server logs go to `/tmp/qcode-server.log`.

For presets, tests, Android, and compile commands, see [docs/building.md](docs/building.md). Layout of the tree is in [docs/architecture.md](docs/architecture.md).
