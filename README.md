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
# Debug build (default) — builds the SDK, examples, and qcode-tui
uv run scripts/build.py

# Release build
uv run scripts/build.py --mode release
```

The headless server is opt-in. Enable it when configuring CMake, then build:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQCODE_BUILD_SERVER=ON
cmake --build build --parallel
```

Or reconfigure an existing build directory:

```bash
cmake -B build -DQCODE_BUILD_SERVER=ON
uv run scripts/build.py
```

Binaries land at:

| Target | Path |
|--------|------|
| TUI | `build/src/tui/qcode-tui` |
| Server | `build/src/server/qcode-server` |
| CLI client | `build/src/server/qcode-cli` |

### Configuration

Both the TUI and server load providers from an OpenCode-style config file. Lookup order:

1. `OPENCODE_CONFIG` (if set)
2. `$XDG_CONFIG_HOME/opencode/opencode.json`
3. `~/.config/opencode/opencode.json`
4. `~/notes/etc/opencode.json`

Set provider API keys via environment variables (e.g. `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`) or in the config file’s provider options.

### Start the TUI

```bash
./build/src/tui/qcode-tui
```

Logs go to `/tmp/qcode.log`.

### Start the server

```bash
# Default port 9080
./build/src/server/qcode-server

# Custom port
./build/src/server/qcode-server --port 9080
```

Then open `http://localhost:9080` for the Web UI, or use the CLI client:

```bash
./build/src/server/qcode-cli --prompt "Hello"
```

Server logs go to `/tmp/qcode-server.log`.

For more build options (tests, clean builds, compile commands), see [DEVELOPMENT.md](DEVELOPMENT.md).

