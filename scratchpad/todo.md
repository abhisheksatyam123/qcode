# WebUI & TUI Improvements: Parity, Layouts, Aesthetics, and Navigation

## Goal
Improve both the QCode WebUI and the TUI:
1. Fix WebUI text rendering (done).
2. Upgrade Markdown rendering to marked.js with support for headings, code blocks, lists, etc. (done).
3. Redesign WebUI tool block rendering to match the TUI exactly (collapsible blocks, status color rails, command/prompt/workdir lines, exit status codes, and tool emoji icon prepending) (done).
4. Beautify the WebUI chat screen: modern dark palette, smooth animations, gorgeous user gradients, rounded assistant bubble cards, and a sleek floating composer wrapper with focus border glow (done).
5. Fix the WebUI terminal occupying only half screen in Tab View by stretching it to 100% width via flex rules (done).
6. Unmap TUI navigation hotkeys `h` and `k` (as well as `j` and `l` for tool blocks/suggestion menu) so keyboard shortcuts do not conflict with text writing or layout navigation since navigation is mouse-based (done).
7. Fix session workspace fallback for tool calls: when bash tool executes with an empty `workdir` field, fallback to the session's workspace path so the execution starts in the directory selected at session creation instead of the server base directory (done).

## Systems
- **WebUI Styles**: `style.css` contains deep dark palettes, modern input glow, tool border rails, and terminal container flex stretch.
- **TUI Keybindings**: `src/tui/main.cpp` handles event routing for arrow keys and suggestions. The standard letter mappings `h`, `j`, `k`, `l` have been stripped out.
- **History Mapping**: `server_main.cpp` keeps raw message categories (`ToolCall`, `ToolResult`, `Reasoning`) intact for full history reconstruction in `app.js`.
- **Workspace Tool Context**:
  - `workspace` field added to `GenerateOptions` (populated in `chat_bus.cpp` via `ctx.workspace`).
  - `workspace` field added to `ToolExecutionContext` (copied from options in `tool_executor.cpp`).
  - `resolve_cwd` in `bash_tool.cpp` uses `fallback_workspace` if `workdir` is empty, ensuring tool commands default to the current active session workspace directory.

## Tasks
- [x] Integrate `marked.js` markdown parser in `index.html`
- [x] Retain raw role types in C++ server history responses
- [x] Rewrite history parser in `app.js` to group nested tool logs
- [x] Render tool block icons in the header
- [x] Design tool blocks to match TUI borders and headers
- [x] Add `flex: 1` structure to `#terminal-panel` in CSS
- [x] Redesign layout wrapper, bubbles, code blocks, and input composer styles
- [x] Strip `h`, `j`, `k`, `l` character event bindings from TUI keyboard mapping (`src/tui/main.cpp`)
- [x] Implement fallback workspace forwarding to tool calls for empty workdirs (`bash_tool.cpp`)
- [x] Compile the project using `ninja` (successful)
- [x] Run unit tests (`ai_tests` - 170/170 passed)
