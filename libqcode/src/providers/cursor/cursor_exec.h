#pragma once

#include "cursor_response_parser.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace qcode {

struct GenerateOptions;

namespace cursor {

// One native AgentService exec the client must answer over BidiAppend.
struct CursorExecRequest {
  uint32_t id = 0;
  std::string exec_id;
  uint32_t args_field = 0;
  std::string args;
  uint32_t hook_request_field = 0;
};

struct CursorExecReply {
  std::string tool_name;
  std::string tool_call_id;
  nlohmann::json arguments = nlohmann::json::object();
  nlohmann::json result = nlohmann::json::object();
  bool is_error = false;
  // One or more AgentClientMessage payloads (shell_stream emits several).
  std::vector<std::string> client_messages;
};

// Executes Cursor ExecServerMessage frames locally and builds the matching
// ExecClientMessage result. An unset oneof looks like empty success and
// stalls the server-side tool loop.
class CursorExec {
 public:
  static CursorExecRequest from_event(const AgentStreamEvent& ev);

  static CursorExecReply handle(
      const CursorExecRequest& request,
      const std::string& workspace,
      std::shared_ptr<std::atomic<bool>> abort_flag = nullptr,
      const GenerateOptions* options = nullptr);

  // AgentService waits for a ShellStream start event before the command
  // finishes. Send this as soon as field 14 arrives, then run the command.
  static std::string shell_stream_start_message(const CursorExecRequest& request);

  // AgentClientMessage.exec_client_control_message.stream_close. Cursor-agent
  // sends this after every exec (unary and stream). Without it, shell_stream
  // stays open and the turn never continues.
  static std::string stream_close_message(uint32_t exec_id);

  static const char* tool_name_for_field(uint32_t args_field);
};

}  // namespace cursor
}  // namespace qcode
