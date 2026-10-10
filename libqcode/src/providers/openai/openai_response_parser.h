#pragma once

#include <qcode/core/generate_options.h>
#include <qcode/core/usage.h>
#include "providers/internal/base_provider_client.h"

#include <nlohmann/json.hpp>

namespace qcode {
namespace openai {

// Pull thinking text from a chat message or stream delta. Muse Spark /
// OpenRouter send `reasoning` as an object and `reasoning_details` with
// types other than "text"; those used to throw or be ignored.
std::string extract_openai_reasoning_text(const nlohmann::json& node);

// Single reader for every usage shape qcode meets, so a model's thinking
// tokens are never lost to a transport difference:
//   chat completions : prompt_tokens / completion_tokens_details.reasoning_tokens
//   responses        : input_tokens  / output_tokens_details.reasoning_tokens
//   gemini/antigravity: promptTokenCount / thoughtsTokenCount
//   anthropic-ish    : cache_creation_input_tokens / thinking_tokens
// Unknown fields stay 0; the caller may estimate from reasoning text.
Usage parse_openai_usage(const nlohmann::json& usage_json);

class OpenAIResponseParser : public providers::ResponseParser {
 public:
  GenerateResult parse_success_completion_response(
      const nlohmann::json& response) override;
  GenerateResult parse_error_completion_response(
      int status_code,
      const std::string& body) override;

 private:
  static FinishReason parse_finish_reason(const std::string& reason);
};

}  // namespace openai
}  // namespace qcode