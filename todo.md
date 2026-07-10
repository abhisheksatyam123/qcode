# QCode UI Improvement — OpenCode-style TUI

## Systems

### Architecture (Current)
- Bus pattern: `BusPort` → typed events → `AppStore` → FTXUI render
- Backend fully decoupled from ChatState via `GenerationContext` (Phase 2.1)
- Database persistence moved from backend to store event handlers (Phase 2.2)
- `BackendService` class wraps bus + providers for clean API (Phase 2.3)
- Backend has zero knowledge of UI state, storage, or FTXUI

### GenerationContext
```
struct GenerationContext {
    std::string session_id;
    std::string reasoning_mode = "off";
    int tool_call_count = 0;          // synced back to ChatState after gen
    double total_tool_time_ms = 0.0;  // synced back to ChatState after gen
};
```

### BackendService
```
class BackendService {
    BackendService(bus::BusPort& bus, const std::vector<ProviderInfo>& providers);
    void run_generation(provider_name, model_id, system_prompt, messages,
                       enable_tools, GenerationContext& ctx);
};
```

### Key Files
```
src/tui/message_render.cpp  — tool rendering (BlockTool, render_tool_pair, etc.)
src/tui/main.cpp            — entry, keyboard handler, uses BackendService
src/tui/chat_bus.cpp        — backend generation logic (now pure bus events)
src/tui/store.cpp           — AppStore: subscribes to events, manages state, persists
include/ai/tui/chat_bus.h   — GenerationContext, BackendService
include/ai/tui/state.h      — ChatState (UI state only)
include/ai/tui/contract/event.h — bus event type definitions
```

## Tasks

### Phase 1 & 1b: Tool Rendering ✅
All complete — tool blocks, per-tool renderers, collapse/expand, 'c' toggle.

### Phase 2: Backend Decoupling ✅
- [x] 2.1 Replace `ChatState&` with `GenerationContext` in all backend functions
- [x] 2.2 Move `db::save_message()` from chat_bus.cpp → store event handlers
- [x] 2.3 Create `BackendService` class wrapping bus + providers

### Phase 3: Multi-Frontend
- [ ] 3.1 Headless server mode (WebSocket JSON events)
- [ ] 3.2 TUI as client of headless server
