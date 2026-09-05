#pragma once

#include <qcode/core/generate_options.h>
#include "providers/internal/base_provider_client.h"

#include <nlohmann/json.hpp>

namespace qcode {
namespace openai {

// Pull thinking text from a chat message or stream delta. Muse Spark /
// OpenRouter send `reasoning` as an object and `reasoning_details` with
// types other than "text"; those used to throw or be ignored.
std::string extract_openai_reasoning_text(const nlohmann::json& node);

class OpenAIResponseParser : public providers::ResponseParser {
 public:
  GenerateResult parse_success_completion_response(
      const nlohmann::json& response) override;
  GenerateResult parse_error_completion_response(
      int status_code,
      const std::string& body) override;
  EmbeddingResult parse_success_embedding_response(
      const nlohmann::json& response) override;
  EmbeddingResult parse_error_embedding_response(
      int status_code,
      const std::string& body) override;

 private:
  static FinishReason parse_finish_reason(const std::string& reason);
};

}  // namespace openai
}  // namespace qcode