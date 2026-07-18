#pragma once

#include <qcode/retry/retry_policy.h>
#include <qcode/types/stream_options.h>
#include "providers/base_provider_client.h"

#include <string>
#include <vector>

namespace qcode {
namespace antigravity {

// Antigravity (Google Vertex / Gemini) provider.
//
// Reuses OpenAIResponseParser (which already understands the Gemini generateContent
// payload via qcode::gemini::normalize_gemini_response) and OpenAIStreamImpl for SSE.
// The Google Vertex endpoints (:generateContent / :streamGenerateContent?alt=sse)
// are fixed here rather than sniffed from the base URL.
class AntigravityClient : public providers::BaseProviderClient {
 public:
  explicit AntigravityClient(
      const std::string& api_key,
      const std::string& base_url =
          "https://daily-cloudcode-pa.sandbox.googleapis.com/v1internal");
  AntigravityClient(const std::string& api_key, const std::string& base_url,
                    const retry::RetryConfig& retry_config);
  AntigravityClient(const std::string& api_key, const std::string& base_url,
                    const std::string& project_id);

  StreamResult stream_text(const StreamOptions& options) override;
  EmbeddingResult embeddings(const EmbeddingOptions& options) override;
  std::string provider_name() const override;
  std::vector<std::string> supported_models() const override;
  bool supports_model(const std::string& model_name) const override;
  std::string config_info() const override;
  std::string default_model() const override;
};

}  // namespace antigravity
}  // namespace qcode
