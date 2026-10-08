#include "anthropic_thinking.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace qcode {
namespace anthropic {
namespace {

std::string to_lower_ascii(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::optional<std::string> normalize_effort(
    const std::optional<std::string>& effort) {
  if (!effort.has_value()) return std::nullopt;
  std::string value = to_lower_ascii(*effort);
  const auto space = value.find_first_of(" \t");
  if (space != std::string::npos) value.erase(space);
  if (value.empty() || value == "off" || value == "none" ||
      value == "disabled" || value == "false") {
    return std::nullopt;
  }
  return value;
}

// Budget-form fallback when the variant config sets no budget_tokens.
int default_budget(std::string_view effort) {
  if (effort == "minimal" || effort == "low") return 2000;
  if (effort == "medium") return 8000;
  if (effort == "xhigh") return 20000;
  if (effort == "max") return 24000;
  return 16000;
}

}  // namespace

AnthropicThinkingPlan anthropic_plan_thinking(
    const std::optional<std::string>& thinking_type,
    const std::optional<std::string>& thinking_display,
    const std::optional<std::string>& reasoning_effort,
    const std::optional<int>& budget_tokens,
    std::optional<int> max_tokens) {
  AnthropicThinkingPlan plan;
  const std::string type = to_lower_ascii(thinking_type.value_or(""));
  int max = max_tokens.value_or(4096);
  const auto effort = normalize_effort(reasoning_effort);

  if (type == "disabled" || type == "none") {
    plan.max_tokens = max;
    return plan;
  }
  if (type == "adaptive") {
    // Adaptive thinking is always on (these models reject type:disabled);
    // an effort-less turn simply sends no output_config. budget_tokens is
    // ignored by adaptive models, so a configured one never leaks through.
    plan.adaptive = true;
    plan.thinking_on = true;
    plan.effort = effort;
    plan.display = thinking_display.has_value() && !thinking_display->empty()
                       ? *thinking_display
                       : std::string("summarized");
  } else {
    int budget = budget_tokens.value_or(0);
    if (budget <= 0 && effort.has_value()) budget = default_budget(*effort);
    if (budget > 0) {
      // Anthropic requires budget_tokens >= 1024 and max_tokens > budget_tokens.
      plan.budget_tokens = std::max(budget, 1024);
      plan.thinking_on = true;
      max = std::max(max, *plan.budget_tokens + 1024);
    }
  }
  plan.max_tokens = max;
  return plan;
}

}  // namespace anthropic
}  // namespace qcode
