## Systems

### Visual Parity Roadmap: OpenCode TUI (Phase 2)

1. **Structured Tool Rendering**:
   - Implemented `BlockTool` for consistent tool-call/result rendering.
   - Refactored `render_message` to use `BlockTool` and modular `ai::MessageContent` parts.
2. **Visual Consistency**: 
   - Tool calls and results now have distinct visual blocks, status indicators, and colored borders consistent with OpenCode's styling.

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
- [x] Create `BlockTool` component wrapper for tool rendering.

### Active
- [ ] Implement `BashToolRender` with status indicators and specific output rendering (stdout/stderr).
- [ ] Implement `TaskToolRender` with operation-specific visual labels.
- [ ] Refine system prompt rendering to OpenCode compact style.
- [ ] Match OpenCode's syntax-highlighting style (`syntaxStyle`) for markdown and code parts.
- [ ] Build and verify all visual components.
