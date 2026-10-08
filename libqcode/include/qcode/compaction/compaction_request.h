#pragma once

#include <qcode/config/provider_info.h>
#include <qcode/core/client.h>
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
//   - the history messages replayed verbatim (prepare_turn_history),
//   - the same request parameters: output budget, sampling and the session
//     variant's thinking settings (Anthropic omits signed thinking blocks and
//     invalidates the message cache when thinking settings change),
//   - the compaction directive appended as the FINAL user message.
// Only the last reply, the directive and the summary output are uncached.

struct CacheReplayInput {
  // Raw turn system prompt, exactly as handed to generation (TUI
  // handle_slash_command / server generate route).
  std::string system_prompt;
  // "orchestrator" | "subagent" (GenerationContext::agent_mode).
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
  // opencode.json entry of the routed model (max_tokens, thinking,
  // variants); null for a model outside the catalog.
  const ModelInfo* model = nullptr;
  // Session variant the turns ran with (GenerationContext::reasoning_mode).
  std::string reasoning_mode;
};

// The compaction directive, delivered as the final user message.
std::string directive();

// Project-scoped handoff location. All task data lives in the session's
// workspace root (never the notes vault):
//   <workspace>/scratchpad/handoff-<session_id>.md
// Falls back to the process cwd when the session has no workspace.
std::string handoff_path(const std::string& session_id);

// Write the compaction summary to handoff_path(); returns the path on
// success, empty string on failure.
std::string write_handoff(const std::string& session_id,
                          const std::string& summary);

// Build the summarizer request. `wire_model` and `provider_id` must be the
// values the last turn routed with (prepare_provider_call wire model), and
// `history` the conversation as the turn sends it (prepare_turn_history).
GenerateOptions build_cache_replay_request(const CacheReplayInput& input,
                                           const std::string& wire_model,
                                           const std::string& provider_id,
                                           Messages history);

// Send the summarizer request over the turn's transport: streamed when the
// client streams tool-loop steps (a thinking summary of a long conversation
// can outlast a blocking call's read timeout), else one generate_text().
// Nothing is published: the summary is not chat output.
GenerateResult run_summarizer(Client& client, const GenerateOptions& request);

}  // namespace compaction
}  // namespace qcode
