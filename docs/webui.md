# WebUI — enhancements (T0–T9 + R1–R4)

Stack: `apps/webui/src/` — `app.js` (~5k lines), `index.html`, `style.css`.
Build: `cd apps/webui && npm run build`. Tests: `node --test test/webui_unit.test.js test/webui_integration.test.js` (19 tests).

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

## Files / Sessions / Terminal (T5–T6)
- Explorer + sessions live filters; collapsible `.diff-view`; `.open-tabs` bar
  with dirty-agnostic close; session pin (`★/☆`, `qcode-pinned`) + sort pinned-first.
- Terminal: xterm poll auto-reconnects (12 fails → `startTerminal`).

## Tests (R3)
`test/webui_unit.test.js` extracts pure fns from `app.js` source and checks:
`fuzzyScore/Filter`, `shortPath`, `formatBytes/Ms`, `detectFsLanguage`,
`extractChildSessionId`, gfm markdown flags, new CSS classes, a11y hooks.
