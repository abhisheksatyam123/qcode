#include "cursor_exec.h"

#include "cursor_proto.h"

#include <qcode/core/generate_options.h>
#include <qcode/core/logger.h>
#include <qcode/core/tool.h>
#include <qcode/tools/bash_tool.h>
#include <qcode/tools/task_tool.h>

#include <chrono>
#include <cstdint>
#include <sstream>
#include <stdexcept>
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
  args["timeout"] = timeout_ms > 0 ? timeout_ms : 900000;
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
  // Builds (cmake/ninja) routinely exceed 2 minutes. A 120s default used to
  // SIGTERM them (exit 15) and then curl killed RunSSE at 300s.
  constexpr int kDefaultShellTimeoutMs = 15 * 60 * 1000;
  int timeout_ms = static_cast<int>(proto::field_varint(fields, 3, 0));
  if (timeout_ms > 0 && timeout_ms < 1000) timeout_ms *= 1000;
  if (timeout_ms < kDefaultShellTimeoutMs) timeout_ms = kDefaultShellTimeoutMs;
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
  const auto bash = run_bash(command, workspace, 900000, workspace, abort_flag);
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

bool looks_like_task_args(const JsonValue& args) {
  if (!args.is_object()) return false;
  const std::string op = args.value("op", "");
  if (op == "spawn" || op == "result" || op == "kill" || op == "pause" ||
      op == "resume" || op == "resurrect" || op == "model" || op == "status" ||
      op == "list") {
    return true;
  }
  return args.contains("prompt") || args.contains("task") ||
         args.contains("subagent_type") || args.contains("objective") ||
         args.contains("agent") || args.contains("background_task_id") ||
         args.contains("run_in_background") ||
         (args.contains("description") && args.contains("prompt"));
}

bool is_printable_text(const std::string& s) {
  if (s.empty()) return false;
  size_t printable = 0;
  for (unsigned char c : s) {
    if (c == '\t' || c == '\n' || c == '\r' || (c >= 32 && c < 127)) {
      ++printable;
    }
  }
  return printable * 10 >= s.size() * 8;
}

bool looks_like_json_object(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                          s[i] == '\r')) {
    ++i;
  }
  return i < s.size() && s[i] == '{';
}

JsonValue try_parse_json_object(const std::string& s) {
  if (!looks_like_json_object(s)) return JsonValue();
  try {
    auto parsed = JsonValue::parse(s);
    if (parsed.is_object()) return parsed;
  } catch (...) {
  }
  return JsonValue();
}

JsonValue flatten_proto_args(const std::string& data, int depth = 0) {
  JsonValue out = JsonValue::object();
  if (depth > 4 || data.empty()) return out;
  const auto fields = proto::parse_fields(data);
  if (fields.empty()) return out;
  for (const auto& f : fields) {
    const std::string key = std::to_string(f.num);
    if (f.wire == 2) {
      JsonValue nested = try_parse_json_object(f.bytes);
      if (!nested.is_null() && nested.is_object()) {
        for (auto it = nested.begin(); it != nested.end(); ++it) {
          if (!out.contains(it.key())) out[it.key()] = it.value();
        }
        continue;
      }
      JsonValue inner = flatten_proto_args(f.bytes, depth + 1);
      if (!inner.empty()) {
        for (auto it = inner.begin(); it != inner.end(); ++it) {
          if (!out.contains(it.key())) out[it.key()] = it.value();
        }
        if (is_printable_text(f.bytes) && !out.contains(key)) {
          out[key] = f.bytes;
        }
        continue;
      }
      if (is_printable_text(f.bytes)) out[key] = f.bytes;
    } else if (f.wire == 0) {
      out[key] = f.varint;
    }
  }
  return out;
}

void promote_task_fields(JsonValue& args) {
  if (!args.is_object()) return;
  auto take = [&](const char* num, const char* named) {
    if (args.contains(named)) return;
    if (args.contains(num) && args[num].is_string() &&
        !args[num].get<std::string>().empty()) {
      args[named] = args[num];
    }
  };
  // Native TaskArgs typically follow the tool property order: description,
  // prompt, subagent_type, model. MCP field 1 is the tool name, field 2 args.
  take("1", "description");
  take("2", "prompt");
  take("3", "subagent_type");
  take("4", "model");
  if ((!args.contains("prompt") || args.value("prompt", "").empty()) &&
      args.contains("description") && args["description"].is_string()) {
    args["prompt"] = args["description"];
  }
  if (args.contains("4") && args["4"].is_number_unsigned() &&
      args["4"].get<uint64_t>() > 0 && !args.contains("run_in_background")) {
    args["run_in_background"] = true;
  }
}

JsonValue parse_task_args_from_exec(const CursorExecRequest& req) {
  JsonValue parsed = try_parse_json_object(req.args);
  if (!parsed.is_null() && looks_like_task_args(parsed)) return parsed;

  const auto fields = proto::parse_fields(req.args);
  std::string mcp_name = proto::field_string(fields, 1);
  std::string mcp_args = proto::field_string(fields, 2);
  if (!mcp_args.empty()) {
    JsonValue from_mcp = try_parse_json_object(mcp_args);
    if (from_mcp.is_null()) {
      from_mcp = flatten_proto_args(mcp_args);
      promote_task_fields(from_mcp);
    }
    const bool named_task = mcp_name == "task" || mcp_name == "Task";
    if (!from_mcp.is_null() && from_mcp.is_object() &&
        (named_task || looks_like_task_args(from_mcp))) {
      if (named_task && !looks_like_task_args(from_mcp) &&
          from_mcp.contains("1")) {
        promote_task_fields(from_mcp);
      }
      if (named_task || looks_like_task_args(from_mcp)) return from_mcp;
    }
  }

  for (const auto& f : fields) {
    if (f.wire != 2 || f.bytes.empty()) continue;
    JsonValue nested = try_parse_json_object(f.bytes);
    if (!nested.is_null() && looks_like_task_args(nested)) return nested;
  }

  JsonValue flat = flatten_proto_args(req.args);
  promote_task_fields(flat);
  if (looks_like_task_args(flat)) return flat;
  return JsonValue();
}

bool maybe_task_exec_field(uint32_t args_field) {
  switch (args_field) {
    case 2:
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 9:
    case 10:
    case 14:
    case 20:
    case 27:
    case 29:
    case 36:
    case 41:
    case 42:
    case 43:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 51:
    case 52:
      return false;
    default:
      return true;  // MCP (11) and unknown Task oneofs
  }
}

std::string task_error_message(const JsonValue& result) {
  if (result.contains("error")) {
    if (result["error"].is_string()) {
      const std::string msg = result["error"].get<std::string>();
      if (!msg.empty()) return msg;
    } else if (!result["error"].is_null()) {
      return result["error"].dump();
    }
  }
  if (result.contains("output") && result["output"].is_string()) {
    const std::string out = result["output"].get<std::string>();
    if (!out.empty()) return out;
  }
  return "task failed";
}

CursorExecReply handle_task(const CursorExecRequest& req,
                            const std::string& workspace,
                            std::shared_ptr<std::atomic<bool>> abort_flag,
                            const GenerateOptions* options,
                            JsonValue args) {
  CursorExecReply reply;
  reply.tool_name = "task";
  reply.tool_call_id = req.exec_id.empty() ? std::to_string(req.id) : req.exec_id;
  reply.arguments = args;

  ToolExecutionContext ctx;
  ctx.workspace = workspace;
  ctx.abort_flag = abort_flag;
  if (options) ctx.subagent_runner = options->subagent_runner;

  const std::string label = args.value(
      "description", args.value("prompt", args.value("task", "")));
  LOG_INFO("Cursor exec task op={} desc={}", args.value("op", "spawn"),
           label.size() > 120 ? label.substr(0, 120) : label);
  const auto started = std::chrono::steady_clock::now();
  auto result = TaskTool::execute(args, ctx);
  const auto elapsed_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count());
  reply.result = result;
  reply.is_error = result.is_object() && result.contains("error");
  const std::string output =
      result.contains("output") && result["output"].is_string()
          ? result["output"].get<std::string>()
          : (reply.is_error ? task_error_message(result) : result.dump());
  const uint32_t result_field = result_field_for(req.args_field);
  if (reply.is_error) {
    return finish(std::move(reply), req, result_field,
                  oneof_error(task_error_message(result)), elapsed_ms);
  }
  return finish(std::move(reply), req, result_field,
                oneof_success(proto::bytes_field(1, output)), elapsed_ms);
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
    std::shared_ptr<std::atomic<bool>> abort_flag,
    const GenerateOptions* options) {
  try {
  LOG_INFO("Cursor exec: field={} id={} exec_id={}", request.args_field,
           request.id, request.exec_id);
  if (maybe_task_exec_field(request.args_field)) {
    auto task_args = parse_task_args_from_exec(request);
    if (!task_args.is_null() && looks_like_task_args(task_args)) {
      return handle_task(request, workspace, abort_flag, options,
                         std::move(task_args));
    }
    if (request.args_field == 11) {
      const auto fields = proto::parse_fields(request.args);
      const std::string mcp_name = proto::field_string(fields, 1);
      LOG_WARN("Cursor exec MCP tool '{}' was not a task payload", mcp_name);
    }
  }
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
  } catch (const std::exception& e) {
    LOG_ERROR("Cursor exec field={} threw: {}", request.args_field, e.what());
    return unsupported(request, std::string("cursor exec failed: ") + e.what());
  }
}

}  // namespace cursor
}  // namespace qcode
