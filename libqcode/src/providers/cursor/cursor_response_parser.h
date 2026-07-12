#pragma once

#include "providers/base_provider_client.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ai {
namespace cursor {

// Parsed view of one AgentServerMessage connect frame.
struct AgentStreamEvent {
  enum class Kind {
    kHeartbeat,
    kTextDelta,
    kTurnEnded,
    // token_delta after assistant text — Cursor often omits turn_ended on
    // short turns, so this is the practical end-of-turn signal.
    kPostTextTokenDelta,
    kRequestContext,
    kOther,
    kError,
  };

  Kind kind = Kind::kOther;
  std::string text;          // for kTextDelta / kTurnEnded
  std::string error;         // for kError
  uint32_t exec_id = 0;      // for kRequestContext
  std::string exec_id_str;   // for kRequestContext (optional)
};

// Incrementally re-assembles connect-es frames from an HTTP/2 body stream.
class ConnectFrameBuffer {
 public:
  // Append raw bytes; returns any newly completed frame payloads (inner proto,
  // envelope header stripped). Heartbeat / empty frames are still returned so
  // callers can classify them.
  std::vector<std::string> feed(std::string_view chunk);

 private:
  std::string pending_;
};

// Parses Cursor's protobuf responses.
//
// - aiserver.v1.AiService responses are raw protobuf (no connect envelope).
// - agent.v1.AgentService/RunSSE responses are application/connect+proto:
//   a binary stream of connect envelopes, each carrying AgentServerMessage.
//   Legacy SSE (data: base64(...)) is still accepted.
class CursorResponseParser : public providers::ResponseParser {
 public:
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

  // Full body (binary connect stream or legacy SSE) -> assistant text.
  static std::string parse_agent_stream_body(const std::string& sse_body);

  // Classify a single AgentServerMessage payload (connect envelope already
  // stripped).
  static AgentStreamEvent classify_agent_payload(const std::string& payload);
};

}  // namespace cursor
}  // namespace ai
