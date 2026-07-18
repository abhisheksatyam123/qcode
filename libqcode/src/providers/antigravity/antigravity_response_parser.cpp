#include "antigravity_response_parser.h"

#include <qcode/transform/gemini_transform.h>
#include "providers/openai/openai_response_parser.h"
#include "utils/response_utils.h"

namespace ai {
namespace antigravity {

GenerateResult AntigravityResponseParser::parse_success_completion_response(
    const nlohmann::json& response) {
  openai::OpenAIResponseParser parser;
  return parser.parse_success_completion_response(
      gemini::normalize_gemini_response(response));
}

GenerateResult AntigravityResponseParser::parse_error_completion_response(
    int status_code, const std::string& body) {
  return utils::parse_standard_error_response(
      "Antigravity", status_code, body);
}

EmbeddingResult AntigravityResponseParser::parse_success_embedding_response(
    const nlohmann::json&) {
  return EmbeddingResult("Antigravity does not support embeddings");
}

EmbeddingResult AntigravityResponseParser::parse_error_embedding_response(
    int, const std::string&) {
  return EmbeddingResult("Antigravity does not support embeddings");
}

}  // namespace antigravity
}  // namespace ai
