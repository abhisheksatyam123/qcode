#pragma once

#include <optional>
#include <string>

namespace qcode {
namespace anthropic {

// How a Claude (messages) request asks for thinking. The wire form is chosen
// by opencode.json, not by model-id parsing: a model's `"thinking": {"type"}`
// reaches GenerateOptions::thinking_type.
//   "adaptive" -> thinking{type:adaptive, display} + output_config{effort}
//                 (Claude 4.6+: budget_tokens is ignored, and without display
//                 the reasoning text never reaches the client; probed live).
//   "enabled" or unset -> thinking{type:enabled, budget_tokens} when an effort
//                 or budget is set (accepted by every thinking Claude model).
//   "disabled" / "none" -> no thinking field at all.
struct AnthropicThinkingPlan {
  bool adaptive = false;       // wire form: adaptive (+effort) vs budget_tokens
  bool thinking_on = false;    // thinking blocks appear in the response/history
  std::optional<std::string> effort;  // output_config.effort (adaptive only)
  std::optional<int> budget_tokens;   // budget form only
  std::string display;                // adaptive only ("summarized" default)
  int max_tokens = 4096;              // final max_tokens to send on the wire
};

// `reasoning_effort` is sent verbatim (lower-cased): the variant config owns
// which efforts a model takes. "off"/"none"/"disabled" mean no effort.
AnthropicThinkingPlan anthropic_plan_thinking(
    const std::optional<std::string>& thinking_type,
    const std::optional<std::string>& thinking_display,
    const std::optional<std::string>& reasoning_effort,
    const std::optional<int>& budget_tokens,
    std::optional<int> max_tokens);

}  // namespace anthropic
}  // namespace qcode
