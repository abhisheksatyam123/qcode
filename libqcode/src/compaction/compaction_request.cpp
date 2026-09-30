#include <qcode/compaction/compaction_request.h>

#include <qcode/generation/turn_prefix.h>
#include <qcode/transform/provider_transform.h>

namespace qcode {
namespace compaction {

namespace {

// Delivered as the FINAL user message of the replayed conversation — never
// as a replacement system prompt — so the conversation's own system prompt,
// tools and message prefix stay in front of it and the provider's warm
// prefix cache is reused (dsh compaction-basic summarizer rule).
const char* kDirective =
    "You are now acting as a compaction engine for this AI coding assistant. "
    "Condense the conversation ABOVE into a structured checkpoint (handoff "
    "packet) so a fresh session can resume the work with no loss of "
    "essential context.\n\n"
    "Output EXACTLY the Markdown structure below: keep both sections, in "
    "order. Use terse bullets, not prose paragraphs. Write \"(none)\" for an "
    "empty section — never drop a section.\n\n"
    "## Tasks\n"
    "- [the next actionable task(s) and the verified/current state of each]\n\n"
    "## Systems\n"
    "- [concise facts that matter: code, APIs, data structures, files, "
    "commands, verified evidence, blockers, and user preferences]\n\n"
    "Rules:\n"
    "- Preserve exact file paths, commands, error strings, identifiers, "
    "numeric values, and user instructions — especially corrections and "
    "feedback.\n"
    "- Preserve the next actionable task, pending jobs, blockers, and "
    "verified evidence.\n"
    "- Do NOT mention this summarization request or that the context was "
    "compacted.\n"
    "- Output only the checkpoint text. Do NOT call any tool and take no "
    "other action.\n";

}  // namespace

std::string directive() { return kDirective; }

GenerateOptions build_cache_replay_request(const CacheReplayInput& input,
                                           const std::string& wire_model,
                                           const std::string& provider_id,
                                           Messages history) {
  const bool plan_mode = (input.agent_mode == "plan");
  const bool is_subagent =
      input.is_subagent || (input.agent_mode == "subagent");

  static const std::vector<ProviderInfo> kNoProviders;

  GenerateOptions opts;
  opts.model = wire_model;
  // Byte-identical to ChatBus's system prompt for the same inputs.
  opts.system = build_turn_system_prompt(input.system_prompt, plan_mode,
                                         is_subagent,
                                         input.providers ? *input.providers
                                                         : kNoProviders);
  // Replay the history through the same normalization pass the live turn
  // uses, then append the directive as the trailing user message.
  opts.messages =
      ProviderTransform::normalize_messages(std::move(history),
                                            Model(wire_model, provider_id));
  opts.messages.push_back(Message::user(directive()));
  // Same tool schemas as the last routed request — tool definitions are
  // part of the cacheable prefix (and Zen requires bash/read declared).
  if (input.enable_tools) {
    opts.tools = build_turn_tools(!plan_mode && !is_subagent,
                                  input.vision_supported);
  }
  opts.session_id = input.session_id;
  opts.max_steps = 1;  // single wire call; tools are declared, never executed
  return opts;
}

}  // namespace compaction
}  // namespace qcode
