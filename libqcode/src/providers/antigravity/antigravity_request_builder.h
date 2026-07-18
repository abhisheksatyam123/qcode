#pragma once

#include <qcode/types/embedding_options.h>
#include <qcode/types/generate_options.h>
#include "http/http_request_handler.h"
#include "providers/base_provider_client.h"

#include <nlohmann/json.hpp>
#include <string>

namespace ai {
namespace antigravity {

// Builds Antigravity / Google Vertex (Gemini) requests.
//
// The wire format is OpenAI-shaped internally; we reuse OpenAIRequestBuilder to
// assemble messages/tools/parameters, then translate that JSON through
// ai::gemini::convert_openai_to_gemini + wrap_antigravity_envelope. HTTP headers
// carry the Antigravity/Vertex identity.
class AntigravityRequestBuilder : public providers::RequestBuilder {
 public:
  explicit AntigravityRequestBuilder(std::string project_id = "")
      : project_id_(std::move(project_id)) {}

  nlohmann::json build_request_json(const GenerateOptions& options) override;
  nlohmann::json build_request_json(const EmbeddingOptions& options) override;
  httplib::Headers build_headers(const providers::ProviderConfig& config) override;

 private:
  std::string project_id_;
};

}  // namespace antigravity
}  // namespace ai
