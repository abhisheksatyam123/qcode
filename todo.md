# QCode UI Improvement — OpenCode-style TUI

## Systems

### Goal
Make QCode TUI look like OpenCode with beautiful tool rendering and clean backend/frontend separation.

### Architecture (Current)
- Bus pattern already exists: `BusPort` → typed events → `AppStore` → FTXUI render
- Backend (`chat_bus.cpp`) has zero FTXUI deps — communicates via bus events only
- Coupling: `run_generation_with_bus()` takes `ChatState&` (only reads `session_id` + `reasoning_mode`)
- Tool rendering is basic: one-liner `▶ tool summary ⠋ calling...` + simple BlockTool

### Key Files
```
src/tui/message_render.cpp  — 378L — render_tool_call/result, BlockTool (MAIN TARGET)
src/tui/tool_renderers.cpp  —  30L — only BashToolRender (expand with per-tool renderers)
src/tui/views.cpp           — 675L — render_view layout
src/tui/chat_bus.cpp        — ~580L — backend (decouple ChatState&)
src/tui/main.cpp            — 554L — monolithic entry
include/ai/tui/views.h      — color palette, render declarations
include/ai/tui/tool_renderers.h — tool renderer declarations
include/ai/tui/message_render.h — BlockTool, render_message decl
include/ai/tui/chat_bus.h   — run_generation_with_bus signature
```

### OpenCode Reference (tool rendering style)
- Collapsible blocks with colored left border (accent color)
- Per-tool icons: `⚡` bash, `📄` file, `🔍` search, `🤖` task
- Status badges: `✓ success` green, `✗ failed` red, `⠋ running` yellow
- Duration inline: `· 142ms`
- Output truncated to ~20 lines with `[+N more lines]`
- Title line: `⚡ Bash · description · 142ms`

## Tasks

### Phase 1: Beautiful Tool Rendering (Largely Complete)
- [x] 1.1 Redesign `BlockTool()` — accent-colored left border, icon, status badge, duration
- [x] 1.2 Add per-tool-type renderers (bash, task, read_file, write_file, search)
- [x] 1.3 Improve `render_tool_call()` — icon per type, better layout
- [x] 1.4 Improve `render_tool_result()` — truncated output, expand hint, better structure
- [x] 1.5 Build and test

### Phase 1b: Club Tool Input+Output & Make Expandable/Collapsible (NEW PRIORITY)
- [ ] 1.6 Add `tool_collapse_state` map to `ChatState` (key: tool_call_id, value: expanded bool)
- [ ] 1.7 Modify `render_message` to pair tool calls with results by `tool_call_id`
- [ ] 1.8 Create `render_tool_block()` that renders combined call+result with expand/collapse
- [ ] 1.9 Add keyboard handler (Enter/Space) and mouse click handler for toggling collapse state
- [ ] 1.10 Visual indicator: ▼/▶ chevron, show input when collapsed, show both when expanded
- [ ] 1.11 Build and test

### Phase 2: Backend Decoupling
- [ ] 2.1 Replace `ChatState&` param with `GenerationContext` struct (session_id, reasoning_mode)
- [ ] 2.2 Move `db::save_message()` from chat_bus.cpp → store event handlers
- [ ] 2.3 Create `BackendService` class wrapping bus + providers

### Phase 3: Multi-Frontend
- [ ] 3.1 Headless server mode (WebSocket JSON events)
- [ ] 3.2 TUI as client of headless server
