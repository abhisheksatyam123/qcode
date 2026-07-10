# QCode UI Improvement — OpenCode-style TUI

## Systems

### Architecture (Current)
- **TUI**: Bus → Store → FTXUI render (OpenCode-style tool rendering)
- **Server**: qcode-server with HTTP/NDJSON API + built-in Web UI
- **CLI**: qcode-cli for scripting
- Shared backend: `chat_bus.cpp`, `GenerationContext`, `BackendService`

### GenerationContext
```
struct GenerationContext {
    std::string session_id;
    std::string reasoning_mode = "off";
    int tool_call_count = 0;
    double total_tool_time_ms = 0.0;
};
```

### How to Build & Run
```
# Build everything
cd build && cmake .. -DQCODE_BUILD_SERVER=ON && make -j$(nproc)

# TUI
./src/tui/qcode-tui

# Server + Web UI (open http://localhost:9080)
./src/server/qcode-server --port 9080

# CLI
./src/server/qcode-cli --prompt "Hello" --verbose
```

## Tasks

### Phase 1 & 1b: Tool Rendering ✅
- Tool blocks, per-tool renderers, collapse/expand, 'c' toggle

### Phase 2: Backend Decoupling ✅
- GenerationContext, db::save_message in store handlers, BackendService

### Phase 3: Multi-Frontend ✅
- HTTP server (POST /generate, GET /health, /providers)
- Web UI (chat, markdown, multi-turn sessions, tool blocks)
- CLI client (--prompt, --session, --verbose)

### Phase 3 Enhancements ✅
- Real-time NDJSON streaming (background thread)
- Multi-turn sessions (session_id persistence)
- Markdown rendering in Web UI
- TUI per-block keyboard nav (Up/Down to focus, c/Enter to toggle)
