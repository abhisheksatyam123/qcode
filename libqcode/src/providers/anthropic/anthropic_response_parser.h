#pragma once

#include <qcode/core/generate_options.h>
#include "providers/internal/base_provider_client.h"

#include <nlohmann/json.hpp>

namespace qcode {
namespace anthropic {

class AnthropicResponseParser : public providers::ResponseParser {
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
  static FinishReason parse_stop_reason(const std::string& reason);
};

// Normalized input-side token counts from an Anthropic `usage` object.
// Anthropic's input_tokens EXCLUDES cache reads/writes; qcode uses OpenAI
// semantics: prompt = full prompt size, cached = subset served from cache.
struct AnthropicInputUsage {
  int prompt_tokens = 0;
  int cached_prompt_tokens = 0;
  bool present = false;  // any input-side field was in the object
};
AnthropicInputUsage normalize_input_usage(const nlohmann::json& usage);

}  // namespace anthropic
}  // namespace qcode