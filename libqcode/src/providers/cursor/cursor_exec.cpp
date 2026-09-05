#include "cursor_exec.h"

#include "cursor_proto.h"

#include <qcode/core/logger.h>
#include <qcode/core/tool.h>
#include <qcode/tools/bash_tool.h>

#include <chrono>
#include <sstream>
#include <utility>

namespace qcode {
namespace cursor {
namespace {

// ExecServerMessage oneof -> ExecClientMessage oneof. Pi args are +1.
uint32_t result_field_for(uint32_t args_field) {
  switch (args_field) {
    case 45:
      return 46;  // pi_read
    case 46:
      return 47;  // pi_bash
    case 47:
      return 48;  // pi_edit
    case 48:
      return 49;  // pi_write
    case 49:
      return 50;  // pi_grep
    case 50:
      return 51;  // pi_find
    case 51:
      return 52;  // pi_ls
    case 52:
      return 55;  // mini_swe_agent_bash
    default:
      return args_field;
  }
}

// SandboxPolicy.Type.TYPE_INSECURE_NONE — required on ShellStream start so
// the event oneof is a real message, not an empty nested field.
constexpr uint64_t kSandboxInsecureNone = 1;

std::string wrap_exec_client(const CursorExecRequest& req,
                             uint32_t result_field,
                             const std::string& result,
                             int elapsed_ms = 0,
                             bool include_exec_id = true) {
  std::string exec_msg = proto::varint_field(1, req.id);
  // Official stream serializer sets only id + shellStream. exec_id on those
  // frames has been a suspect for AgentService dropping the event.
  if (include_exec_id && !req.exec_id.empty()) {
    exec_msg += proto::bytes_field(15, req.exec_id);
  }
  if (elapsed_ms > 0) {
    exec_msg += proto::varint_field(39, static_cast<uint64_t>(elapsed_ms));
  }
  exec_msg += proto::bytes_field(result_field, result);
  return proto::bytes_field(2, exec_msg);
}

std::string shell_stream_start_payload(const CursorExecRequest& req) {
  // Echo requested_sandbox_policy (ShellArgs field 9) when present.
  const auto args = proto::parse_fields(req.args);
  auto policy = proto::field_string(args, 9);
  if (policy.empty()) {
    policy = proto::varint_field(1, kSandboxInsecureNone);
  }
  return proto::bytes_field(4, proto::bytes_field(1, policy));
}

std::string shell_success_payload(const std::string& command,
                                  const std::string& cwd,
                                  int exit_code,
                                  const std::string& output,
                                  int elapsed_ms) {
  return proto::bytes_field(1, command) + proto::bytes_field(2, cwd) +
         proto::varint_field(
             3, static_cast<uint64_t>(exit_code < 0 ? 1 : exit_code)) +
         proto::bytes_field(5, output) + proto::bytes_field(6, "") +
         proto::varint_field(7, static_cast<uint64_t>(elapsed_ms));
}

std::string oneof_success(const std::string& payload) {
  return proto::bytes_field(1, payload);
}

std::string oneof_error(const std::string& message, uint32_t error_field = 2) {
  return proto::bytes_field(error_field, proto::bytes_field(1, message));
}

JsonValue run_bash(const std::string& command,
                   const std::string& cwd,
                   int timeout_ms,
                   const std::string& workspace,
                   std::shared_ptr<std::atomic<bool>> abort_flag) {
  JsonValue args;
  args["command"] = command;
  args["workdir"] = cwd.empty() ? workspace : cwd;
  args["timeout"] = timeout_ms > 0 ? timeout_ms : 120000;
  args["description"] = "cursor exec";
  ToolExecutionContext ctx;
  ctx.workspace = workspace;
  ctx.abort_flag = std::move(abort_flag);
  return BashTool::execute(args, ctx);
}

std::string bash_output(const JsonValue& result) {
  if (result.contains("error")) {
    if (result["error"].is_string()) return result["error"].get<std::string>();
    return result["error"].dump();
  }
  if (result.contains("output") && result["output"].is_string()) {
    return result["output"].get<std::string>();
  }
  return result.dump();
}

bool bash_failed(const JsonValue& result) {
  if (result.contains("error")) return true;
  if (result.contains("metadata") && result["metadata"].contains("exit")) {
    return result["metadata"]["exit"].get<int>() != 0;
  }
  return false;
}

int bash_exit(const JsonValue& result) {
  if (result.contains("metadata") && result["metadata"].contains("exit")) {
    return result["metadata"]["exit"].get<int>();
  }
  return result.contains("error") ? 1 : 0;
}

CursorExecReply finish(CursorExecReply reply, const CursorExecRequest& req,
                       uint32_t result_field, const std::string& result,
                       int elapsed_ms = 0) {
  reply.client_messages.push_back(
      wrap_exec_client(req, result_field, result, elapsed_ms));
  if (reply.tool_name.empty()) {
    reply.tool_name = CursorExec::tool_name_for_field(req.args_field);
  }
  if (reply.tool_call_id.empty()) reply.tool_call_id = req.exec_id;
  return reply;
}

CursorExecReply unsupported(const CursorExecRequest& req, const std::string& why) {
  CursorExecReply reply;
  reply.is_error = true;
  reply.tool_name = CursorExec::tool_name_for_field(req.args_field);
  reply.tool_call_id = req.exec_id;
  reply.result["error"] = why;
  return finish(std::move(reply), req, result_field_for(req.args_field),
                oneof_error(why));
}

// ── classic shell / pi bash ──────────────────────────────────────────

CursorExecReply handle_shell(const CursorExecRequest& req,
                             const std::string& workspace,
                             std::shared_ptr<std::atomic<bool>> abort_flag,
                             bool stream) {
  const auto fields = proto::parse_fields(req.args);
  const auto command = proto::field_string(fields, 1);
  auto cwd = proto::field_string(fields, 2);
  if (cwd.empty()) cwd = workspace;
  int timeout_ms = static_cast<int>(proto::field_varint(fields, 3, 120000));
  if (timeout_ms <= 0) timeout_ms = 120000;
  if (timeout_ms < 1000) timeout_ms *= 1000;  // seconds -> ms
  const auto tool_call_id = proto::field_string(fields, 4);
  const auto description = proto::field_string(fields, 15);

  CursorExecReply reply;
  reply.tool_name = stream ? "bash" : "bash";
  reply.tool_call_id = tool_call_id.empty() ? req.exec_id : tool_call_id;
  reply.arguments["command"] = command;
  reply.arguments["workdir"] = cwd;
  if (!description.empty()) reply.arguments["description"] = description;

  LOG_INFO("Cursor exec {} command={}", stream ? "shell_stream" : "shell",
           command.substr(0, 240));
  const auto started = std::chrono::steady_clock::now();
  const auto bash = run_bash(command, cwd, timeout_ms, workspace, abort_flag);
  const auto elapsed_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count());
  auto output = bash_output(bash);
  constexpr size_t kMaxStreamChars = 80000;
  if (output.size() > kMaxStreamChars) {
    output = output.substr(0, kMaxStreamChars) + "\n[truncated]";
  }
  const int exit_code = bash_exit(bash);
  reply.result["output"] = output;
  reply.result["exit"] = exit_code;
  reply.is_error = bash.contains("error");
  LOG_INFO("Cursor exec {} done exit={} out_len={} ms={}",
           stream ? "shell_stream" : "shell", exit_code, output.size(),
           elapsed_ms);

  if (stream) {
    // start was already BidiAppend'd by the client so AgentService does not
    // sit waiting while the command runs.
    if (!output.empty()) {
      const auto stdout_ev =
          proto::bytes_field(1, proto::bytes_field(1, output));
      reply.client_messages.push_back(
          wrap_exec_client(req, 14, stdout_ev, 0, /*include_exec_id=*/false));
    }
    const uint64_t code =
        static_cast<uint64_t>(exit_code < 0 ? 1 : exit_code);
    const auto exit_body =
        proto::varint_field(1, code) + proto::bytes_field(2, cwd) +
        proto::varint_field(4, 0) +  // aborted
        proto::varint_field(6, static_cast<uint64_t>(elapsed_ms));
    reply.client_messages.push_back(wrap_exec_client(
        req, 14, proto::bytes_field(3, exit_body), elapsed_ms,
        /*include_exec_id=*/false));
    // Stream events + stream_close were not enough: AgentService still
    // idled out. Also send the unary ShellResult the field-2 tool uses.
    reply.client_messages.push_back(wrap_exec_client(
        req, 2,
        oneof_success(shell_success_payload(command, cwd, exit_code, output,
                                            elapsed_ms)),
        elapsed_ms));
    return reply;
  }

  const auto success =
      shell_success_payload(command, cwd, exit_code, output, elapsed_ms);
  const uint32_t result_field = result_field_for(req.args_field);
  if (reply.is_error) {
    return finish(std::move(reply), req, result_field,
                  proto::bytes_field(5, proto::bytes_field(1, output)),
                  elapsed_ms);
  }
  return finish(std::move(reply), req, result_field, oneof_success(success),
                elapsed_ms);
}

CursorExecReply handle_pi_bash(const CursorExecRequest& req,
                               const std::string& workspace,
                               std::shared_ptr<std::atomic<bool>> abort_flag) {
  const auto fields = proto::parse_fields(req.args);
  const auto command = proto::field_string(fields, 1);
  CursorExecReply reply;
  reply.tool_name = "bash";
  reply.tool_call_id = req.exec_id;
  reply.arguments["command"] = command;
  const auto bash = run_bash(command, workspace, 120000, workspace, abort_flag);
  const auto output = bash_output(bash);
  reply.result["output"] = output;
  reply.is_error = bash_failed(bash);
  const auto inner = proto::bytes_field(1, output);
  const auto result =
      reply.is_error ? proto::bytes_field(2, proto::bytes_field(1, output))
                     : proto::bytes_field(1, inner);
  return finish(std::move(reply), req, 47, result);
}


CursorExecReply handle_hook(const CursorExecRequest& req) {
  uint32_t case_field = req.hook_request_field;
  if (case_field == 0) case_field = 4;  // pre_tool_use
  const auto response = proto::bytes_field(case_field, std::string{});
  const auto result = proto::bytes_field(1, response);
  CursorExecReply reply;
  reply.tool_name = "hook";
  reply.tool_call_id = req.exec_id;
  reply.arguments["hook"] = static_cast<int>(case_field);
  reply.result["ok"] = true;
  return finish(std::move(reply), req, 27, result);
}

CursorExecReply handle_allowlist(const CursorExecRequest& req, bool allow) {
  CursorExecReply reply;
  reply.tool_name = CursorExec::tool_name_for_field(req.args_field);
  reply.tool_call_id = req.exec_id;
  reply.result["allowlisted"] = allow;
  return finish(std::move(reply), req, req.args_field,
                proto::varint_field(1, allow ? 1 : 0));
}

CursorExecReply handle_mcp_state(const CursorExecRequest& req) {
  CursorExecReply reply;
  reply.tool_name = "mcp_state";
  reply.tool_call_id = req.exec_id;
  reply.result["servers"] = nlohmann::json::array();
  return finish(std::move(reply), req, 36, oneof_success(std::string{}));
}

}  // namespace

std::string CursorExec::shell_stream_start_message(
    const CursorExecRequest& request) {
  return wrap_exec_client(request, 14, shell_stream_start_payload(request), 0,
                          /*include_exec_id=*/false);
}

std::string CursorExec::stream_close_message(uint32_t exec_id) {
  // AgentClientMessage { 5: ExecClientControlMessage {
  //   1: ExecClientStreamClose { 1: id } } }
  const auto close = proto::varint_field(1, exec_id);
  return proto::bytes_field(5, proto::bytes_field(1, close));
}

CursorExecRequest CursorExec::from_event(const AgentStreamEvent& ev) {
  CursorExecRequest req;
  req.id = ev.exec_id;
  req.exec_id = ev.exec_id_str;
  req.args_field = ev.exec_field;
  req.args = ev.exec_args;
  req.hook_request_field = ev.hook_request_field;
  return req;
}

const char* CursorExec::tool_name_for_field(uint32_t args_field) {
  switch (args_field) {
    case 2:
    case 14:
    case 46:
    case 52:
      return "bash";
    case 3:
    case 48:
      return "write";
    case 4:
      return "delete";
    case 5:
    case 49:
      return "grep";
    case 7:
    case 29:
    case 45:
      return "read";
    case 8:
    case 51:
      return "ls";
    case 9:
      return "diagnostics";
    case 11:
      return "mcp";
    case 20:
    case 43:
      return "fetch";
    case 27:
      return "hook";
    case 36:
      return "mcp_state";
    case 41:
      return "allowlist";
    case 47:
      return "edit";
    case 50:
      return "find";
    default:
      return "exec";
  }
}

CursorExecReply CursorExec::handle(
    const CursorExecRequest& request,
    const std::string& workspace,
    std::shared_ptr<std::atomic<bool>> abort_flag) {
  LOG_INFO("Cursor exec: field={} id={} exec_id={}", request.args_field,
           request.id, request.exec_id);
  switch (request.args_field) {
    case 2:
    case 52:
      return handle_shell(request, workspace, abort_flag, /*stream=*/false);
    case 14:
      return handle_shell(request, workspace, abort_flag, /*stream=*/true);
    case 46:
      return handle_pi_bash(request, workspace, abort_flag);
    case 27:
      return handle_hook(request);
    case 36:
      return handle_mcp_state(request);
    case 41:
    case 43:
      return handle_allowlist(request, /*allow=*/true);
    case 42:
      return handle_allowlist(request, /*allow=*/false);
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 20:
    case 29:
    case 45:
    case 47:
    case 48:
    case 49:
    case 50:
    case 51:
      return unsupported(request,
                         "only bash is enabled; rerun this as a shell command");
    default:
      return unsupported(request, "unsupported cursor exec field " +
                                      std::to_string(request.args_field));
  }
}

}  // namespace cursor
}  // namespace qcode
