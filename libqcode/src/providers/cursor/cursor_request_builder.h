#pragma once

#include <qcode/core/embedding_options.h>
#include <qcode/core/generate_options.h>
#include "http/http_request_handler.h"
#include "providers/base_provider_client.h"

#include <nlohmann/json.hpp>
#include <string>

namespace qcode {
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

  // Connect-es envelope helpers (return RAW AgentRunRequest protobuf; the
  // client wraps once).
  // ---- Cursor-specific protobuf builders ----
  // Returns the RAW AgentRunRequest protobuf (no connect envelope).

  // aiserver.v1.AiService/GetUsableModels -> empty request.
  std::string build_get_usable_models_request() const;

  // agent.v1.AgentService/RunSSE -> AgentRunRequest with a single user turn.
  std::string build_agent_run_request(const GenerateOptions& options) const;

  // AgentClientMessage { run_request = AgentRunRequest }.
  std::string build_agent_client_run_message(
      const GenerateOptions& options) const;

  // AgentClientMessage { exec_client_message = ExecClientMessage with
  // request_context_result success }. Cursor requires this reply before it
  // will emit assistant text_delta frames.
  std::string build_request_context_reply(
      uint32_t exec_id,
      const std::string& exec_id_str,
      const std::string& workspace_path) const;
};

}  // namespace cursor
}  // namespace qcode
