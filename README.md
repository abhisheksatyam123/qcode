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

Model behaviour lives in the same file, not in code. Per model, or once under
`model_defaults` at the top level (every provider) or per provider. Defaults
merge into each model with JSON merge-patch rules: top level, then provider,
then the model; model keys win and `null` removes an inherited key:

```jsonc
{
  // Every provider's models: default request size (capped by limit.output).
  "model_defaults": { "max_tokens": 32000 },
  "provider": {
    "anthropic": {
      "model_defaults": {
        // Claude wire form: "adaptive" = thinking{adaptive,display} + output_config.effort,
        // "enabled" (or unset) = thinking{enabled,budget_tokens}. allow_off=false hides Off.
        "thinking": { "type": "adaptive", "display": "summarized", "allow_off": false },
        // /variant picker, in order. Each key is the id; effort defaults to the id.
        "variants": {
          "low": {}, "medium": {}, "high": { "max_tokens": 32000 },
          "xhigh": { "max_tokens": 64000 }, "max": { "max_tokens": 64000 },
          "ultra": { "label": "Ultra", "effort": "max", "max_tokens": 128000,
                     "prompt": "Extra system instruction while ultra is active",
                     "description": "Max effort + 128k output" }
        }
      },
      "models": {
        "claude-opus-5-5": {
          "reasoning_default": "high",
          "limit": { "context": 1000000, "output": 128000 },
          // USD per 1M tokens; the Stats tab and header cost use these only.
          "cost": { "input": 4, "output": 20, "cache_read": 0.2, "cache_write": 5 }
        }
      }
    }
  }
}
```

Variant fields: `effort` (wire value), `max_tokens` (raises the request,
capped by `limit.output`), `budget_tokens` (budget-form models), `prompt`,
`label`, `description`, `disabled`. A plain `"reasoning_efforts": [...]` list
still works for models without a `variants` object.

Sampling: a model's `"temperature"` (a number, or `false` to send none) and
`"top_p"` (also accepted under its `"options"`) override the built-in model
family defaults, which apply only when the config is silent.

Limits and request size:

- `limit.context` is the only source of a model's context window. Models
  without one show no context percentage and skip automatic compaction; the
  window is never guessed from the model name.
- `max_tokens` is the default request size, capped by `limit.output`. Without
  it the request asks for `limit.output`, and with neither the provider's own
  default applies. There is no built-in cap. Subagents use the same budget;
  a model missing from the catalog uses the lead model's budget.

Session titles: like opencode, a new session is named from its first prompt
by a small model: `"small_model": "opencode/ling-3.1-flash-free"` at the top
level of opencode.json (any `provider/model`), else the session's own model.
`/rename` or a title given to `/new` is never replaced.

Subagents: two kinds of agent, the orchestrator and its subagents, and one
`task` tool for both. The orchestrator sees every opencode.json model in its
prompt and picks one per task (omit `model` for its own). Subagents can list
the team, message the lead or a sister, and start sisters; messages and
background reports are delivered before the recipient's next step.

Automatic compaction: like Claude Code, a session compacts itself during the
tool loop once the next request would carry `compaction.threshold_tokens` of
context. The loop summarizes with the exact request it was about to send plus
the compaction directive (so the summary is a cheap prompt-cache read), writes
`<workspace>/scratchpad/handoff-<session>.md`, replaces the conversation with
one message (summary, the workspace task file, "continue without asking") and
keeps working. The session rows keep the full transcript; turns read history
from the latest summary on.

```json
{"model_defaults": {"compaction": {"auto": true, "threshold_tokens": 250000}}}
```

The threshold is capped at the context window minus the reply budget (10-25%
of the window): with 250000, 1M-window models compact at 250k, 262k-window
models at about 230k and 200k-window models at 168k. Without
`threshold_tokens` the cap is used; `"auto": false` turns it off, and then the
TUI prunes old tool output above 85% of the window instead. `/compact` still
compacts on demand.

Gemini (Antigravity / `protocol: google`): the variant's wire `effort` is sent
as `thinkingConfig.thinkingLevel` unchanged, `budget_tokens` as
`thinkingBudget`, and `thinking.display: "omitted"` turns thought text off
(`includeThoughts: false`). `thinking.type: "disabled"` sends no thinking
config. For example `"max": {"effort": "high", "budget_tokens": 24576}`.
The output budget (`max_tokens` / `limit.output`) is sent as
`maxOutputTokens`, and usage counts thought tokens as output, as Google bills.

Config changes: the TUI re-reads opencode.json with `/reload` (models, prices,
variants, limits; a running turn keeps the config it started with). The server
reads it at startup, so restart `qcode-server` to apply edits. Values of the
wrong type are ignored with a warning naming their path (TUI toast + log).

Cost: each model call is priced when it runs, at the serving model's `cost`
(cache reads/writes without a price use `cost.input`). The session total in the
TUI header, the Stats tab and the server's `GET /session/<id>/stats` (`usage`)
is the sum of those calls, with a per-model split, so switching models or
editing prices later never reprices earlier calls. Calls on models without a
`cost` are counted but not costed; an explicit all-zero `cost` marks a free
model, whose calls count as $0.

### Start the TUI

```bash
./build/host-debug/apps/tui/qcode-tui
```

Logs go to `$QCODE_LOG_DIR/qcode-tui-<session>.log` (default `/tmp/qcode-logs/`); lines from the UI thread go to `qcode-tui.log`.

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

Without `--session <id>` the CLI creates a session (workspace: `--workspace <dir>`, default
the current directory) and prints its id on stderr for follow-up prompts.

Server logs go to `/tmp/qcode-server.log`.

For presets, tests, Android, and compile commands, see [docs/building.md](docs/building.md). Layout of the tree is in [docs/architecture.md](docs/architecture.md).
