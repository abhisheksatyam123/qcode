## Systems

| Key Component | Purpose |
|---|---|
| **TUI Chat Bus** (`src/tui/chat_bus.cpp`) | Multi-step tool loop: builds messages → calls `generate_text()` → extracts text/tool_calls/tool_results → appends to `response_messages` |
| **OpenAI Response Parser** (`openai_response_parser.cpp`) | Parses `choices[0].message.content` & `.tool_calls` from JSON; sets `finish_reason` |
| **Anthropic Response Parser** (`anthropic_response_parser.cpp`) | Parses `content[]` blocks for text + `tool_use`; extracts `stop_reason` |
| **`GenerateResult`** (`include/ai/types/generate_options.h`) | `finish_reason` defaults `kFinishReasonError`, `error` = `optional<string>` (empty). `is_success()` = `!error && finish_reason != kFinishReasonError` |
| **HTTP Layer** (`http_request_handler.cpp`) | `execute_single_request()`: 200 → `result.text = body`, non-200 → `error = body` |
| **TUI Message View** (`src/tui/views.cpp`) | `render_view()` assembles `Elements msgs`; each message followed by `separatorLight()` colored by role |
| **Spinner Thread** (`src/tui/main.cpp`) | Background thread calls `advance_frame()` every 120ms → triggers screen re-render → animates spinner during generation |
| **Scroll Container** (`src/tui/views.cpp`) | `vscroll_indicator \| focusPosition* \| yframe`: viewport fills parent height, content scrolls within |

## Bugs Fixed

### B1: Agent Stops After 2 Tool Calls

| File | Change |
|---|---|
| `src/providers/openai/openai_response_parser.cpp` | Added `else` block when `choices` missing/empty → sets `result.error` + `LOG_ERROR` |
| `src/tui/chat_bus.cpp` | Added `LOG_ERROR` inside `!step_res.is_success()` branch |
| `src/providers/base_provider_client.cpp` | Added `LOG_ERROR` in `generate_text_single_step` when HTTP request fails |
| `src/providers/anthropic/anthropic_response_parser.cpp` | Added defensive `else` logging when content array missing |

**Root cause**: Provider returned HTTP 200 / 100-byte body missing `choices`. Parser left `finish_reason=kFinishReasonError` with no error message. `is_success()` returned false silently — loop broke with invisible error.

### B2: Ghost Scroll at Bottom — Extra scroll space below content

| File | Change |
|---|---|
| `src/tui/views.cpp` | Removed `yflex` from scroll chain: `yframe \| yflex` → `yframe` |

**Root cause**: `yflex` inside the scroll container added flexible empty space below messages. This space extended the scrollable range, creating phantom scroll units below actual content. Scrolling down entered this void; scrolling back up required reversing through it.

**How `yframe` works**: Makes the scroll viewport fill available parent height. Content scrolls inside. When content < viewport, viewport still fills space (no flex gap inside scroll range). This is the correct behavior.

### B3: I-beam Cursor Flicker While Typing

| File | Change |
|---|---|
| `src/tui/main.cpp` | Wrapped `store.advance_frame()` in `if (store.is_generating())` guard |

**Root cause**: Spinner thread called `advance_frame()` every 120ms even when idle. This triggered `notify()` → `screen.Post(Event::Custom)` → full TUI re-render → cursor reset/flash in the input component.

**Fix**: Only advance animation frames during active generation. `expire_toasts()` is already called from the render callback itself, so toast cleanup still happens on user interaction.

## Features Added

### F1: Role-Colored Separator Line

| File | Change |
|---|---|
| `src/tui/views.cpp` | Replaced `text("")` gap between messages with `separatorLight() \| color(roleColor)` — Assistant → `accent(orange)`, User → `user_green()`, System/tool → `dim_gray()` |
