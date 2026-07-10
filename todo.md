# QCode UI Improvement — OpenCode-style TUI

## Systems

### Architecture (Current)
- **TUI**: Bus → Store → FTXUI render (Phase 1 & 2 complete)
- **Server**: qcode-server executable with HTTP/NDJSON API (Phase 3.1 complete)
- Shared backend: `chat_bus.cpp`, `GenerationContext`, `BackendService` across both

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
# Build TUI (default)
cd build && cmake .. && make qcode-tui && ./src/tui/qcode-tui

# Build server
cd build && cmake .. -DQCODE_BUILD_SERVER=ON && make qcode-server
./src/server/qcode-server --port 9080

# Test server
curl http://localhost:9080/health
curl http://localhost:9080/providers
curl -X POST http://localhost:9080/generate -d '{"text":"Hello","provider":"openai","model":"gpt-4"}'
```

## Tasks

### Phase 1 & 1b: Tool Rendering ✅
All complete — tool blocks, per-tool renderers, collapse/expand, 'c' toggle.

### Phase 2: Backend Decoupling ✅
- [x] 2.1 GenerationContext replaces ChatState&
- [x] 2.2 db::save_message moved to store event handlers
- [x] 2.3 BackendService class

### Phase 3: Multi-Frontend
- [x] 3.1 Headless server mode (HTTP/NDJSON streaming)
  - POST /generate → streams NDJSON events
  - GET /health, GET /providers
  - No FTXUI deps — pure HTTP/JSON
- [ ] 3.2 CLI client or TUI-as-client mode
