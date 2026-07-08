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
