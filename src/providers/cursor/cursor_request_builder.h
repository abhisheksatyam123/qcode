#pragma once

#include "ai/types/embedding_options.h"
#include "ai/types/generate_options.h"
#include "http/http_request_handler.h"
#include "providers/base_provider_client.h"

#include <nlohmann/json.hpp>
#include <string>

namespace ai {
namespace cursor {

// Builds Cursor (protobuf / connect-es) requests.
//
// Unlike OpenAI/Anthropic, Cursor's wire format is binary protobuf wrapped in a
// connect-es envelope (1-byte flag + 4-byte big-endian length + payload). We do
// NOT reuse the JSON-based providers::RequestBuilder flow; the JSON methods here
// are stubs so we can still satisfy the interface, while the Cursor-specific
// methods build the real proto bodies.
class CursorRequestBuilder : public providers::RequestBuilder {
 public:
  nlohmann::json build_request_json(const GenerateOptions& options) override;
  nlohmann::json build_request_json(const EmbeddingOptions& options) override;
  httplib::Headers build_headers(const providers::ProviderConfig& config) override;

  // ---- Cursor-specific protobuf builders (return connect-enveloped bytes) ----

  // aiserver.v1.AiService/GetUsableModels -> empty request.
  std::string build_get_usable_models_request() const;

  // agent.v1.AgentService/RunSSE -> AgentRunRequest with a single user turn.
  std::string build_agent_run_request(const GenerateOptions& options) const;
};

}  // namespace cursor
}  // namespace ai
