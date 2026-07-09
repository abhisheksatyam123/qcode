## STATUS: PARKED — deferred follow-ups from provider-arch; NOT started. Current active task moved to todo-scrolling-speed.
# todo: provider-arch follow-up gaps (after registry/transform commit da75886)

## Context
Commit da75886 completed Phases 0-4 of the provider-arch refactor: standalone
`ai::providers::ProviderRegistry` (client-resolution factory), antigravity promoted
to its own provider, gemini_transform wired into core, and unit tests for
registry (8) / gemini_transform (13) / antigravity client (2). Build green
(qcode-tui + ai_tests), 161 unit tests pass. This note tracks what is NOT done.

## Completed (for reference)
- Registry wired + `AI_SDK_HAS_REGISTRY` defined; linked into main ai-sdk-cpp.
- chat.cpp/chat_bus.cpp route through ProviderRegistry::resolve(); inlined
  provider dispatch removed; dead ai/antigravity.h include dropped from session.
- antigravity resolver (TUI) + gemini_transform envelope path implemented/tested.

## Gaps remaining

### G1 — Live runtime never executed (highest risk)
- qcode-tui was NOT launched; no TTY/config/creds in this environment.
- Antigravity auth+streaming (keyring -> Google token -> network) unverified.
- OpenCode Zen ("opencode") auth specifics unverified (generic resolver passes
  api_key="unused" and relies on api_url; behavior preserved, not run live).
- Action: build in real env + launch; registry now surfaces resolve() errors
  via report_error / ErrorOccurred (visible, not silent).

### G2 — register_tui_providers() called per generation
- chat.cpp:476 and chat_bus.cpp:441 both call register_tui_providers() on every
  request. Idempotent (map overwrite) but redundant. Hoist to a one-time init at
  app startup for cleanliness.

### G3 — Orphaned ProviderTransform still unwired
- src/transform/transform.cpp (ProviderTransform: temp/topP/topK, normalize_messages,
  schema sanitize) is compiled but never called by any provider (per original todo
  Phase 1). Either wire it into providers or mark intentionally future-only.

### G4 — TUI resolver not unit-tested
- register_tui_providers() / the antigravity resolver live in src/tui and are NOT
  linked into ai_tests (needs keyring/env). Registry dispatch for the "antigravity"
  id is only covered indirectly (CustomProviderCanBeRegistered). Add a TUI-side test
  or a resolver-level test if the keyring path can be stubbed.

### G5 — Silent fallback for unknown provider ids
- ProviderRegistry::resolve() falls back to the generic "opencode" resolver for ANY
  unknown id (using configured api_url, else Zen endpoint with "unused" key). A
  misconfigured provider id in opencode.json will not error -- it silently uses the
  generic OpenAI-compatible client. Consider an explicit provider allowlist /
  availability gate, or error when api_url is empty and id != known provider.

### G6 — Design/todo mismatch (availability API never built)
- The original plan (and an early context packet) described an availability registry
  (is_provider_available / allowed_providers / runtime allowlist). The implemented
  registry is RESOLUTION-ONLY. If availability gating was a requirement, it is unmet.

### G7 — Integration tests not run here
- Tool-calling / multi-step / ClickHouse / OpenAI / Anthropic integration suites are
  parametrized and SKIPPED without credentials/network. Not executed in this env.

## Recommended next step
Start with G1 (launch in real env) since it gates confidence in everything else;
then G2 (one-time init) and G5 (fallback policy) are small, safe cleanups.
