# todo: TUI polish — queuing, thinking-token visualization, opencode-aligned Stats

## Status
ACTIVE — grounded investigation complete; design to be locked. References:
opencode source at /home/abhi/projects/opencode (stats model in
`src/process/session/stats.ts`; TUI stats view in
`src/surface/cli/cmd/tui/routes/session/index.tsx`). OPEN: the exact queuing
bugs to fix (the queue already works today — header chip + deque + drain).

## Goal
Bring qcode-tui UX up to opencode parity for: (1) prompt queuing, (2) thinking /
reasoning-token visualization (chat + stats), (3) the Stats window (token counts
with reasoning + cache, context-window breakdown, per-tool breakdown), plus
related polish.

## Systems (grounded in code)

### Token / usage data model
- `include/ai/types/usage.h:5-16` `struct Usage { prompt_tokens, completion_tokens,
  total_tokens }` — NO reasoning / cache fields yet.
- `include/ai/tui/contract/event.h:109-116` `TokenUsageUpdated` event Payload:
  `{ prompt_tokens, completion_tokens, total_tokens }`. Published from
  chat_bus.cpp; `src/tui/store.cpp:216` subscribes and sets
  `state.total_prompt_tokens / total_completion_tokens / total_tokens`.
- `src/tui/chat.cpp:345-347` ALSO directly increments the same three state totals
  from `gen_result.usage` (second accumulation path — keep consistent).

### ChatState (include/ai/tui/state.h:30-70)
Token/usage mirrors: `total_prompt_tokens`, `total_completion_tokens`,
`total_tokens`, `tool_call_count`, `total_tool_time_ms`. Queue mirror:
`queued_prompts`. MISSING: reasoning tokens, cache read/write tokens, per-tool
breakdown, context-component breakdown.

### Thinking / reasoning today
- Provider config supports thinking: `src/transform/transform.cpp:109-111`
  Anthropic extended thinking (`type=enabled`, `budget_tokens`); o-series via
  `reasoning_effort`/`budget` (transform.cpp:96-101).
- TUI has NO thinking path: `append_chat_message(role, text)` only
  (store.h:43 / store.cpp:39); no `thinking|reasoning` handling in src/tui.
- Provider streams do NOT surface thinking deltas to the TUI (verified: no
  thinking/reasoning emit in src/providers stream parsers).

### Queue today
- `include/ai/tui/store.h:33-37,63-64`: `enqueue_prompt` / `dequeue_prompt` /
  `append_to_last_queued_prompt` / `queue_size` / `has_queued_prompt` over
  `std::deque<std::string> prompt_queue_` guarded by `queue_mutex_`; updates
  `state.queued_prompts` (int).
- `src/tui/store.cpp:116-156` impl; `src/tui/main.cpp:141-142` logs
  queue_remaining on spawn; drain runs next queued prompt on generation finish.
- `src/tui/views.cpp:367-385` header queue chip: `⧖ N` (yellow) when
  `queued_prompts > 0`.

### Stats tab today (src/tui/views.cpp ~561-615, tab_selected==2)
- Hardcoded `hard_limit = 200000`; usage bar (used/hard_limit %).
- SESSION STATS: Prompt/Completion/Total Tokens, Tool Calls, Tool Time,
  Estimated Cost (rough: openrouter 2.50/10.00, else 3.00/15.00 per 1M).
- NO reasoning, NO cache, NO context breakdown, NO per-tool breakdown.

### opencode stats model (TARGET — src/process/session/stats.ts)
- `TokenCounts { input, output, reasoning, cache:{read, write} }`;
  `tokenTotal = input+output+reasoning+cache.read+cache.write`;
  `promptTokenTotal = input+cache.read+cache.write`.
- `contextWindowStats()` -> `{ estimatedTotal, components[], tools }` where
  components are `{ name, tokens, detail? }` e.g. "system prompt", "user input",
  "assistant text", "assistant reasoning", "tool calls (N calls)"; tools =
  per-tool token totals (TokenAttribution).
- TUI `SessionStatsWithContext` (session/index.tsx): TokenCountsLike +
  ContextWindowStats `{ components[]{name,tokens,pct,detail}, tools[]
  {name,calls,inputTokens,outputTokens,totalTokens}, avgCallTokens,
  totalToolCallTokens }`. Views: chat | todo | files | stats (4 tabs). Stats tab
  auto-refetches every 30s.

## Tasks

### A. Stats window -> opencode parity
- [ ] Extend `Usage` (usage.h) with `reasoning_tokens`, `cache_read_tokens`,
      `cache_write_tokens`; populate from anthropic (`usage.cache_read_input_tokens`
      etc.) + openai/o-series responses.
- [ ] Extend `TokenUsageUpdated::Payload` (event.h) with the same 3 new fields;
      publish them from chat_bus.cpp; apply in store.cpp (set new ChatState fields,
      keep chat.cpp:345 path in sync).
- [ ] Add ChatState fields: `reasoning_tokens`, `cache_read_tokens`,
      `cache_write_tokens` (`shared_ptr<int>`), reset on new session.
- [ ] Replace hardcoded `hard_limit = 200000` (views.cpp:561) with the model's
      actual context window (from `providers_list[...]` model def).
- [ ] Rewrite Stats tab (views.cpp ~561-615) to opencode layout:
      - Token counts: Input/Prompt, Output/Completion, Reasoning, Cache
        (read/write), Total — mirror `TokenCounts`.
      - Context-window usage bar + component breakdown (system prompt, user
        input, assistant text, assistant reasoning, tool calls) with % like
        opencode `ContextWindowStats`.
      - Per-tool breakdown: calls, input/output/total tokens (tools[]).
      - Estimated cost: per-model pricing (drop the 2-rate hack).
- [ ] (Stretch) Track context-component token estimates in qcode (system prompt
      size, etc.) to fill the breakdown; if too costly, show token totals only.

### B. Thinking / reasoning-token visualization
- [ ] Add a reasoning/thinking message part to the chat model (chat_history is
      `pair<role,text>`; add a part type or a separate `thinking` message list).
      New store method e.g. `append_thinking(text)` / `append_chat_message(role, text, kind)`.
- [ ] Capture thinking deltas in provider streams (anthropic_stream thinking
      block, openai o-series reasoning_summary) and forward to the bus/view as
      they stream (collapsible "thinking" block, like opencode reasoning part).
- [ ] Render thinking block in views.cpp: collapsible, dimmed, distinct from the
      assistant answer (icon e.g. thought bubble).
- [ ] Surface reasoning token count in Stats (covered by A).

### C. Queuing fixes / improvements
- [ ] Enumerate current queuing issues (queue works today: chip + deque + drain).
      Confirm with user the specific bugs: ordering, drain-on-error, dup prompts,
      concurrency, preview of queued prompts.
- [ ] Improve queue UX: header chip already exists (⧖ N); consider a queued-prompt
      preview (last/first item) and per-queued status.
- [ ] Ensure `queued_prompts` mirror stays correct across append/delete/drain
      (store.cpp already updates on each op) and is shown in Stats (queue depth).

### D. Other polish ("and so on")
- [ ] Session duration / request count in Stats (opencode shows latency/requests).
- [ ] Reset all usage/queue mirrors on new session (state init).
- [ ] Build + manual verify against opencode layout; screenshot compare.

## Open questions
- Exact queuing bugs to fix (need user repro / input).
- Full context-component + per-tool breakdown requires estimating token sizes of
  system prompt / messages in C++ (opencode uses string-length heuristics).
  Feasible but heavier — ok to start with token totals + a simpler breakdown?
- Which providers actually return cache_read/write + reasoning counts (anthropic
  yes; openai o-series partial; gemini?) — scope the Usage extension to those.

## Out of scope
- Provider/auth changes (done in provider-arch, commit da75886).
- Scroll refactor (done in todo-scrolling-speed, committed 972cad9).
