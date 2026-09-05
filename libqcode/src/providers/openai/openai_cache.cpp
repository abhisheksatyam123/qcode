#include "openai_cache.h"

#include <unordered_set>
#include <vector>

namespace qcode {
namespace openai {
namespace {

const nlohmann::json kEphemeralCache = {{"type", "ephemeral"}};

void mark_message_cache_control(nlohmann::json& message) {
  if (!message.is_object() || !message.contains("content")) {
    return;
  }
  auto& content = message["content"];
  if (content.is_null()) {
    return;
  }
  if (content.is_string()) {
    content = nlohmann::json::array({nlohmann::json{
        {"type", "text"},
        {"text", content.get<std::string>()},
        {"cache_control", kEphemeralCache},
    }});
    return;
  }
  if (content.is_array() && !content.empty() && content.back().is_object()) {
    content.back()["cache_control"] = kEphemeralCache;
  }
}

}  // namespace

bool is_claude_cache_model(const std::string& model) {
  return model.find("claude") != std::string::npos ||
         model.find("anthropic") != std::string::npos;
}

bool is_opencode_gpt5_cache_model(const std::string& model) {
  return model.find("gpt-5") != std::string::npos &&
         model.find("gpt-5-chat") == std::string::npos;
}

void apply_opencode_message_caching(nlohmann::json& messages) {
  if (!messages.is_array() || messages.empty()) {
    return;
  }
  std::vector<std::size_t> system_idx;
  std::vector<std::size_t> other_idx;
  for (std::size_t i = 0; i < messages.size(); ++i) {
    if (messages[i].value("role", "") == "system") {
      system_idx.push_back(i);
    } else {
      other_idx.push_back(i);
    }
  }
  std::unordered_set<std::size_t> mark;
  for (std::size_t i = 0; i < system_idx.size() && i < 2; ++i) {
    mark.insert(system_idx[i]);
  }
  const auto other_n = other_idx.size();
  for (std::size_t n = 0; n < other_n && n < 2; ++n) {
    mark.insert(other_idx[other_n - 1 - n]);
  }
  for (const auto i : mark) {
    mark_message_cache_control(messages[i]);
  }
}

}  // namespace openai
}  // namespace qcode
