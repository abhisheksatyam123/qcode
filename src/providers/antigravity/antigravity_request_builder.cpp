#include "antigravity_request_builder.h"

#include "ai/gemini_transform.h"
#include "ai/logger.h"
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
  return ai::gemini::wrap_antigravity_envelope(gemini_req, options.model);
}

nlohmann::json AntigravityRequestBuilder::build_request_json(
    const EmbeddingOptions& options) {
  // Antigravity has no embedding endpoint; fall back to the OpenAI shape.
  openai::OpenAIRequestBuilder inner;
  return inner.build_request_json(options);
}

httplib::Headers AntigravityRequestBuilder::build_headers(
    const providers::ProviderConfig& config) {
  httplib::Headers headers;

  if (!config.api_key.empty() && config.api_key != "unused") {
    headers.emplace(config.auth_header_name,
                    config.auth_header_prefix + config.api_key);
  }

  // Antigravity (Google Vertex) specific headers
  headers.emplace("User-Agent", "antigravity/1.107.0 linux/x64");
  headers.emplace("X-Goog-Api-Client",
                  "google-cloud-sdk vscode_cloudshelleditor/0.1");
  headers.emplace("Client-Metadata",
                  "{\"ideType\":\"ANTIGRAVITY\",\"platform\":\"LINUX\","
                  "\"pluginType\":\"GEMINI\"}");

  for (const auto& [key, value] : config.extra_headers) {
    headers.emplace(key, value);
  }
  return headers;
}

}  // namespace antigravity
}  // namespace ai
