# todo: fix overly aggressive TUI scrolling

## Status
DONE (committed). Ratio-based scroll replaced with absolute line-based
`focusPosition(x, y)` plus fixed per-input line deltas (`kLinesPerWheel=3`,
`kLinesPerPage=20` in main.cpp). Prior qcode-tui todos DONE; provider-arch gaps
PARKED. `qcode-tui` builds clean. Manual smoothness check still pending — the
TUI cannot run headless in this env; tune the two constants if needed.

## Problem
Scrolling in the qcode TUI feels "too aggressive" — content moves too far / too
fast per input, worst with mouse wheel and during streamed auto-scroll.
Root cause: scroll offset is stored as a **ratio in [0,1]**, so a fixed ratio
delta (0.05/wheel tick, 0.10/page) maps to a wildly different number of lines
depending on conversation height. ~50 lines/tick on a ~1000-line conversation.
Ratio scrolling does not correspond to a consistent, predictable line count.

## Decision (firm, from user)
- Scrolling MUST be a **fixed line count per input**, not a ratio. Ratio-based
  scroll is rejected as wrong ("stupid design").
- Mechanism: ftxui's `focusPosition(int x, int y)` (absolute, line-based).
  Drop `focusPositionRelative(float, float)` (ratio-based).

## Systems (grounded in code)

### ftxui scroll API — build/_deps/ftxui-src/include/ftxui/dom/elements.hpp:112-113
- `Decorator focusPosition(int x, int y);`        // absolute, LINE-BASED. USE THIS.
- `Decorator focusPositionRelative(float x, float y);` // ratio-based. REMOVE.
- `Element frame(Element)` / `yframe(Element)` scrollable area; `vscroll_indicator`
  draws scrollbar. `focusPosition` y is clamped to content bounds by ftxui, so a
  huge y (e.g. INT_MAX) pins to bottom — no manual clamp-to-content needed.

### Current scroll state — include/ai/tui/state.h:39-40 (struct `ChatState`)
- `std::shared_ptr<float> scroll_ratio = make_shared<float>(1.0f);`
- `bool auto_scroll = true;`
- Init: `auto_scroll = true` (state.h:40); scroll_ratio starts at bottom (1.0).
- render_view decl include/ai/tui/views.h:36; def src/tui/views.cpp:336 param
  `const std::shared_ptr<float>& n` (named `n` in .cpp). Call site main.cpp:569
  passes `state.scroll_ratio`.

### Input handling — src/tui/main.cpp:483-489
- PageUp   : auto_scroll=false; ratio = max(0, ratio - 0.10)
- PageDown :                 ; ratio = min(1, ratio + 0.10)
- End      : auto_scroll=true ; ratio = 1.0
- Home     : auto_scroll=false; ratio = 0.0
- WheelUp  : auto_scroll=false; ratio = max(0, ratio - 0.05)
- WheelDown:                 ; ratio = min(1, ratio + 0.05)
- Init auto_scroll=true (main.cpp:137). PRIME SUSPECT: 0.05/tick ratio on a tall
  conversation = ~50 lines/tick.

### Auto-scroll render — src/tui/views.cpp:499 and :553
- `vbox(...) | vscroll_indicator | (state.auto_scroll ? focusPositionRelative(0.f,1.f)
  : focusPositionRelative(0.f, min(1.f, *scroll_ratio))) | yframe | flex`
- When auto_scroll: forces bottom every frame. Else: ratio→focusPositionRelative.

## Implementation plan (to apply)
1. **state.h:39**: change `shared_ptr<float> scroll_ratio` -> `shared_ptr<int>
   scroll_line` (init `make_shared<int>(INT_MAX)` to start pinned at bottom,
   matching auto_scroll=true). Keep `bool auto_scroll`.
2. **views.cpp:499 & :553**: replace ternary with
   `focusPosition(0, *state.scroll_line)` (drop focusPositionRelative branch).
   When `auto_scroll` true, pass `INT_MAX` for y (pins to bottom); else pass
   `*scroll_line`. App-side clamp `*scroll_line` to [0, INT_MAX].
3. **main.cpp:483-489**: replace ratio deltas with fixed line deltas:
   - WheelUp   : auto_scroll=false; *scroll_line = max(0, *scroll_line - LINES_PER_WHEEL)   (e.g. 3)
   - WheelDown :                 ; *scroll_line = min(INT_MAX, *scroll_line + LINES_PER_WHEEL)
   - PageUp    : auto_scroll=false; *scroll_line = max(0, *scroll_line - LINES_PER_PAGE)     (e.g. 20)
   - PageDown  :                 ; *scroll_line = min(INT_MAX, *scroll_line + LINES_PER_PAGE)
   - Home      : *scroll_line = 0
   - End       : auto_scroll=true; *scroll_line = INT_MAX
4. **render_view signature** (views.h:36 / views.cpp:336): change param type
   `const shared_ptr<float>& n` -> `const shared_ptr<int>& n`; update call site
   main.cpp:569 accordingly.
5. **Clamp**: keep `*scroll_line` in [0, INT_MAX] to avoid overflow; rely on ftxui
   to clamp to actual content height.

### Known follow-ups / caveats
- LINES_PER_PAGE is approximate: app has no access to frame pixel/line height, so
  it is a constant guess (~20). Track as tuning follow-up.
- focusPosition x is always 0 (horizontal scroll not used).

## Tasks
- [x] Edit state.h:39 — `shared_ptr<float> scroll_ratio` -> `shared_ptr<int> scroll_line`
      (init INT_MAX).
- [x] Edit views.cpp:499 & :553 — `focusPosition(0, *scroll_line)`; auto_scroll -> INT_MAX.
- [x] Edit views.h:36 / views.cpp:336 — render_view param type float->int.
- [x] Edit main.cpp:483-489 — replace ratio deltas with fixed line deltas
      (Wheel ±3, Page ±20, Home=0, End=INT_MAX). Init scroll_line=INT_MAX.
- [x] Build qcode-tui (`cmake --build build` or repo build) to confirm compile.
      TUI cannot run headless here; manual verify smoothness with a long chat.
- [x] Note in todo: confirm wheel/page line constants feel right; tune if needed.

## Out of scope
- Changing PageUp/Down/Home/End semantics (keep them).
- Provider/arch work (done; see todo-provider-arch + gaps).
- git commit of notes/ (notes left uncommitted; provider-arch committed as da75886).
