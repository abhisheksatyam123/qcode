#pragma once

#include <qcode/types/embedding_options.h>
#include <qcode/types/generate_options.h>
#include "providers/base_provider_client.h"

#include <nlohmann/json.hpp>

namespace ai {
namespace openai {

class OpenAIRequestBuilder : public providers::RequestBuilder {
 public:
  explicit OpenAIRequestBuilder(bool use_responses = false)
      : use_responses_(use_responses) {}

  nlohmann::json build_request_json(const GenerateOptions& options) override;
  nlohmann::json build_request_json(const EmbeddingOptions& options) override;
  httplib::Headers build_headers(
      const providers::ProviderConfig& config) override;

 private:
  bool use_responses_;
};

}  // namespace openai
}  // namespace ai