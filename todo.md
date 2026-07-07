## Systems

### Visual Parity Roadmap: OpenCode TUI Architecture

1. **Rendering Engine**: Component-based `render_message` (using `MessageContent` parts).
2. **Visual Blocks**: `BlockTool` component provides standard OpenCode-style tool rendering (headers, status-colored borders, spinner integration).
3. **Multiline Input & Toolbar**: OpenCode-style boxed panel with integrated model/tool status and newline shortcut helpers.
4. **Layout**: Unified left-aligned message timeline.

## Tasks

### Completed (Done)
- [x] Align TUI chat history to left-aligned conversation timeline
- [x] Create multi-line boxed input panel with status toolbar
- [x] Implement streaming for the final assistant response
- [x] Transition message history rendering to part-based `render_message` structure
- [x] Create `BlockTool` component wrapper for tool rendering

### Phase 3: Visual & Functional Perfection (Active)

#### 1. Component-Specific Rendering (Tooling)
- [ ] Implement `BashToolRender` using `BlockTool`:
    - [ ] Display command string with `$` prefix.
    - [ ] Render stdout/stderr output with scrollable/truncated view.
    - [ ] Show exit code and status (running/success/failed).
- [ ] Implement `TaskToolRender` using `BlockTool`:
    - [ ] Handle subagent task lifecycle (status, sub-steps).
    - [ ] Display task-specific operation name and input summary.

#### 2. Visual Polish & System Prompt
- [ ] Implement System Prompt renderer:
    - [ ] Compact, dimmed box style matching OpenCode system banners.
    - [ ] Left-aligned timeline integration.
- [ ] Refine syntax highlighting:
    - [ ] Apply consistent `syntaxStyle` for all markdown/code content blocks.
    - [ ] Ensure theme tokens (e.g., `textMuted`, `accent`) are perfectly aligned with OpenCode's theme variables.

#### 3. State & Interaction
- [ ] Implement `ToolCallObservability` state management:
    - [ ] Logic for collapsible/expandable output panes.
    - [ ] Integration with real-time tool state updates (running/completed).
- [ ] Final visual audit:
    - [ ] Fine-tune padding, margins, and borders for "pixel-perfect" match to OpenCode.
    - [ ] Verify spinner state transitions in `BlockTool`.

