#pragma once

#include "providers/internal/base_provider_client.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qcode {
namespace cursor {

struct CursorModelInfo {
  std::string id;
  std::string name;
};

// Parsed view of one AgentServerMessage connect frame.
struct AgentStreamEvent {
  enum class Kind {
    kHeartbeat,
    kTextDelta,
    kReasoningDelta,
    kTurnEnded,
    // token_delta after assistant text — Cursor often omits turn_ended on
    // short turns, so this is the practical end-of-turn signal.
    kPostTextTokenDelta,
    kRequestContext,
    // Native AgentService exec (shell/read/write/pi_*). Must be answered via
    // BidiAppend or the server waits and the turn dies on idle-end.
    kExec,
    // KvServerMessage (get_blob / set_blob). Same: no reply, turn stalls.
    kKv,
    kOther,
    kError,
  };

  Kind kind = Kind::kOther;
  std::string text;          // for kTextDelta / kTurnEnded
  std::string error;         // for kError
  uint32_t exec_id = 0;      // for kRequestContext / kExec
  std::string exec_id_str;   // for kRequestContext / kExec (optional)
  uint32_t exec_field = 0;   // ExecServerMessage oneof field number
  std::string exec_args;     // raw args message for that oneof
  uint32_t hook_request_field = 0;  // ExecuteHookRequest oneof, when exec_field=27
  std::string kv_message;    // raw KvServerMessage for kKv
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
  static std::vector<CursorModelInfo> parse_get_usable_models(
      const std::string& body);

  // Full body (binary connect stream or legacy SSE) -> assistant text.
  static std::string parse_agent_stream_body(const std::string& sse_body);

  // Classify a single AgentServerMessage payload (connect envelope already
  // stripped).
  static AgentStreamEvent classify_agent_payload(const std::string& payload);
};

}  // namespace cursor
}  // namespace qcode
