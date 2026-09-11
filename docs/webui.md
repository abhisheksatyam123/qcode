# WebUI — enhancements (T0–T9 + R1–R4 + T4.1b)

Stack: `apps/webui/src/` — `app.js` (~4950 lines), `utils.js` (~284 lines,
17 pure exports), `index.html`, `style.css`.
Build: `cd apps/webui && npm run build`.
Tests: `node --test test/webui_unit.test.js test/webui_integration.test.js`
(24 tests: 9 integration + 15 unit).

## Modules
- `utils.js` (pure, no DOM): fuzzyScore/Filter, shortPath, formatBytes/Ms,
  detectFsLanguage, parseToolValue, sessionIdFromTaskResult,
  extractChildSessionId, esc, capitalize, relTime, extractFrontmatter,
  stripFrontmatter, transformWikilinks, SLASH_COMMANDS, PALETTE_COMMANDS.
  `app.js` imports all 17; unit tests import `utils.js` directly.
- Kept in `app.js` (stateful): parseFrontmatter (`window.jsyaml`),
  renderMarkdown/getMarkedRenderer/renderFrontmatterBox/tagMarkdownLinks/
  bindFrontmatterToggles/navigateMarkdownLink (need `marked`/DOM/session).

## Streaming + honest states (T1, T7)
- `runGeneration` streams NDJSON `POST /session/:id/generate` via `getReader`.
- Transport abort → one automatic `?resume=1` retry into the SAME
  `assistantMsg` (`runGenerationResume`, no duplicate user prompt); else
  `stoppedEarly=true` + `.stopped-early` banner + Retry action.
- `generation.complete` with backend error always surfaces inline
  (`streamError`), even with partial text.
- Reasoning default `off` (no thinking requested). `thinking-hint` shows when
  messages exist but Reasoning=Off. Usage line shows `thinking 0` when
  reasoning is on but the backend reports no reasoning tokens.
- Server: `?resume` branch (404/complete/active-turn drains), 2.5s
  `backend.heartbeat` keepalive, per-turn abort flag, locked queue appends,
  graceful shutdown signalling active sessions.

## Persistence (T2)
`localStorage` keys: `qcode-theme` (legacy), `qcode-prefs`
(provider/model/reasoning/theme/activeTab/showThinking/layoutMode/filesSubtab/sessionId),
`qcode-draft-<sessionId>` (cleared on send), `qcode-pinned` (starred sessions).
Restored in `init()` / `switchSession()`.

## Chat (T3)
- `.msg-actions` per message: Copy / Raw toggle / Rerun; `.copy-code-btn` on
  every `pre code` (clipboard + fallback). Relative `.msg-ts` timestamps
  (`createdAt`, `relTime`). Smart autoscroll (`nearBottom`) + `#jump-latest`
  pill. `#messages` has `aria-live="polite"`; modal has dialog role.
- Markdown: `marked` gfm+breaks; tables/task-lists covered by fixture tests;
  frontmatter/wikilink text transforms unit-tested via `utils.js`.

## Files / Sessions / Terminal (T5–T7.2)
- Explorer + sessions live filters; collapsible `.diff-view`; `.open-tabs` bar;
  session pin (`★/☆`, `qcode-pinned`) + sort pinned-first.
- Terminal: xterm poll auto-reconnects (12 fails → `startTerminal`).
- T7.2: stats/sessions/terminal failures render `.error-panel` with Retry.

## Vendoring (T8.3)
xterm/xterm-css/fit-addon/hljs(+32 langs)/hljs-css vendored under `src/`
as `vendor-*` (local-first, CDN `onerror` fallback). Recipe:
`src/vendor/hljs-entry.js` (`esbuild --bundle --minify --format=iife`).
Fonts + nerd-fonts remain CDN.

## Subagent fallback (P0.2, backend)
`run_subagent_turn_multi` retries failover-worthy errors (retryable flag,
ModelError/unsupported-model, resolve failures, transient 429/5xx) across up
to 4 candidates: explicit target, then Zen → Antigravity → shuffled
OpenRouter. Cursor excluded unless explicitly requested.
Success-after-retry reports `fallback_used/attempts/model`.
