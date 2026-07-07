## Tasks
- [x] Fix tool call and result rendering to match OpenCode UI
- [x] Add ToolCall/ToolResult roles in chat.cpp
- [x] Implement OpenCode-style rendering in views.cpp
- [x] Verify rendering with standalone FTXUI test
- [x] Commit changes

## Systems
- `src/tui/chat.cpp`: Stores messages as `{"ToolCall", formatted_str}` and `{"ToolResult", formatted_str}`
- `src/tui/views.cpp`: Renders ToolCall/ToolResult with left-border separators, color-coded headers, dimmed output
- `src/tui/tools.cpp`: `format_tool_call()` produces `"▾ Observability · toolname\n  $ command"`, `format_tool_result()` produces `"  └─ Completed successfully\n  output"`
- `include/ai/tui/state.h`: `ChatState` has `scroll_offset` member for scrolling
