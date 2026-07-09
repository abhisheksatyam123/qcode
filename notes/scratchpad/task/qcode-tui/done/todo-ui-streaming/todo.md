## STATUS: DONE (committed; see git history).
## Systems
- Goals:
    1. Improve TUI rendering for tool calls, results, system messages, and user/assistant messages to be more Opencode-like.
    2. Implement streaming for assistant responses when tools are enabled.
- Files:
    - src/tui/views.cpp (Rendering)
    - src/tui/tools.cpp (Tool formatting)
    - src/tui/chat.cpp (Streaming orchestration)
- Current State:
    - Tool calls/results use raw text in a basic vbox/hbox block.
    - Streaming is disabled when tools are enabled.

## Tasks
- [x] Investigate current implementation in src/tui/views.cpp and src/tui/tools.cpp.
- [x] Analyze src/tui/chat.cpp to understand why streaming is bypassed when tools are enabled.
- [x] Develop a plan to refactor rendering and streaming.
