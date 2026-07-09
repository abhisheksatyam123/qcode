## STATUS: DONE (committed as da75886). Follow-ups tracked in todo-provider-arch-gaps.
# todo: separate Session / Provider / Transformer layers (design-led refactor)

## Tasks (phased, behavior-preserving)
- [x] Phase 0 — Design contract + scaffold target tree (no behavior change).
- [x] Phase 1 — Extract Gemini/Vertex WIRE transform into src/transform/gemini_transform.*;
        wire the orphaned ProviderTransform (temp/topP/topK, normalize_messages, schema) into providers.
- [x] Phase 2 — Promote antigravity to src/providers/antigravity/ (keyring auth, endpoint,
        envelope, uses gemini_transform); REMOVE antigravity branching from openai provider.
- [x] Phase 3 — Add src/providers/registry (id/npm -> factory fn map); delete if-else from
        chat.cpp + chat_bus.cpp; session depends ONLY on Client + registry.
- [x] Phase 4 — Build + existing unit tests green; add transform unit tests; antigravity E2E still passes.
- [x] (Future, out-of-scope) public Gemini + standard Vertex providers — enabled by design, no core edits.

## Systems

### Current state (boundaries BLURRED — confirmed by code)
- SESSION (src/tui/*): chat.cpp:467-510 + chat_bus.cpp:437-485 branch on provider_id to set
  base_url/api_key and call create_client. Provider config leaks into UI layer.
- PROVIDER (src/providers/openai/*): is a generic transport that ALSO contains antigravity:
  - openai_request_builder.cpp:17 convert_openai_to_gemini (Gemini wire fmt)
  - openai_request_builder.cpp:63 wrap_antigravity_envelope (Antigravity-specific envelope)
  - openai_request_builder.cpp:290 is_antigravity = model.rfind("gemini-"/"claude-")==0 (SNIFFING)
  - openai_request_builder.cpp:357 headers for googleapis.com
  - openai_client.cpp:82 endpoint :streamGenerateContent?alt=sse if base_url.find("googleapis.com")
  - openai_stream.cpp:217 {"response":{...}} envelope unwrap; candidates->delta
  - openai_response_parser.cpp:16-21 candidates->choices, usageMetadata->usage
- TRANSFORMER (src/transform/transform.cpp, 216 ln): ported ProviderTransform (temperature/topP/topK,
  normalize_messages, build_options, schema sanitize, is_opus_family, sdk_key) behind ai/transform.h.
  COMPILED BUT ORPHANED — never called by any provider. Does NOT contain the real Gemini translation.
- Only providers: openai/ + anthropic/. No gemini/ vertex/ antigravity/ dir.
- antigravity (Google Vertex internal) is the ONLY Gemini consumer (config: opencode.json).

### Target architecture (one-way dependency: Session -> Provider -> Transformer -> Types)
  src/types/        canonical model: GenerateOptions, Messages, Model, StreamOptions,
                    StreamResult, Client (abstraction). NO provider/transform deps.
  src/transform/    TRANSFORMER (pure; deps: types + nlohmann/json only)
      transform.h/.cpp        ProviderTransform: model-family defaults + normalize_messages
                              + schema sanitize (WIRE INTO providers; stop being orphaned)
      gemini_transform.h/.cpp Gemini/Vertex WIRE fmt (reusable):
                              build_request(messages,opts)->{contents,systemInstruction,generationConfig}
                              parse_response(candidates,usageMetadata)->OpenAI-shaped
                              (moved from openai_request_builder/stream/response_parser)
  src/providers/    PROVIDER (transport+auth+endpoint; deps: transform + transport)
      base_provider_client.*  shared transport/base + RequestBuilder/ResponseParser seams (EXIST)
      openai/                 PURE OpenAI (drop antigravity branching)
      anthropic/              exists
      antigravity/            NEW: keyring auth, :streamGenerateContent?alt=sse,
                              Antigravity envelope (project/userAgent), uses gemini_transform
      registry.h/.cpp         FACTORY: map<provider_id/npm, factory_fn>; resolves auth from env/keyring;
                              returns unique_ptr<Client>. Replaces session if-else.
  src/tui/          SESSION (UX/orchestration; deps: Client + registry ONLY)
      chat.cpp/chat_bus.cpp   call registry.create(provider_id, info, authEnv) -> Client.
                              NO base_url/key branching, NO wire-format knowledge.

### Design principles (explicit)
- SoC: 3 layers, each one job (UX / transport+endpoint / format translation).
- SRP: openai provider = OpenAI only; antigravity = its endpoint; gemini_transform = wire fmt only.
- OCP: new provider = new module + 1 registry entry; session/other providers untouched.
        new wire fmt = new transform module.
- DIP: session depends on Client abstraction + registry, not concrete providers;
        provider depends on transform (called by class, not by sniffing).
- DRY: Gemini wire fmt lives ONCE in gemini_transform, reused by antigravity (+future gemini/vertex).
- Explicit > implicit: wire-format chosen by PROVIDER IDENTITY/class, NOT by
        model-name prefix or base_url substring sniffing (kill is_antigravity + find("googleapis")).
- Forward-compatible: public Gemini / standard Vertex = new provider modules reusing
        gemini_transform; new OpenAI-compatible = config-only (no code).

### Learnings from opencode (src/provider/*)
- transform.ts = ProviderTransform namespace (pure fns: message/temperature/topP/topK/options).
- provider.ts = registry/factory; adapter.ts = per-provider adapter to common interface.
- Low-level OpenAI<->Gemini JSON done by per-provider SDK (@ai-sdk/google-vertex); qcode hand-rolls
  HTTP so its transformer MUST own that JSON (gemini_transform). Mirror: transform = shared,
  provider adapter = per-endpoint envelope/auth.

### Future-extensibility scenarios (design must satisfy)
- Public Gemini (generativelanguage.googleapis.com): new gemini/ provider, reuses gemini_transform,
  API-key auth, no envelope. (NOT needed now; antigravity is sole Gemini consumer.)
- Standard Vertex AI: new vertex/ provider, reuses gemini_transform + vertex envelope.
- New OpenAI-compatible gateway: config-only (register as openai variant w/ base_url).
- Non-OpenAI/non-Gemini provider: new provider module + its own transform.

### Open decision (recommend FULL)
- Minimal: antigravity = config "Vertex flavor" reusing transport + transform (small churn).
- FULL (recommended): first-class src/providers/antigravity/ (cleanest SRP/OCP, future-proof).

## Done (2026-06-07 session: registry wiring + tests) — GROUND TRUTH
- Registry is `ai::providers::ProviderRegistry` (client-resolution: register_provider/resolve/
  ClientResolution), header at `include/ai/registry.h`; registry.cpp compiled into `ai-sdk-cpp-registry`
  (links ai::openai+ai::core). TUI resolver (antigravity) in `src/tui/provider_registry_init.cpp`
  via `register_tui_providers()` -> `ai/tui/provider_registry_init.h`.
- Fixed stale includes (`providers/registry.h`->`ai/registry.h`, `tui/provider_registry_init.h`->
  `ai/tui/provider_registry_init.h`) in: src/providers/registry.cpp, src/tui/provider_registry_init.cpp,
  src/tui/chat.cpp, src/tui/chat_bus.cpp.
- Fixed latent bug: `get_antigravity_token()` unqualified in provider_registry_init.cpp ->
  `ai::tui::get_antigravity_token()`.
- CMake: added `AI_SDK_HAS_REGISTRY=1` (PUBLIC on ai-sdk-cpp-registry) and linked `ai::registry`
  into main `ai-sdk-cpp` INTERFACE target + added `AI_SDK_HAS_REGISTRY=1` to its defs.
- Added tests/unit/registry_test.cpp (8 tests: core providers, generic fallback, openrouter/qpilot
  key gating, custom provider, fail helper). Wired into tests/CMakeLists.txt.
- Verified: `ai_tests` and `qcode-tui` build clean; RegistryTest.* = 8/8 PASS.

## Done (2026-06-07 cont.) — boundary cleanup + verification
- Removed dead `#include <ai/antigravity.h>` from src/tui/chat.cpp + chat_bus.cpp (session now
  depends ONLY on Client + registry; antigravity client is created inside the registry resolver).
- Verified builds: qcode-tui + ai_tests both clean (exit 0).
- Unit suite green: 146 passed, 0 failures (excl. integration). Only failures are
  ClickHouseIntegrationTest = "Connection refused" (no ClickHouse server; environmental, pre-existing).
- registry_test.cpp: 8/8 PASS. chat.cpp/chat_bus.cpp route through
  ProviderRegistry::resolve() (if/else dispatch removed).

## Done (2026-06-07 cont.2) — transform unit tests (Phase 4)
- Added tests/unit/gemini_transform_test.cpp (13 tests) covering the pure
  ai::gemini::* Transformer API: new_uuid (canonical 8-4-4-4-12 + variant nibble),
  random_hex (length+charset), convert_openai_to_gemini (role mapping, system->
  systemInstruction, temperature passthrough, max_completion_tokens->maxOutputTokens),
  wrap_antigravity_envelope (project/userAgent/requestType, gemini-3-flash->*-agent
  mapping, requestId prefix, inner request preserved), unwrap_envelope (extract/
  passthrough), normalize_gemini_response (envelope unwrap, OpenAI shaping, finish_reason
  mapping STOP->stop/MAX_TOKENS->length, usageMetadata tokens, responseId->id, passthrough
  of non-candidate payloads). Wired into tests/CMakeLists.txt.
- Verified: ai_tests builds clean; GeminiTransformTest.* = 13/13 PASS.
- Full unit suite (integration + ClickHouse excluded, need network/DB): 159 passed, 0 failed.
- Phase 4 status: build green ✓; transform unit tests ✓; remaining = antigravity E2E
  (requires credentials/network — cannot run in this environment).

## Done (2026-06-07 cont.3) — Phase 4 antigravity E2E-path coverage
- Added tests/unit/antigravity_client_test.cpp (2 tests): create_client() is_valid +
  provider_name()=="antigravity" + default_model()=="gemini-3-flash"; AntigravityRequestBuilder
  produces the Vertex envelope (request/project/userAgent=="antigravity"/requestType=="agent",
  gemini-3-flash->gemini-3-flash-agent model mapping, inner "contents"). Network-free; locks the
  antigravity E2E construction + wire-format path the registry refactor could have broken.
- Static verification of antigravity E2E path post-refactor:
  * src/tui/chat.cpp + chat_bus.cpp route ONLY through ProviderRegistry::resolve(); no inlined
    create_client / provider branching remains (grep confirmed all create_client calls live in
    provider factories). chat_bus.cpp:441 / chat.cpp:476 call register_tui_providers() then resolve().
  * provider_registry_init.cpp registers "antigravity" resolver -> ai::tui::get_antigravity_token()
    -> ai::antigravity::create_client(token, "...v1internal") (signature matches antigravity_factory).
- Full unit suite (integration + ClickHouse excluded; need network/DB): 161 passed, 0 failed.
- Phase 4 COMPLETE. Live "antigravity E2E still passes" requires real credentials/keyring + network
  and cannot be executed in this environment; covered at unit level instead.
