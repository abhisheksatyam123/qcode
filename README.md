# AI SDK CPP

The AI SDK CPP is a modern C++ toolkit designed to help you build AI-powered applications with popular model providers like OpenAI and Anthropic. It provides a unified, easy-to-use API that abstracts away the complexity of different provider implementations.

## Motivation

C++ developers have long lacked a first-class, convenient way to interact with modern AI services like OpenAI, Anthropic, and others. AI SDK CPP bridges this gap by providing:

- **Unified API**: Work with multiple AI providers through a single, consistent interface
- **Modern C++**: Built with C++20 features for clean, expressive code
- **Minimal Dependencies**: Minimal external dependencies for easy integration

## Installation

You will need:

- A C++20 compatible compiler
- CMake 3.16+
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
# Debug build (default) — builds libqcode and qcode-tui
uv run scripts/build.py

# Release build
uv run scripts/build.py --mode release
```

Or with CMake directly:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel --target qcode-tui
```

Server and CLI are also on by default (`QCODE_BUILD_SERVER` / `QCODE_BUILD_CLI`). Disable them if you only want the TUI:

```bash
cmake -B build -G Ninja -DQCODE_BUILD_SERVER=OFF -DQCODE_BUILD_CLI=OFF
cmake --build build --parallel --target qcode-tui
```

Binaries land at:

| Target | Path |
|--------|------|
| TUI | `build/apps/tui/qcode-tui` |
| Server | `build/apps/server/qcode-server` |
| CLI client | `build/apps/cli/qcode-cli` |

### Configuration

Both the TUI and server load providers from an OpenCode-style config file. Lookup order:

1. `OPENCODE_CONFIG` (if set)
2. `$XDG_CONFIG_HOME/opencode/opencode.json`
3. `~/.config/opencode/opencode.json`
4. `~/notes/etc/opencode.json`

Set provider API keys via environment variables (e.g. `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`) or in the config file’s provider options.

### Start the TUI

```bash
./build/apps/tui/qcode-tui
```

Logs go to `/tmp/qcode.log`.

### Start the server

```bash
# Default port 9080
./build/apps/server/qcode-server

# Custom port
./build/apps/server/qcode-server --port 9080
```

Then open `http://localhost:9080` for the Web UI, or use the CLI client:

```bash
./build/apps/cli/qcode-cli --prompt "Hello"
```

Server logs go to `/tmp/qcode-server.log`.

For more build options (tests, clean builds, compile commands), see [DEVELOPMENT.md](DEVELOPMENT.md).

