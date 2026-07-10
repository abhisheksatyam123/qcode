# QCode UI Improvement — OpenCode-style TUI

## Systems

### Goal
Make QCode TUI look like OpenCode with beautiful tool rendering and clean backend/frontend separation.

### Architecture (Current)
- Bus pattern already exists: `BusPort` → typed events → `AppStore` → FTXUI render
- Backend (`chat_bus.cpp`) has zero FTXUI deps — communicates via bus events only
- Coupling: `run_generation_with_bus()` takes `ChatState&` (only reads `session_id` + `reasoning_mode`)

### Key Files
```
src/tui/message_render.cpp  — ~630L — tool block, render_message, render_tool_pair (MAIN TARGET)
src/tui/tool_renderers.cpp  — per-tool renderers (BashToolRender, etc.)
src/tui/main.cpp            — 554L — monolithic entry, keyboard handler
include/ai/tui/state.h      — ChatState struct
include/ai/tui/message_render.h — BlockTool, render_message decl
include/ai/tui/views.h      — color palette, accent() etc.
```

### Phase Status
Phase 1 (1.1-1.5): ✅ Complete. Beautiful tool blocks, per-tool renderers.
Phase 1b: Almost complete. Pairing + collapse/expand logic is in place.
Phase 2: Next — decouple backend from ChatState&.
Phase 3: Planned — multi-frontend (headless server).

## Tasks

### Phase 1 & 1b: Tool Rendering with Collapse/Expand (Complete)
- [x] 1.1 Redesign `BlockTool()` — accent-colored left border, icon, status badge, duration
- [x] 1.2 Add per-tool-type renderers (bash, task, read_file, write_file, search)
- [x] 1.3 Improve `render_tool_call()` — icon per type, better layout
- [x] 1.4 Improve `render_tool_result()` — truncated output, expand hint, better structure
- [x] 1.6 Add `tool_collapse_state` map to `ChatState` (key: tool_call_id, value: expanded bool)
- [x] 1.7 Modify `render_message` to pair tool calls with results by `tool_call_id`
- [x] 1.8 Create `render_tool_pair()` that renders combined call+result with expand/collapse
- [x] 1.10 Visual indicator: ▼/▶ chevron, show input when collapsed, show both when expanded
- [x] 1.11 Build and test (compiles and links)
- [ ] 1.9 Implement keyboard toggle (Enter/Space or 'c') to toggle collapse state
  - 'c' handler exists as stub (returns false — no-op)
  - Need to track which block has focus and toggle its collapse state in ChatState

### Phase 2: Backend Decoupling
- [ ] 2.1 Replace `ChatState&` param with `GenerationContext` struct (session_id, reasoning_mode)
- [ ] 2.2 Move `db::save_message()` from chat_bus.cpp → store event handlers
- [ ] 2.3 Create `BackendService` class wrapping bus + providers

### Phase 3: Multi-Frontend
- [ ] 3.1 Headless server mode (WebSocket JSON events)
- [ ] 3.2 TUI as client of headless server
