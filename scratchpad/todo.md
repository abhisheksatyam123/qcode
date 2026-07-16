## Systems
- Goal: Improve the web UI — (1) Esc should pause/cancel the active generation from any focus context; (2) polish the mobile UI to match the desktop polish.
- Files touched:
  - `apps/webui/src/app.js`: added `pauseActiveGeneration()` helper; routed document-level Escape, xterm Escape handler, and a new Pause button through it; toggled Pause button visibility in `setGenerating()`.
  - `apps/webui/src/index.html`: added `pause-btn` to input actions; enriched viewport meta (`viewport-fit=cover`, `user-scalable=no`, `maximum-scale=1`); updated input-info hint text.
  - `apps/webui/src/style.css`: pause-btn styles; `100dvh`/`overflow-x` on `#app`; body tap-highlight + overscroll fixes; safe-area top/bottom padding; mobile (≤1024/900/600/480) sidebar tap-targets, header, input, file explorer/editor, terminal, and bottom-sheet modals.
- Build: `scripts/build.py --mode release` copies built webui next to qcode-server; vite build + node --check pass; C++ tests still green (273/273).

## Tasks
- [x] Make Esc pause the active generation globally (document + xterm + pause button).
- [x] Audit + polish mobile responsive CSS (layout, tap targets, safe-area, modals).
- [x] Rebuild (webui + full server) and verify build + C++ test suite.
