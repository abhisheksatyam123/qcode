#pragma once

#include <qcode/core/embedding_options.h>
#include <qcode/core/generate_options.h>
#include "providers/internal/base_provider_client.h"

#include <nlohmann/json.hpp>

namespace qcode {
namespace anthropic {

class AnthropicRequestBuilder : public providers::RequestBuilder {
 public:
  AnthropicRequestBuilder() = default;
  explicit AnthropicRequestBuilder(bool is_oauth) : is_oauth_(is_oauth) {}

  nlohmann::json build_request_json(const GenerateOptions& options) override;
  nlohmann::json build_request_json(const EmbeddingOptions& options) override;
  httplib::Headers build_headers(
      const providers::ProviderConfig& config) override;

  bool is_oauth() const { return is_oauth_; }
  void set_oauth(bool is_oauth) { is_oauth_ = is_oauth; }

 private:
  bool is_oauth_ = false;
};

}  // namespace anthropic
}  // namespace qcode
