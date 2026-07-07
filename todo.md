## Systems

### Status Update: Message Rendering Refactor

1. **Refactored Rendering Engine**: Transitioned the chat history display logic in `src/tui/views.cpp` to use `render_message`, which iterates through `ai::MessageContent` (Text, ToolCall, ToolResult) rather than relying on flat string-based message entries.
2. **Modular Component System**: Created a dedicated `src/tui/message_render.cpp` and header file to handle message-part rendering.
3. **Visual Alignment**: The initial infrastructure for OpenCode-style message part rendering is in place, and the code now builds successfully using the new rendering pipeline.

## Tasks

### Completed (Done)
- [x] Fix duplicate text rendering bug in views.cpp
- [x] Render reasoning tokens as dimmed markdown (no header) matching OpenCode reasoning-part
- [x] Refactor chat.cpp to resolve thread-safety issues and race conditions
- [x] Align TUI chat history to left-aligned conversation timeline
- [x] Render tool calls and results with custom icons (🔧, ✔, ❌) and color-coded status timelines
- [x] Implement OpenCode-style output truncation budget and file backup logic in bash tool
- [x] Implement visual selection clipboard resolution to copy full untruncated command logs from file
- [x] Create multi-line boxed input panel with status toolbar (model badge, tools toggle status, and shortcuts)
- [x] Add Alt+Enter keyboard newline insertion mapping in TUI
- [x] Implement streaming for the final assistant response when tools are enabled
- [x] Transition message history rendering to part-based `render_message` structure

### Active
- [ ] Implement `BlockTool` component wrapper for tool rendering.
- [ ] Implement `BashToolRender` with status indicators and output styling.
- [ ] Implement `TaskToolRender`.
- [ ] Refine system prompt rendering to OpenCode compact style.
