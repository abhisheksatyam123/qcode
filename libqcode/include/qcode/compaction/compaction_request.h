#pragma once

#include <qcode/config/provider_info.h>
#include <qcode/core/generate_options.h>

#include <string>
#include <vector>

namespace qcode {
namespace compaction {

// Cache-replaying summarizer request (DeepSeek Harness compaction rule).
//
// The auxiliary compaction call must be a genuine PREFIX of the last routed
// request so the provider's warm prompt cache is reused instead of
// invalidated:
//   - the conversation's own effective system prompt (NOT a replacement
//     summarizer system prompt),
//   - the same dispatched tool schemas, byte-for-byte,
//   - the history messages replayed verbatim,
//   - the compaction directive appended as the FINAL user message.
// Only the directive and the summary output are then uncached.

struct CacheReplayInput {
  // Raw turn system prompt, exactly as handed to generation (TUI
  // handle_slash_command / server generate route).
  std::string system_prompt;
  // "orchestrator" | "plan" | "subagent" (GenerationContext::agent_mode).
  std::string agent_mode = "orchestrator";
  // Child session (qcode::session::is_child_session), mirroring ChatBus.
  bool is_subagent = false;
  // Whether the last turn dispatched tools (server always true; TUI toggle).
  bool enable_tools = true;
  // Vision capability of the routed model (affects the tool set).
  bool vision_supported = false;
  // Provider list used for the system-prompt catalog section.
  const std::vector<ProviderInfo>* providers = nullptr;
  // Session id, so prompt_cache_key-style transports key identically to the
  // last routed request.
  std::string session_id;
};

// The compaction directive, delivered as the final user message.
std::string directive();

// Build the summarizer request. `wire_model` and `provider_id` must be the
// values the last turn routed with (prepare_provider_call wire model).
GenerateOptions build_cache_replay_request(const CacheReplayInput& input,
                                           const std::string& wire_model,
                                           const std::string& provider_id,
                                           Messages history);

}  // namespace compaction
}  // namespace qcode
