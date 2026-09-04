#include "antigravity_request_builder.h"

#include <qcode/transform/gemini_transform.h>
#include <qcode/core/logger.h>
#include "providers/openai/openai_request_builder.h"

namespace qcode {
namespace antigravity {

nlohmann::json AntigravityRequestBuilder::build_request_json(
    const GenerateOptions& options) {
  // Reuse the OpenAI message/tool/parameter assembly, then translate to the
  // Gemini generateContent shape and wrap in the Antigravity envelope.
  openai::OpenAIRequestBuilder inner;
  auto openai_req = inner.build_request_json(options);
  auto gemini_req = qcode::gemini::convert_openai_to_gemini(openai_req);
  return qcode::gemini::wrap_antigravity_envelope(
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

  // Old antigravity/<semver> UAs only provision *-tiered Flash ids; the
  // hub aidev_client UA is what unlocks -low/-medium/-high (3.8 included).
  headers.emplace(
      "User-Agent",
      "antigravity/hub/2.8.0 (aidev_client; os_type=linux; arch=amd64)");

  for (const auto& [key, value] : config.extra_headers) {
    headers.emplace(key, value);
  }
  return headers;
}

}  // namespace antigravity
}  // namespace qcode
