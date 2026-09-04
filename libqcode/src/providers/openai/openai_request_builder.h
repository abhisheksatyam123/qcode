#pragma once

#include <qcode/core/embedding_options.h>
#include <qcode/core/generate_options.h>
#include <qcode/transform/provider_transform.h>
#include "providers/base_provider_client.h"

#include <nlohmann/json.hpp>

#include <string>

namespace qcode {
namespace openai {

class OpenAIRequestBuilder : public providers::RequestBuilder {
 public:
  explicit OpenAIRequestBuilder(bool use_responses = false)
      : use_responses_(use_responses) {}

  // Transport flavor derived from the client's base URL so the wire body can
  // mirror upstream opencode per-provider lowering (reasoning placement,
  // usage accounting, token limits).
  void set_base_url(const std::string& base_url) override {
    base_url_ = base_url;
    transport_ = ProviderTransform::chat_transport_for(base_url);
  }

  void set_wire_protocol(const std::string& protocol) override {
    wire_protocol_ = protocol;
  }

  nlohmann::json build_request_json(const GenerateOptions& options) override;
  nlohmann::json build_request_json(const EmbeddingOptions& options) override;
  httplib::Headers build_headers(
      const providers::ProviderConfig& config) override;

 private:
  bool use_responses_;
  std::string wire_protocol_;
  std::string base_url_;
  ProviderTransform::ChatTransport transport_ =
      ProviderTransform::ChatTransport::kCompatible;
};

}  // namespace openai
}  // namespace qcode