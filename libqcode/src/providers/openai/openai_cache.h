#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace qcode {
namespace openai {

bool is_claude_cache_model(const std::string& model);
bool is_opencode_gpt5_cache_model(const std::string& model);
void apply_opencode_message_caching(nlohmann::json& messages);

}  // namespace openai
}  // namespace qcode
