#!/usr/bin/env bash
# Cloud Agent install phase for qcode.
#
# Idempotent: installs system toolchain packages, uv, git submodules, a default
# opencode.json (only if absent), then builds the C++ apps + tests and the WebUI.
# Safe to run repeatedly and against cached/partially-prepared state.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# --- System packages -------------------------------------------------------
# The default image ships clang-18, gcc-13, CMake 3.28 and Node 22. These add
# the pieces qcode needs: Ninja (required for C++20 named modules), OpenSSL and
# libcurl headers, clang tooling (format/tidy/scan-deps), the libstdc++ dev
# headers matching the GCC toolchain clang-18 selects, and clang's profile
# runtime (a zlib test target links it under -coverage).
APT_PACKAGES=(
  ninja-build
  clang-format
  clang-tidy
  clang-tools-18
  libclang-rt-18-dev
  libstdc++-14-dev
  openssl
  libssl-dev
  libcurl4-openssl-dev
  ccache
  pkg-config
)
echo "==> Installing system packages"
sudo apt-get update -y
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${APT_PACKAGES[@]}"

# --- uv (runs the helper build scripts) ------------------------------------
export PATH="$HOME/.local/bin:$PATH"
if ! command -v uv >/dev/null 2>&1; then
  echo "==> Installing uv"
  curl -LsSf https://astral.sh/uv/install.sh | sh
fi
export PATH="$HOME/.local/bin:$PATH"

# --- Git submodules (zlib, brotli, googletest, clickhouse-cpp) -------------
echo "==> Syncing git submodules"
git submodule update --init --recursive

# --- Default provider config (only created if missing) ---------------------
# The TUI/server refuse to start without at least one provider. This template
# covers the providers qcode knows about (openai, anthropic, opencode,
# openrouter, antigravity) so the standard ~/.config/opencode/opencode.json is
# complete — the tui_config HomeConfigDrivesProvidersModelsAndEfforts test
# expects those provider ids whenever a home config exists. Keys are read from
# the matching *_API_KEY environment variables (add them as Cloud Agent secrets
# to enable live model calls); the server still starts and serves the Web UI
# without them.
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/opencode"
CONFIG_FILE="$CONFIG_DIR/opencode.json"
if [ ! -f "$CONFIG_FILE" ]; then
  echo "==> Writing default opencode.json to $CONFIG_FILE"
  mkdir -p "$CONFIG_DIR"
  cat > "$CONFIG_FILE" <<'JSON'
{
  "provider": {
    "openai": {
      "name": "OpenAI",
      "npm": "@ai-sdk/openai",
      "options": {
        "baseURL": "https://api.openai.com/v1",
        "apiKey": "{env:OPENAI_API_KEY}"
      },
      "models": {
        "gpt-4o-mini": {
          "name": "GPT-4o mini",
          "tool_call": true,
          "limit": {"context": 128000, "output": 16384}
        }
      }
    },
    "anthropic": {
      "name": "Anthropic",
      "options": {
        "baseURL": "https://api.anthropic.com/v1",
        "apiKey": "{env:ANTHROPIC_API_KEY}"
      },
      "models": {
        "claude-3-5-haiku-latest": {
          "name": "Claude 3.5 Haiku",
          "tool_call": true,
          "limit": {"context": 200000, "output": 8192}
        }
      }
    },
    "opencode": {
      "name": "OpenCode Zen",
      "options": {
        "apiKey": "{env:OPENCODE_API_KEY}"
      },
      "models": {}
    },
    "openrouter": {
      "name": "OpenRouter",
      "options": {
        "apiKey": "{env:OPENROUTER_API_KEY}"
      },
      "models": {}
    },
    "antigravity": {
      "name": "Antigravity",
      "options": {
        "apiKey": "{env:ANTIGRAVITY_API_KEY}"
      },
      "models": {}
    }
  }
}
JSON
fi

# --- Build (Debug + tests + WebUI) -----------------------------------------
# clang-18 enables the C++20 named-module path; Ninja is required for modules.
echo "==> Building qcode (debug, with tests)"
export CC=clang-18
export CXX=clang++-18
export CMAKE_GENERATOR=Ninja
uv run scripts/build.py --mode debug --tests --export-compile-commands

echo "==> Install complete"
