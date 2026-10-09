#pragma once

#include <qcode/core/generate_options.h>
#include "providers/internal/base_provider_client.h"

#include <nlohmann/json.hpp>

namespace qcode {
namespace anthropic {

class AnthropicRequestBuilder : public providers::RequestBuilder {
 public:
  AnthropicRequestBuilder() = default;
  explicit AnthropicRequestBuilder(bool is_oauth) : is_oauth_(is_oauth) {}

  // Anthropic thinking form is model-dependent (adaptive + output_config.effort
  // for Claude >=4.6, legacy budget_tokens before that) and is planned by
  // anthropic_plan_thinking() in anthropic_thinking.h.
  nlohmann::json build_request_json(const GenerateOptions& options) override;
  httplib::Headers build_headers(
      const providers::ProviderConfig& config) override;

  bool is_oauth() const { return is_oauth_; }
  void set_oauth(bool is_oauth) { is_oauth_ = is_oauth; }

 private:
  bool is_oauth_ = false;
};

}  // namespace anthropic
}  // namespace qcode
