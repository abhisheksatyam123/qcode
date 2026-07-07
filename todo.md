## Tasks
- [x] Tool calls: left colored bar + icon header + dimmed command body
- [x] Tool results: status bar with duration + output, color-coded (green/red)
- [x] Update tools.cpp formatting to produce structured output
- [x] User messages: right-styled with green accent and "You" header
- [x] System messages: centered dimmed info banner
- [x] Basic markdown rendering: code blocks (```bg), inline code (`bg+bold), bold (**bold**), lists (-,*,1.)
- [x] **max_steps 10→25**: model had no room to finish tool workflows
- [x] **Streaming bugfixes** (chat.cpp):
  - Non-tools path: placeholder pushed async via `screen->Post` — stream could start before it existed, causing UB on `.back()`. Fixed: push synchronously + defensive empty-vector check in `flush_text`.
  - Tools path: no visible feedback during tool-only steps — user saw blank screen. Fixed: push "⏳ Working..." placeholder on first tool call start.
  - Empty assistant message when model only made tool calls. Fixed: show "✅ Done" instead of blank.
- [ ] File attachment display support (needs data model)
- [ ] Assistant messages: model badge per-message (currently uses global selected model)

## Systems
### Files
- `src/tui/views.cpp` — Message rendering + `render_markdown()` helper
- `src/tui/tools.cpp` — `format_tool_call()` / `format_tool_result()`
- `src/tui/chat.cpp` — Generation driver, tool callbacks, message store
- `include/ai/tui/views.h` — Color palette + `render_view()` decl
- `include/ai/tui/state.h` — `ChatState`: chat_history as `vector<pair<role, text>>`

### Streaming Fixes Applied (chat.cpp)

**max_steps**: `10 → 25` — prevents tool-heavy convos from hitting the limit before producing a final answer.

**Non-tools streaming race** (line 237):
- BEFORE: placeholder push was `screen->Post(lambda)` → async. Stream started immediately on background thread. `flush_text` did `chat_history->back().second += batch` but the vector had no `.back()` yet → UB.
- AFTER: `state.chat_history->push_back({"Assistant", ""})` synchronous before stream starts. `flush_text` checks `chat_history->empty()` defensively.

**Tools path visibility** (lines 134-145):
- BEFORE: no Assistant entry until `on_step_finish` fires with non-empty text. If model only makes tool calls for 10+ steps, user sees nothing.
- AFTER: `on_tool_call_start` checks `assistant_msg_idx < 0 && assistant_text->empty()` and pushes "  ⏳ Working..." immediately. Cleared by `on_tool_call_finish` when first tool result arrives.

**Empty final text** (line 221):
- BEFORE: if `gen_result.text` is empty, an empty Assistant message was pushed.
- AFTER: empty text replaced with "  ✅ Done".
