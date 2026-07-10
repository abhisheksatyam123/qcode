# Legacy root todo files (consolidated)

These two files lived at the repo root (`todo.md`, `.todo.md`). They are historical
working notes from earlier TUI sessions and are superseded by `workspace-cleanup-todo.md`.
Preserved here so the history is not lost.

---

## todo.md (root)

## Systems

Project: **AI SDK CPP** (C++20, CMake) — a unified C++ toolkit for multiple LLM providers
(OpenAI, Anthropic, openrouter, qpilot, antigravity, opencode) plus a bus-driven TUI
(`qcode-tui`) in src/tui. Logs go to /tmp/qcode.log (level DEBUG).

Goal of this task: ensure the runtime logs **capture the scenarios** (queue lifecycle,
generation duration, status transitions, errors) that are useful for improving the TUI.

Log coverage finding: provider/client layer (chat_bus.cpp, base_provider_client.cpp,
http) is well logged, but the TUI orchestration layer was silent — store.cpp had all
LOG_* stripped, main.cpp only logged submit length. Queue/status/error scenarios were
uncaptured. An existing-log scenario worth improving: a generation returned
`gemini-2.5-flash` with `Extracted message content - length: 0` / `final assistant_text
empty=true` (empty-response scenario).

## Tasks

[x] View improvements: prompt queue (drain), header queue/error chips, Stats tool stats
[x] Add logging at key decision points so scenarios are captured:
    - main.cpp: prompt queued / appended (INFO); spawn_generation start (provider/model/
      prompt_len/queue_remaining); generation complete duration_ms; queue drain (INFO)
    - store.cpp: enqueue/dequeue/append (DEBUG); set_status (DEBUG); set_error (ERROR)
[ ] (Candidate) Handle empty assistant responses in the view (logs already capture the case)

## Verification
- `cmake --build build --target qcode-tui` -> clean build.
- Interactive: run ./build/src/tui/qcode-tui, submit prompts (queue some while generating),
  trigger an error; /tmp/qcode.log now records the full queue + generation + error flow.

---

## .todo.md (root)

## Changes Made

### Streaming & Spinner
- **Animated spinner**: Braille chars (⠋⠙⠹⠸⠼⠴⠦⠧⠏) cycle at 120ms via background thread while generating
- **Throttled streaming**: Text deltas batched and flushed at max ~30fps instead of per-event (prevents UI flooding)
- **Tool mode streaming**: `on_step_finish` callback progressively builds assistant text as each step completes — text appears during tool execution instead of all at once at the end

### Multi-Core Parallelism
- **Parallel tool execution enabled** (`base_provider_client.cpp`: `parallel=false` → `true`)
- **Fixed use-after-free bug** in parallel path: `ToolExecutor` now captures `tool_call` by VALUE (was dangling ref)
- **Callbacks in parallel path**: `on_tool_call_start`/`finish` now fire even when tools run in parallel
- **Thread-safe step counter**: Changed `shared_ptr<int>` to `shared_ptr<atomic<int>>` for parallel safety

### Thinking Tokens
- Assistant rendering parses `<thinking>...</thinking>` blocks from model output
- Thinking content shown in dimmed gray with a "✦ Thinking..." header
- Non-thinking text rendered normally
- Falls back to plain text if no thinking tags present

### Other
- Auto-scroll follows new content; PageUp/WheelUp disables it, End key resets
- Clean build, zero errors

## Files Modified
- `src/tui/views.cpp` — thinking token rendering, spinner, auto-scroll
- `src/tui/chat.cpp` — throttled streaming, tool-mode step streaming, atomic counter
- `src/tui/main.cpp` — spinner thread, auto_scroll on submit, End key
- `include/ai/tui/state.h` — auto_scroll, generation_frame
- `src/tools/tool_executor.cpp` — parallel path fix (value capture + callbacks)
- `src/providers/base_provider_client.cpp` — parallel=true
