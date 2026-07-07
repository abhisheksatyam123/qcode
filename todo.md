## Systems

### High-Level Summary of Achieved OpenCode Alignment

1. **TUI Message Layout**:
   * User and Assistant messages are left-aligned in a unified timeline with distinctive accent-colored separators.
   * Thinking tokens render as dimmed markdown inline (OpenCode reasoning style) with no labels.
   * Redundant model headers and footers are merged into a single clean inline tag: `Assistant · <model_name>`.

2. **Rich Tool Call & Result Timeline**:
   * Tool calls render with a wrench icon `🔧` and argument preview.
   * Tool results render with a green checkmark `✔` or a red cross `❌` indicating success/failure.
   * Auto-sanitizes older database records to strip legacy box-drawing characters on the fly.

3. **Boxed Chat Input Panel**:
   * Top area provides multiline editing with the prompt chevron `❯`.
   * Bottom status toolbar renders the active model badge, a green/gray tools status indicator (`🔧 Tools: ON` vs `⚙ Tools: OFF`), and keyboard instruction helper.
   * Keyboard shortcut `Alt+Enter` inserts newlines, while standard `Enter` sends the message.

4. **Output Truncation & Selection Clipboard Resolution**:
   * Large outputs (default > 4,096 characters) are saved to `/home/abhi/notes/state/data/tool-output/` and replaced with an OpenCode truncation banner.
   * Yanking/copying a truncated message in selection mode (`Ctrl+P` -> `y`) reads the full file and copies the untruncated content.

5. **Streaming Support with Tools Enabled**:
   * Refactored `run_tools_generation` in `src/tui/chat.cpp` to execute step-by-step LLM generations manually.
   * If a step is the final answer (no subsequent tool execution is needed), the TUI switches to `client.stream_text` with tools disabled. This streams the final text response token-by-token directly to the user interface, solving the blocking/freezing TUI issue when tools are enabled.

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
- [x] Build and compile all targets successfully

### Active (New)
- [ ] *Pending next instructions from the user*
