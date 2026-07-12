#pragma once

#include "providers/base_provider_client.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ai {
namespace cursor {

// Parses Cursor's protobuf responses.
//
// - aiserver.v1.AiService responses are raw protobuf (no connect envelope).
// - agent.v1.AgentService/RunSSE responses are SSE: each `data:` line is
//   base64( connect-envelope( AgentRunResponse proto ) ).
class CursorResponseParser : public providers::ResponseParser {
 public:
  // ai::providers::ResponseParser interface (stubs; Cursor uses protobuf,
  // not JSON, so these are unused -- the static helpers above do the real work).
  GenerateResult parse_success_completion_response(
      const nlohmann::json& response) override;
  GenerateResult parse_error_completion_response(
      int status_code, const std::string& body) override;
  EmbeddingResult parse_success_embedding_response(
      const nlohmann::json& response) override;
  EmbeddingResult parse_error_embedding_response(
      int status_code, const std::string& body) override;

  // body = raw protobuf from GetUsableModelsResponse { 1: models[] }.
  static std::vector<std::string> parse_get_usable_models(
      const std::string& body);

  // sse_body = full SSE body from RunSSE. Best-effort extraction of natural
  // language text from AgentRunResponse frames (full schema parsing deferred).
  static std::string parse_agent_stream_body(const std::string& sse_body);
};

}  // namespace cursor
}  // namespace ai
