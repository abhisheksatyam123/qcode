## STATUS: DONE (committed; see git history).
# todo: empty assistant response handling

## Tasks
- [x] Surface empty model responses in the TUI (was silently dropped -> now a
      yellow "⚠ empty" header chip + transient toast; no fabricated message).

## Systems
Project: AI SDK CPP (C++20, CMake); bus-driven TUI `qcode-tui` in `src/tui`;
logs `/tmp/qcode.log` (file_logger DEBUG). Build: `cmake --build build --target qcode-tui`.

Completed this session (build green, binary `build/src/tui/qcode-tui`):
- Prompt queue, header queue/error chips, Stats tool stats, error visibility,
  structured orchestration logging (prior turns).
- Empty-response handling:
  - `src/tui/chat_bus.cpp` (~line 277): in `is_success()` but `final_text.empty()`
    branch, publish `ErrorOccurred{severity="warning", message="The model returned
    an empty response (no text generated)."}` instead of doing nothing.
  - `src/tui/store.cpp` `ErrorOccurred` handler: severity-aware - "warning" sets
    `status="warn"`, clears `is_generating`, shows a ~6s toast (no System chat
    message, no hard-error status); "error" keeps prior behavior.
  - `src/tui/views.cpp` header (~line 357): `status=="warn"` -> yellow "⚠ empty" chip.

Contract notes:
- `ErrorOccurred` severity documented as "info"/"warning"/"error"; prior usages
  all "error". New "warning" path is distinct from errors.
- `SessionStatusChanged(idle)` is published BEFORE the empty branch, so final
  status after a warning is "warn" (idle then warn). Cleared on next action.
