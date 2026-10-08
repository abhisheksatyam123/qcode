#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace qcode {
namespace anthropic {

// Claude 4.6 (and everything newer) replaced `thinking:{type:"enabled",
// budget_tokens}` with `thinking:{type:"adaptive", display:"summarized"}` plus
// `output_config:{effort}` — the budget is ignored by these models and, without
// an explicit `display`, the reasoning text never reaches the client (probed
// live: type=disabled -> HTTP 400 "use adaptive and output_config.effort";
// type=enabled -> thinking text empty, ~60 thinking tokens regardless of budget).
struct AnthropicThinkingPlan {
  bool adaptive = false;       // wire form: adaptive (+effort) vs budget_tokens
  bool thinking_on = false;    // thinking blocks appear in the response/history
  std::optional<std::string> effort;  // output_config.effort (adaptive only)
  std::optional<int> budget_tokens;   // legacy thinking budget (non-adaptive)
  int max_tokens = 4096;              // final max_tokens to send on the wire
};

// Claude version parsed out of a model id (major/minor). `matched` is false
// when no `claude-<major>[.<minor>]` token is present.
struct AnthropicModelVersion {
  bool matched = false;
  int major = 0;
  int minor = 0;
  bool supports_xhigh() const { return major >= 5 || (major == 4 && minor >= 7); }
};

// True when `model_id` is a Claude model that takes the adaptive thinking form.
// A "-thinking" variant suffix is stripped first; a Claude id whose version
// cannot be parsed falls back to adaptive (newer models drop the budget form).
bool anthropic_uses_adaptive_thinking(std::string_view model_id);

AnthropicModelVersion anthropic_model_version(std::string_view model_id);

// Maps a qcode reasoning effort ("low".."max") onto the effort a model accepts:
// xhigh only exists from 4.7/5.x on, unknown efforts clamp to high.
std::optional<std::string> anthropic_effective_effort(
    const std::optional<std::string>& effort, std::string_view model_id);

AnthropicThinkingPlan anthropic_plan_thinking(
    std::string_view model_id,
    const std::optional<std::string>& reasoning_effort,
    const std::optional<int>& budget_tokens,
    std::optional<int> max_tokens);

}  // namespace anthropic
}  // namespace qcode
