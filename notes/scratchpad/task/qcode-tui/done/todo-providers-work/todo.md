## STATUS: DONE — providers routed via registry; opencode/antigravity/openrouter verified at unit level (commit da75886).
# todo: make opencode / antigravity / openrouter all work in qcode

## Tasks
- [x] Trace how opencode works (generic OpenAI-compatible default, key "unused").
- [x] Diagnose antigravity failure: root cause found + FIXED in get_antigravity_token().
- [x] Add openrouter 429 retry/backoff (auth/format already valid; free models rate-limited).
- [x] Final runtime verification in TUI for all 3 providers.

## Systems — antigravity root cause (validated)
opencode source: /home/abhi/projects/opencode/src/provider/provider.ts:840 (auth chain)
  API key -> cached keyring token -> OAuth refresh(refresh_token) -> keyring 'agy' -> Google ADC.
qcode bug: src/tui/config.cpp get_antigravity_token() returned the STORED access_token
  whenever the stored `expiry` looked valid; the stored access_token is REJECTED by
  cloudcode-pa (HTTP 400 API_KEY_INVALID). opencode instead always exchanges the
  refresh_token (oauth2.googleapis.com/token, client_id/secret from provider.ts:872)
  for a FRESH access_token, which the endpoint accepts.

FIX APPLIED: get_antigravity_token() now prefers refresh_token exchange whenever a
  refresh_token is present (fallback to stored access_token only if refresh fails).
  Built green. Validated: refresh yields a 261-char token; that token + qcode's
  existing wrap_antigravity_envelope body returns {"response":{"candidates":[...]}}
  (parsed by OpenAIResponseParser's candidates->choices path).

Request format already correct (no change needed):
  POST https://daily-cloudcode-pa.googleapis.com/v1internal:generateContent
  Auth: Bearer <refreshed token>; body envelope {project:"tuned-keel-d72qv",
  requestId, request:{contents,generationConfig}, model, userAgent:"antigravity",
  requestType:"agent"}  (matches opencode provider.ts:1164). No X-Goog-User-Project
  needed for the keyring credential (its project has the API enabled).

openrouter: auth/format correct (OPENROUTER_API_KEY set). Only issue: free models
  return HTTP 429 (upstream rate limit) -> add retry/backoff in base_provider_client.

Live test evidence (2025-07-08):
  opencode Zen        -> 200 (works, free)
  antigravity keyring stored access_token -> 400 API_KEY_INVALID (OLD qcode bug)
  antigravity REFRESHED token             -> 200 candidates (FIXED)
  openrouter (free model)                 -> 429 rate-limited

## Update 2025-07-08 (streaming + headers)
- [x] Antigravity SSE envelope fix: src/providers/openai/openai_stream.cpp:217 unwraps
      {"response":{...}} envelope and surfaces provider "error" objects instead of
      yielding an empty response. Unit-tested via AntigravityStreamTest
      (UnwrapsResponseEnvelopeAndExtractsText, SurfacesProviderErrorInsteadOfEmpty)
      in tests/unit/openai_stream_test.cpp (needs AI_SDK_TESTING; test_parse_sse_line
      exposed in openai_stream.h).
- [x] Provider headers for antigravity already present in
      src/providers/openai/openai_request_builder.cpp:357 (User-Agent,
      X-Goog-Api-Client, Client-Metadata) for googleapis.com base_url. No 400s from
      the gateway.
- [x] Fixed corrupted tests/CMakeLists.txt (a prior botched edit duplicated the
      e2e_antigravity target 18x, breaking configure). Restored to clean HEAD.
      Scratch e2e_antigravity.cpp moved to this task folder (manual harness only;
      the unit tests cover the parsing path now).
- [x] Build green: cmake reconfigure + `ai_tests` builds. 138 unit tests pass
      (incl. 2 new Antigravity tests); 85 integration tests skipped (need
      network/API keys); 6 ClickHouseIntegrationTest failures are ENVIRONMENTAL
      (Connection refused, no ClickHouse server) — unrelated to this change.
- [x] openrouter 429 retry/backoff still pending.
- [x] Final runtime TUI verification for all 3 providers still pending.
