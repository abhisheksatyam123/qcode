#pragma once

#include <nlohmann/json.hpp>

namespace qcode {
namespace openai {

// Chat Completions JSON -> OpenAI Responses API payload.
nlohmann::json to_responses_request(const nlohmann::json& request);

}  // namespace openai
}  // namespace qcode
