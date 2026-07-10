# QCode UI Improvement — OpenCode-style TUI

## Systems

### Goal
Make QCode TUI look like OpenCode with beautiful tool rendering and clean backend/frontend separation.

### Architecture (Current)
- Bus pattern: `BusPort` → typed events → `AppStore` → FTXUI render
- Backend (`chat_bus.cpp`) now uses `GenerationContext` instead of `ChatState&` (Phase 2.1 done)
- Remaining coupling: `db::save_message()` calls directly in chat_bus.cpp
- Still need: `BackendService` class wrapping bus + providers

### Key Files
```
src/tui/message_render.cpp  — tool rendering (BlockTool, render_tool_pair, etc.)
src/tui/main.cpp            — entry point, keyboard handler
src/tui/chat_bus.cpp        — backend generation logic (needs db::save_message refactor)
src/tui/views.cpp           — FTXUI views, layout
include/ai/tui/chat_bus.h   — GenerationContext, run_generation_with_bus decl
include/ai/tui/state.h      — ChatState struct
include/ai/tui/bus/         — event bus types
src/tui/db/                 — database helpers (save_message, etc.)
```

### GenerationContext
```
struct GenerationContext {
    std::string session_id;
    std::string reasoning_mode = "off";
    int tool_call_count = 0;          // synced back to ChatState after gen
    double total_tool_time_ms = 0.0;  // synced back to ChatState after gen
};
```

## Tasks

### Phase 1 & 1b: Tool Rendering ✅
All complete — tool blocks, per-tool renderers, collapse/expand, 'c' toggle.

### Phase 2: Backend Decoupling
- [x] 2.1 Replace `ChatState&` with `GenerationContext` in all backend functions
- [ ] 2.2 Move `db::save_message()` from chat_bus.cpp → store event handlers
- [ ] 2.3 Create `BackendService` class wrapping bus + providers

### Phase 3: Multi-Frontend
- [ ] 3.1 Headless server mode (WebSocket JSON events)
- [ ] 3.2 TUI as client of headless server
