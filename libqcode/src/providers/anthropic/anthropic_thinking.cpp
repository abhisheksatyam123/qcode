#include "anthropic_thinking.h"

#include <algorithm>
#include <cctype>

namespace qcode {
namespace anthropic {
namespace {


// Model ids ride as "<catalog-prefix>/claude-<family>-<major>.<minor>" for
// third-party endpoints, so match "claude-" anywhere in the id (opencode does
// the same). Numeric version: `claude-(?:[a-z]+-)?(\d+)(?:[.@-](\d{1,2}))?(?:[.@-]|$)`
// on the lowercased id — case-insensitive, family segment optional.
bool parse_claude_version(std::string_view lowered,
                          AnthropicModelVersion& out) {
  const std::string_view kNeedle = "claude-";
  size_t search = 0;
  while (true) {
    const size_t start = lowered.find(kNeedle, search);
    if (start == std::string_view::npos) return false;
    size_t pos = start + kNeedle.size();
    // Optional family segment ("opus-", "sonnet-", "haiku-").
    const size_t family_end = lowered.find('-', pos);
    if (family_end != std::string_view::npos && family_end > pos) {
      bool letters = true;
      for (size_t i = pos; i < family_end; ++i) {
        const unsigned char c = static_cast<unsigned char>(lowered[i]);
        if (!std::isalpha(c)) {
          letters = false;
          break;
        }
      }
      if (letters) pos = family_end + 1;
    }
    if (pos >= lowered.size() ||
        !std::isdigit(static_cast<unsigned char>(lowered[pos]))) {
      search = pos;
      continue;
    }
    size_t major_end = pos;
    while (major_end < lowered.size() &&
           std::isdigit(static_cast<unsigned char>(lowered[major_end]))) {
      ++major_end;
    }
    int major = 0;
    for (size_t i = pos; i < major_end; ++i) {
      major = major * 10 + (lowered[i] - '0');
    }
    int minor = 0;
    // Minor: 1-2 digits after "." or "-" (5.5, opus-4-6), followed by a
    // separator or the end — so a date like -20250514 is not a minor.
    if (major_end < lowered.size() &&
        (lowered[major_end] == '.' || lowered[major_end] == '-')) {
      const size_t p = major_end + 1;
      size_t minor_end = p;
      while (minor_end < lowered.size() &&
             std::isdigit(static_cast<unsigned char>(lowered[minor_end]))) {
        ++minor_end;
      }
      const size_t len = minor_end - p;
      const bool terminated =
          minor_end == lowered.size() || lowered[minor_end] == '.' ||
          lowered[minor_end] == '-' || lowered[minor_end] == '@';
      if (len >= 1 && len <= 2 && terminated) {
        for (size_t i = p; i < minor_end; ++i) {
          minor = minor * 10 + (lowered[i] - '0');
        }
      }
    }
    out.matched = true;
    out.major = major;
    out.minor = minor;
    return true;
  }
}

std::string strip_thinking_suffix(std::string_view model_id) {
  std::string id(model_id);
  constexpr std::string_view kSuffix = "-thinking";
  if (id.size() >= kSuffix.size() &&
      id.compare(id.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
    id.resize(id.size() - kSuffix.size());
  }
  return id;
}

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
  if (value == "minimal") return std::string("low");
  if (value != "low" && value != "medium" && value != "high" &&
      value != "xhigh" && value != "max") {
    return std::string("high");
  }
  return value;
}

}  // namespace

AnthropicModelVersion anthropic_model_version(std::string_view model_id) {
  AnthropicModelVersion version;
  const std::string id = strip_thinking_suffix(model_id);
  if (to_lower_ascii(id).find("claude-") == std::string::npos) return version;
  parse_claude_version(to_lower_ascii(id), version);
  return version;
}

bool anthropic_uses_adaptive_thinking(std::string_view model_id) {
  const std::string id = strip_thinking_suffix(model_id);
  const std::string lowered = to_lower_ascii(id);
  if (lowered.find("claude-") == std::string::npos) return false;
  AnthropicModelVersion version;
  if (!parse_claude_version(lowered, version)) return true;  // newer than 4.5
  if (version.major > 4) return true;
  return version.major == 4 && version.minor >= 6;
}

std::optional<std::string> anthropic_effective_effort(
    const std::optional<std::string>& effort, std::string_view model_id) {
  const auto normalized = normalize_effort(effort);
  if (!normalized.has_value()) return std::nullopt;
  if (*normalized == "xhigh" &&
      !anthropic_model_version(model_id).supports_xhigh()) {
    // 4.6 predates xhigh; max is its top effort.
    return std::string("max");
  }
  return normalized;
}

AnthropicThinkingPlan anthropic_plan_thinking(
    std::string_view model_id,
    const std::optional<std::string>& reasoning_effort,
    const std::optional<int>& budget_tokens,
    std::optional<int> max_tokens) {
  AnthropicThinkingPlan plan;
  plan.adaptive = anthropic_uses_adaptive_thinking(model_id);
  int max = max_tokens.value_or(4096);

  if (plan.adaptive) {
    plan.thinking_on = true;
    plan.effort = anthropic_effective_effort(reasoning_effort, model_id);
    if (!plan.effort) {
      const std::string lowered = to_lower_ascii(model_id);
      if (lowered.size() >= 9 &&
          lowered.compare(lowered.size() - 9, 9, "-thinking") == 0) {
        plan.effort = std::string("high");  // "-thinking" variant default
      }
    }
    // Effortless adaptive thinking still needs display:summarized, otherwise the
    // reasoning text is dropped on the wire (probed: display defaults to
    // omitted => empty thinking text).
    if (plan.effort && (*plan.effort == "high" || *plan.effort == "xhigh" ||
                        *plan.effort == "max")) {
      // High-effort turns spend tens of thousands of thinking tokens; the old
      // budget+1024 sizing would truncate the answer.
      max = std::max(max, 32000);
    }
    // budget_tokens is ignored by adaptive models: effort drives thinking, so a
    // value carried over from the legacy path must not leak into the request.
  } else {
    int budget = 0;
    if (budget_tokens.has_value()) {
      budget = *budget_tokens;
    } else {
      const auto effort = normalize_effort(reasoning_effort);
      if (effort.has_value()) {
        budget = *effort == "low"    ? 2000
                 : *effort == "medium" ? 8000
                 : *effort == "max"   ? 24000
                                      : 16000;
      } else if (to_lower_ascii(strip_thinking_suffix(model_id))
                     .find("thinking") != std::string::npos) {
        budget = 8000;  // "-thinking" variant with no explicit effort
      }
    }
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
