# Workspace Cleanup TODO

Scope: make the SDK and `qcode-tui` reliable, portable, secure, testable, and
ready for review.

> **Status / verification note (updated this session):** No C/C++ toolchain
> (`cmake`/`g++`/`clang++`/`uv`) exists on this host, so nothing is compile-
> verified locally. Correctness is established by source inspection plus a
> corrected raw-newline-in-string detector (`/tmp/detect_raw_nl2.py`); the
> tree is CLEAN. The working tree was reduced from 271 changed files (256 of
> them spurious `100644->100755` mode bits on vendored files) to **16 content
> files** after restoring HEAD modes and deleting 6 stray `.bak` files.
> `git diff --check` is clean.
>
> **Committed (this round, 4 commits):** `3cbb8d7` fix(tui) streaming/reasoning/
> error/confirm/config; `c931bfa` fix(sdk) logger/tool-name/stream-guard;
> `91b2818` chore todo update; `55cf6ca` fix(security) shell-command quoting.
> All SDK changes (~17 content files) are committed; only `tmp.json` and the
> scratchpad notes dir remain untracked (intentionally).

## Priority 0 — Correctness blockers

- [ ] Verify the tree compiles at all.
  - [x] The uncommitted edits to `src/tui/store.cpp` (~line 218) and
    `src/tui/commands.cpp` (6 places) contain raw newlines inside `"..."`
    string literals, which is ill-formed C++. Replace with `\n` escapes.
  - [ ] Install build prerequisites (`uv`, `cmake`) — neither is available on
    this machine, so no build has been verified locally.
    **BLOCKED:** no compiler/toolchain on this host; edits verified by source
    inspection + a corrected raw-newline detector (`detect_raw_nl2.py`).**
- [ ] Fix streaming accumulation in `src/tui/chat_bus.cpp`.
  - [x] Keep a full assistant transcript separately from the flush buffer.
  - [x] Publish incremental deltas without replacing the accumulated message.
  - [x] Save the complete assistant response to SQLite.
- [ ] Fix reasoning accumulation.
  - [x] Append reasoning batches instead of replacing them.
  - [x] Preserve the final Anthropic signature.
  - [x] Ensure the final empty reasoning event cannot erase prior content.
- [ ] Fix streaming error finalization.
  - [x] Do not publish a successful final message after an error.
  - [x] Preserve the error status and error message in the UI.
  - [x] Stop generation cleanup consistently on every exit path.
- [x] Fix tool-confirmation bypass: parallel tool execution in
  `src/tools/tool_executor.cpp` skips `on_tool_call_confirm` entirely, and
  parallel is the default. Apply confirmation in both paths.
- [ ] Add a bounded maximum for tool-loop steps.
  - [x] Replace `99999999` with a configurable safe default.
  - [x] Surface a clear limit-exceeded error.
- [ ] Implement true incremental Anthropic streaming.
  - [ ] Use an HTTP content receiver or equivalent streaming API.
  - [ ] Add tests for chunked SSE responses.

## Priority 1 — Security and portability

- [ ] Remove OAuth client IDs/secrets from `src/tui/config.cpp`.
- [ ] Load provider credentials and OAuth configuration from environment or
  user configuration.
- [ ] Replace hardcoded `/home/abhi/...` paths with configurable paths.
  - [ ] Provider configuration path.
  - [ ] Tool-output directory.
  - [ ] Python/keyring executable discovery.
- [ ] Make shell command construction safe everywhere paths are interpolated.
  - [x] BashTool: avoid unescaped `cd <path>; <command>` strings; validate or
    safely quote working directories (done: `cd -- '<cwd>'` with single-quote escaping).
  - [x] `views.cpp`: `git diff` commands interpolate file paths into quoted
    strings; paths containing `"` or `$` break them (done: single-quote `shell_quote()` for all 3 path sites).
- [ ] Implement actual `timeout_ms` enforcement for foreground commands.
- [ ] Review command execution policy and document the trust model for model-
  generated shell commands.
- [ ] Ensure background processes cannot become zombies.
  - [ ] Reap exited children.
  - [ ] Refresh task status from the operating system.
  - [ ] Store and expose background output paths.
- [ ] Fix provider registry semantics.
  - [ ] The `"openai"` id resolves to API key `"unused"` with the OpenCode Zen
    base URL — real OpenAI is unreachable through the registry. Give it its
    own resolver reading `OPENAI_API_KEY`.
  - [ ] Unknown provider ids silently fall back to the generic `opencode`
    resolver; log or surface this.

## Priority 1 — Concurrency and state integrity

- [ ] Audit all worker-thread access to `ChatState`.
- [ ] Protect chat history, messages, status, errors, token counts, and tool
  statistics with a clear synchronization strategy.
- [ ] Keep bus event handling on one thread or enforce thread-safe store
  mutations.
- [ ] Fix thread lifetime bugs.
  - [ ] `/compact` in `src/tui/commands.cpp` spawns a detached thread that
    captures `bus` by reference — use-after-free if the app exits
    mid-compaction. Track it in `background_threads` and join on shutdown.
  - [ ] The LLM worker in `src/tui/main.cpp` captures `&store` and
    `&spawn_generation` (stack objects) and recursively calls
    `spawn_generation` from the worker thread, reading provider/model
    selection off the UI thread. Route queue-drain back through the UI thread
    or make the captured state owned.
  - [ ] `register_tui_providers()` is called on every generation from
    `chat_bus.cpp`; registry map writes can race with concurrent resolves.
    Register once at startup (e.g. `std::call_once`).
- [ ] Add session-id validation to every backend event handler.
- [ ] Define cancellation behavior for an active generation.
- [ ] Make `AppStore::on_change()` subscriptions removable and RAII-safe.
- [ ] Bound parallel tool execution (currently one `std::async` thread per
  call, uncapped) and add a timeout to `execute_async_tool()`'s blocking
  `future.get()`.

## Priority 1 — Persistence

- [ ] Centralize SQLite connection and statement handling.
- [ ] Check and report all SQLite open, prepare, bind, step, and finalize
  failures.
- [ ] Fix `db.cpp` logging: "database opened successfully" is logged three
  times (including after error branches) and SQLite errors are logged at
  INFO level.
- [ ] Add schema versioning and migrations.
- [ ] Use transactions for multi-message tool-call workflows.
- [ ] Enable foreign keys consistently on every connection.
- [ ] Preserve reasoning, tool-call, and tool-result content when reloading a
  session.
- [ ] Make database location configurable.
- [ ] Replace the hand-rolled UUID generator in `src/tui/db.cpp` with the
  already-vendored `stduuid` (the current one shares a `std::mt19937` across
  threads without locking).
- [ ] Add tests using a temporary database.

## Priority 2 — SDK internals (streaming, HTTP, retry, logging)

- [ ] Replace busy-wait polling (`sleep_for(1ms)`) in both stream impls'
  `get_next_event()` with a condition variable or blocking queue.
- [ ] Make the hard 30-second `kEventTimeout` between stream events
  configurable; it aborts legitimately slow generations (long tool
  latencies, deep reasoning).
- [x] Add a double-start guard/mutex to `AnthropicStreamImpl::start_stream()`
  (the OpenAI impl has one).
- [ ] Reuse HTTP connections: `HttpRequestHandler` builds a new
  `httplib::Client` per request (TLS handshake every call).
- [ ] Handle explicit ports (`host:port`) and invalid URLs in
  `parse_base_url()`.
- [ ] Add retry support for streaming requests (only non-streaming `post()`
  retries today).
- [ ] Retry policy improvements.
  - [ ] Return failed results instead of throwing `RetryError` for
    non-retryable errors.
  - [ ] Add jitter and a maximum-delay cap to the exponential backoff.
  - [ ] Make the blocking retry sleep cancellation-aware.
- [ ] Logger fixes.
  - [x] `ConsoleLogger` uses `std::localtime` (not thread-safe); switch to
    `localtime_r` (done: `localtime_r` + `std::tm` buffer).
  - [x] The default global logger writes to stdout, which corrupts the FTXUI
    screen if SDK code logs before the TUI installs a file logger; default to
    `NullLogger` or install the TUI logger first thing.
    **Resolved by installing the file logger first thing:** `src/tui/main.cpp:37`
    calls `ai::install_file_logger("/tmp/qcode.log", ...)` before any FTXUI screen
    is created, so the default `ConsoleLogger` is never used while the screen runs.
    Global default left as `ConsoleLogger` (not `NullLogger`) on purpose: 12 examples
    rely on it for SDK logging and do not call `install_logger` (verified).
  - [x] `ConsoleLogger::set_min_level()` is not thread-safe (done: `min_level_` now `std::atomic<LogLevel>`).
- [x] Fix `create_simple_tool()` / `create_simple_async_tool()` silently
  dropping their `name` parameter (`Tool` never receives it) (done: added `Tool::name` and set it in both helpers).

## Priority 2 — Tests

- [ ] Add TUI store tests for:
  - [ ] Incremental text deltas.
  - [ ] Final text persistence.
  - [ ] Incremental reasoning deltas and signatures.
  - [ ] Empty model responses.
  - [ ] Error state transitions.
  - [ ] Prompt queue ordering and draining.
  - [ ] Stale session events.
- [ ] Add stream tests for OpenAI and Anthropic:
  - [ ] Multiple chunks.
  - [ ] Empty chunks.
  - [ ] Finish events.
  - [ ] Provider error events.
  - [ ] Timeout behavior.
  - [ ] Chunked SSE input.
- [ ] Add BashTool tests for:
  - [ ] Timeout enforcement.
  - [ ] Non-zero exit codes.
  - [ ] Unsafe or unusual working directories.
  - [ ] Background process completion and cleanup.
  - [ ] Output truncation and output-file failures.
- [ ] Add tool-executor tests for confirmation in sequential and parallel
  paths.
- [ ] Add persistence tests for session creation, reload, deletion, and
  migration.
- [ ] Add sanitizer builds with AddressSanitizer and ThreadSanitizer.
- [ ] Add tests that run without provider API keys.

## Priority 2 — Build and packaging

- [ ] Add a `BUILD_TUI` CMake option.
- [ ] Avoid fetching/building FTXUI when the TUI is disabled.
- [ ] Make examples independently optional and provider-aware.
- [ ] Correct installed package component metadata.
  - [ ] Include antigravity, registry, and langfuse.
  - [ ] Export all required public dependencies.
  - [ ] Verify shared and static library installations.
- [ ] Add an installed-package consumer test.
- [ ] Verify a clean build from a fresh checkout with recursive submodules.
- [ ] Document minimum tested compiler and dependency versions.
- [ ] Add build checks for Linux, macOS, and Windows where supported.

## Priority 2 — CI and developer workflow

- [ ] Make CI formatting checks independent of unavailable local tools.
- [ ] Add CI jobs for:
  - [ ] Debug build and tests.
  - [ ] Release build and tests.
  - [ ] Sanitizers.
  - [ ] Package installation/consumer build.
  - [ ] Static analysis.
- [ ] Ensure fork pull requests do not require unavailable API secrets.
- [ ] Make ClickHouse integration tests opt-in or isolate them from normal CI.
- [ ] Add a documented bootstrap path when `uv`, CMake, or system packages are
  missing.

## Priority 3 — Documentation and product consistency

- [ ] Update README feature status; embeddings are implemented but listed as
  “Coming Soon”.
- [ ] Document all supported providers, including registry-backed providers.
- [ ] Document reasoning configuration and provider limitations.
- [ ] Document tool execution security and filesystem permissions.
- [ ] Document database location, schema, and session behavior.
- [ ] Add a real TUI smoke-test procedure.
- [ ] Document known limitations such as global reasoning visibility and
  missing per-message collapse.

## Priority 3 — TUI polish

- [ ] Mouse click-to-copy in `main.cpp` estimates the clicked message by
  vertical ratio, which mis-selects with wrapped/variable-height messages;
  track rendered line offsets instead.

## Repository hygiene

- [x] Remove unrelated `100644 -> 100755` mode changes (hundreds of files,
  including vendored dependencies) (done: restored HEAD modes for 264 tracked files; diff now 14 content files).
- [ ] Review submodule changes and restore unintended submodule state.
- [ ] Remove or intentionally track `tmp.json`.
- [ ] Review untracked scratchpad files and keep only deliberate task notes.
- [x] Delete stray backup files: `chat.cpp.bak`, `tools.cpp.bak`,
  `views.cpp.bak` at repo root and `src/tui/chat_bus.cpp.bak`,
  `src/tui/message_render.cpp.bak` (done: also removed `include/ai/logger.h.bak`; 6 total).
- [ ] Remove the legacy non-bus generation path in `src/tui/chat.cpp`
  (~21 KB) once `chat_bus.cpp` fully replaces it; it duplicates the whole
  tool/stream loop and DB writes.
- [ ] Consolidate `.todo.md` and `todo.md` at the repo root into the notes
  scratchpad or this file.
- [x] Run `git diff --check` (done: clean, no trailing whitespace / conflict markers).
- [ ] Run formatting, build, tests, and linting from a clean working tree.
- [ ] Review the final diff for secrets, generated files, and vendored changes.


## Open decisions / follow-ups (found during cleanup)

- [ ] **Decide on `/tools` and `/help` slash-command removal in
  `src/tui/commands.cpp`.** The large `commands.cpp` refactor (~262-line diff)
  removes the `/tools` and `/help` command handlers and adds a
  `bus::BusPort& bus` parameter, wired through both `main.cpp` call sites and
  `commands.h` (verified consistent). No existing todo item covers this
  removal — confirm it was intended, or restore the two handlers. This is a
  possible unintended behavioral regression and should be explicitly reviewed
  before the cleanup is closed out.
- [ ] **Compile-verify the full tree once a C/C++ toolchain is available.**
  This host has no `cmake`/`g++`/`clang++`/`uv`, so all fixes this round are
  inspection-verified only. Re-run the verification checklist (format/build/
  test/lint) from a clean tree before merging.

## Verification checklist

- [ ] `uv run scripts/format.py --check`
- [ ] `uv run scripts/build.py --mode debug --tests`
- [ ] `uv run scripts/build.py --mode release --tests`
- [ ] `uv run scripts/lint.py`
- [ ] `ctest --output-on-failure --parallel`
- [ ] Manual TUI smoke test with:
  - [ ] Normal streaming response.
  - [ ] Anthropic reasoning response.
  - [ ] Empty response.
  - [ ] Provider error.
  - [ ] Queued prompts.
  - [ ] Tool execution and cancellation.
  - [ ] Session reload after restart.
