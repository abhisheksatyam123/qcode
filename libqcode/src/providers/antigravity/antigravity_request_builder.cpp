#include "antigravity_request_builder.h"

#include <qcode/transform/gemini_transform.h>
#include <qcode/logger/logger.h>
#include "providers/openai/openai_request_builder.h"

namespace ai {
namespace antigravity {

nlohmann::json AntigravityRequestBuilder::build_request_json(
    const GenerateOptions& options) {
  // Reuse the OpenAI message/tool/parameter assembly, then translate to the
  // Gemini generateContent shape and wrap in the Antigravity envelope.
  openai::OpenAIRequestBuilder inner;
  auto openai_req = inner.build_request_json(options);
  auto gemini_req = ai::gemini::convert_openai_to_gemini(openai_req);
  return ai::gemini::wrap_antigravity_envelope(
      gemini_req, options.model, project_id_);
}

nlohmann::json AntigravityRequestBuilder::build_request_json(
    const EmbeddingOptions& options) {
  (void)options;
  return nlohmann::json::object();
}

httplib::Headers AntigravityRequestBuilder::build_headers(
    const providers::ProviderConfig& config) {
  httplib::Headers headers;

  if (!config.api_key.empty()) {
    headers.emplace(config.auth_header_name,
                    config.auth_header_prefix + config.api_key);
  }

  headers.emplace("User-Agent", "antigravity/1.15.8 linux/amd64");

  for (const auto& [key, value] : config.extra_headers) {
    headers.emplace(key, value);
  }
  return headers;
}

}  // namespace antigravity
}  // namespace ai
