#include "cursor_exec.h"

#include "cursor_proto.h"

#include <qcode/core/logger.h>
#include <qcode/core/tool.h>
#include <qcode/tools/bash_tool.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace qcode {
namespace cursor {
namespace {

namespace fs = std::filesystem;

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

std::string resolve_path(const std::string& workspace, const std::string& path) {
  if (path.empty()) return workspace.empty() ? "." : workspace;
  const fs::path p{path};
  if (p.is_absolute()) return p.lexically_normal().string();
  const fs::path root = workspace.empty() ? fs::current_path() : fs::path{workspace};
  return (root / p).lexically_normal().string();
}

std::string read_text_file(const std::string& path, std::string& error) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    error = "file not found";
    return {};
  }
  if (fs::is_directory(path, ec)) {
    error = "path is a directory";
    return {};
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "permission denied";
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string slice_lines(const std::string& text, int offset, int limit,
                        int& total_lines, bool& range_applied) {
  std::vector<std::string> lines;
  std::string line;
  std::istringstream in(text);
  while (std::getline(in, line)) lines.push_back(std::move(line));
  if (!text.empty() && text.back() == '\n') {
    // keep getline's drop of the final newline; empty last line is not added
  }
  total_lines = static_cast<int>(lines.size());
  int start = 0;
  if (offset > 0) {
    start = offset - 1;
    range_applied = true;
  }
  if (start < 0) start = 0;
  if (start > total_lines) start = total_lines;
  int count = limit > 0 ? limit : (total_lines - start);
  if (limit > 0) range_applied = true;
  if (count < 0) count = 0;
  if (start + count > total_lines) count = total_lines - start;
  std::ostringstream out;
  for (int i = 0; i < count; ++i) {
    if (i) out << '\n';
    out << lines[static_cast<size_t>(start + i)];
  }
  if (count > 0 && (text.empty() || text.back() == '\n')) out << '\n';
  return out.str();
}

int count_lines(const std::string& text) {
  if (text.empty()) return 0;
  int n = 1;
  for (char c : text) {
    if (c == '\n') ++n;
  }
  if (!text.empty() && text.back() == '\n') --n;
  return n;
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

// ── read / write / delete / ls ───────────────────────────────────────

CursorExecReply handle_read(const CursorExecRequest& req,
                            const std::string& workspace,
                            bool pi) {
  const auto fields = proto::parse_fields(req.args);
  const auto raw_path = proto::field_string(fields, 1);
  const auto path = resolve_path(workspace, raw_path);
  const int offset = static_cast<int>(proto::field_varint(fields, pi ? 2 : 4, 0));
  const int limit = static_cast<int>(proto::field_varint(fields, pi ? 3 : 5, 0));
  const auto tool_call_id = proto::field_string(fields, 2);

  CursorExecReply reply;
  reply.tool_name = "read";
  reply.tool_call_id = tool_call_id.empty() ? req.exec_id : tool_call_id;
  reply.arguments["path"] = path;
  if (offset) reply.arguments["offset"] = offset;
  if (limit) reply.arguments["limit"] = limit;

  std::string error;
  const auto text = read_text_file(path, error);
  if (!error.empty()) {
    reply.is_error = true;
    reply.result["error"] = error;
    if (pi) {
      return finish(std::move(reply), req, 46, oneof_error(error));
    }
    uint32_t err_field = 2;
    if (error == "file not found") err_field = 4;
    if (error == "permission denied") err_field = 5;
    const auto err = proto::bytes_field(1, path) + proto::bytes_field(2, error);
    return finish(std::move(reply), req, req.args_field,
                  proto::bytes_field(err_field, err));
  }

  int total_lines = 0;
  bool range_applied = false;
  const auto sliced = slice_lines(text, offset, limit, total_lines, range_applied);
  reply.result["output"] = sliced;
  reply.result["total_lines"] = total_lines;

  if (pi) {
    return finish(std::move(reply), req, 46,
                  oneof_success(proto::bytes_field(1, sliced)));
  }
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  auto success = proto::bytes_field(1, path) + proto::bytes_field(2, sliced) +
                 proto::varint_field(3, static_cast<uint64_t>(total_lines)) +
                 proto::varint_field(4, ec ? 0 : size) +
                 proto::varint_field(6, range_applied && limit > 0 &&
                                            offset + limit <= total_lines
                                            ? 0
                                            : 0) +
                 proto::varint_field(8, range_applied ? 1 : 0);
  (void)size;
  return finish(std::move(reply), req, req.args_field, oneof_success(success));
}

CursorExecReply handle_write(const CursorExecRequest& req,
                             const std::string& workspace,
                             bool pi) {
  const auto fields = proto::parse_fields(req.args);
  const auto raw_path = proto::field_string(fields, 1);
  const auto content = proto::field_string(fields, 2);
  const auto path = resolve_path(workspace, raw_path);
  const auto tool_call_id = proto::field_string(fields, 3);

  CursorExecReply reply;
  reply.tool_name = "write";
  reply.tool_call_id = tool_call_id.empty() ? req.exec_id : tool_call_id;
  reply.arguments["path"] = path;
  reply.arguments["bytes"] = static_cast<int>(content.size());

  std::error_code ec;
  fs::create_directories(fs::path{path}.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    reply.is_error = true;
    reply.result["error"] = "write failed";
    const auto err = proto::bytes_field(1, path) + proto::bytes_field(2, "write failed");
    return finish(std::move(reply), req, result_field_for(req.args_field),
                  proto::bytes_field(pi ? 2 : 5, err));
  }
  out << content;
  out.close();
  const int lines = count_lines(content);
  reply.result["output"] = "Wrote " + path;
  reply.result["lines"] = lines;
  if (pi) {
    return finish(std::move(reply), req, 49,
                  oneof_success(proto::bytes_field(1, "Wrote " + path)));
  }
  const auto success =
      proto::bytes_field(1, path) +
      proto::varint_field(2, static_cast<uint64_t>(lines)) +
      proto::varint_field(3, static_cast<uint64_t>(content.size()));
  return finish(std::move(reply), req, 3, oneof_success(success));
}

CursorExecReply handle_delete(const CursorExecRequest& req,
                              const std::string& workspace) {
  const auto fields = proto::parse_fields(req.args);
  const auto raw_path = proto::field_string(fields, 1);
  const auto path = resolve_path(workspace, raw_path);
  const auto tool_call_id = proto::field_string(fields, 2);

  CursorExecReply reply;
  reply.tool_name = "delete";
  reply.tool_call_id = tool_call_id.empty() ? req.exec_id : tool_call_id;
  reply.arguments["path"] = path;

  std::error_code ec;
  if (!fs::exists(path, ec)) {
    reply.is_error = true;
    reply.result["error"] = "file not found";
    return finish(std::move(reply), req, 4,
                  proto::bytes_field(2, proto::bytes_field(1, path)));
  }
  if (fs::is_directory(path, ec)) {
    reply.is_error = true;
    reply.result["error"] = "not a file";
    return finish(std::move(reply), req, 4,
                  proto::bytes_field(3, proto::bytes_field(1, path) +
                                            proto::bytes_field(2, "directory")));
  }
  std::string prev;
  std::string read_err;
  prev = read_text_file(path, read_err);
  const auto size = fs::file_size(path, ec);
  fs::remove(path, ec);
  if (ec) {
    reply.is_error = true;
    reply.result["error"] = ec.message();
    return finish(std::move(reply), req, 4,
                  proto::bytes_field(7, proto::bytes_field(1, path) +
                                            proto::bytes_field(2, ec.message())));
  }
  reply.result["output"] = "Deleted " + path;
  const auto success = proto::bytes_field(1, path) + proto::bytes_field(2, path) +
                       proto::varint_field(3, ec ? 0 : size) +
                       proto::bytes_field(4, prev);
  return finish(std::move(reply), req, 4, oneof_success(success));
}

CursorExecReply handle_ls(const CursorExecRequest& req,
                          const std::string& workspace,
                          bool pi) {
  const auto fields = proto::parse_fields(req.args);
  const auto raw_path = proto::field_string(fields, 1);
  const auto path = resolve_path(workspace, raw_path);
  const int limit = static_cast<int>(proto::field_varint(fields, 2, 0));
  const auto tool_call_id = proto::field_string(fields, 3);

  CursorExecReply reply;
  reply.tool_name = "ls";
  reply.tool_call_id = tool_call_id.empty() ? req.exec_id : tool_call_id;
  reply.arguments["path"] = path;

  std::error_code ec;
  if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) {
    reply.is_error = true;
    reply.result["error"] = "not a directory";
    return finish(std::move(reply), req, result_field_for(req.args_field),
                  oneof_error("not a directory"));
  }

  std::ostringstream listing;
  std::string dirs_bytes;
  std::string files_bytes;
  int num_files = 0;
  int shown = 0;
  for (const auto& entry : fs::directory_iterator(path, ec)) {
    if (ec) break;
    if (limit > 0 && shown >= limit) break;
    const auto name = entry.path().filename().string();
    if (entry.is_directory(ec)) {
      listing << name << "/\n";
      const auto child = proto::bytes_field(1, entry.path().string());
      dirs_bytes += proto::bytes_field(2, child);
    } else {
      listing << name << '\n';
      files_bytes += proto::bytes_field(3, proto::bytes_field(1, name));
      ++num_files;
    }
    ++shown;
  }
  reply.result["output"] = listing.str();
  if (pi) {
    return finish(std::move(reply), req, 52,
                  oneof_success(proto::bytes_field(1, listing.str())));
  }
  const auto node = proto::bytes_field(1, path) + dirs_bytes + files_bytes +
                    proto::varint_field(4, 1) +
                    proto::varint_field(6, static_cast<uint64_t>(num_files));
  return finish(std::move(reply), req, 8,
                oneof_success(proto::bytes_field(1, node)));
}

CursorExecReply handle_pi_edit(const CursorExecRequest& req,
                               const std::string& workspace) {
  const auto fields = proto::parse_fields(req.args);
  const auto raw_path = proto::field_string(fields, 1);
  const auto path = resolve_path(workspace, raw_path);

  CursorExecReply reply;
  reply.tool_name = "edit";
  reply.tool_call_id = req.exec_id;
  reply.arguments["path"] = path;

  std::string error;
  auto text = read_text_file(path, error);
  if (!error.empty()) {
    reply.is_error = true;
    reply.result["error"] = error;
    return finish(std::move(reply), req, 48, oneof_error(error));
  }

  int replacements = 0;
  for (const auto& f : fields) {
    if (f.num != 2 || f.wire != 2) continue;
    const auto edit = proto::parse_fields(f.bytes);
    const auto old_text = proto::field_string(edit, 1);
    const auto new_text = proto::field_string(edit, 2);
    if (old_text.empty()) continue;
    const auto pos = text.find(old_text);
    if (pos == std::string::npos) {
      reply.is_error = true;
      reply.result["error"] = "old_text not found";
      return finish(std::move(reply), req, 48,
                    oneof_error("old_text not found in " + path));
    }
    text.replace(pos, old_text.size(), new_text);
    ++replacements;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    reply.is_error = true;
    reply.result["error"] = "write failed";
    return finish(std::move(reply), req, 48, oneof_error("write failed"));
  }
  out << text;
  const auto msg =
      "Applied " + std::to_string(replacements) + " edit(s) to " + path;
  reply.result["output"] = msg;
  const auto success = proto::bytes_field(1, msg) + proto::bytes_field(2, "") +
                       proto::bytes_field(3, "");
  return finish(std::move(reply), req, 48, oneof_success(success));
}

CursorExecReply handle_grep_or_find(const CursorExecRequest& req,
                                    const std::string& workspace,
                                    bool find,
                                    bool pi) {
  const auto fields = proto::parse_fields(req.args);
  const auto pattern = proto::field_string(fields, 1);
  auto path = proto::field_string(fields, 2);
  if (path.empty()) path = workspace;
  path = resolve_path(workspace, path);
  const auto glob = proto::field_string(fields, 3);
  const bool ignore_case = proto::field_varint(fields, pi ? 4 : 8, 0) != 0;
  const int limit = static_cast<int>(proto::field_varint(fields, pi ? 7 : 10, 0));

  CursorExecReply reply;
  reply.tool_name = find ? "find" : "grep";
  reply.tool_call_id = proto::field_string(fields, 14).empty()
                           ? req.exec_id
                           : proto::field_string(fields, 14);
  reply.arguments["pattern"] = pattern;
  reply.arguments["path"] = path;

  std::ostringstream cmd;
  if (find) {
    cmd << "find " << path << " -name " << std::quoted(pattern);
    if (limit > 0) cmd << " | head -n " << limit;
  } else {
    cmd << "rg --no-heading --line-number --color=never";
    if (ignore_case) cmd << " -i";
    if (!glob.empty()) cmd << " --glob " << std::quoted(glob);
    if (limit > 0) cmd << " -m " << limit;
    cmd << " -- " << std::quoted(pattern) << " " << std::quoted(path);
    cmd << " || grep -RIn";
    if (ignore_case) cmd << " -i";
    cmd << " -- " << std::quoted(pattern) << " " << std::quoted(path);
  }

  const auto bash =
      run_bash(cmd.str(), workspace, 60000, workspace, nullptr);
  const auto output = bash_output(bash);
  reply.result["output"] = output;
  reply.is_error = bash.contains("error");

  const uint32_t result_field = result_field_for(req.args_field);
  if (pi) {
    const auto inner = proto::bytes_field(1, output);
    const auto result = reply.is_error ? oneof_error(output) : oneof_success(inner);
    return finish(std::move(reply), req, result_field, result);
  }

  if (find) {
    return finish(std::move(reply), req, result_field,
                  reply.is_error ? oneof_error(output)
                                 : oneof_success(proto::bytes_field(1, output)));
  }

  // Classic GrepSuccess with a single workspace content bucket.
  std::string matches;
  std::istringstream lines(output);
  std::string line;
  int total = 0;
  while (std::getline(lines, line)) {
    if (line.empty()) continue;
    const auto colon = line.find(':');
    std::string file = path;
    std::string content = line;
    int line_no = 0;
    if (colon != std::string::npos) {
      file = line.substr(0, colon);
      const auto colon2 = line.find(':', colon + 1);
      if (colon2 != std::string::npos) {
        try {
          line_no = std::stoi(line.substr(colon + 1, colon2 - colon - 1));
        } catch (...) {
          line_no = 0;
        }
        content = line.substr(colon2 + 1);
      }
    }
    const auto match = proto::bytes_field(1, file) +
                       proto::bytes_field(
                           2, proto::varint_field(1, static_cast<uint64_t>(line_no)) +
                                  proto::bytes_field(2, content));
    matches += proto::bytes_field(1, match);
    ++total;
  }
  const auto content_result =
      matches + proto::varint_field(2, static_cast<uint64_t>(total)) +
      proto::varint_field(3, static_cast<uint64_t>(total));
  const auto union_result = proto::bytes_field(3, content_result);
  const auto entry = proto::bytes_field(1, workspace) + proto::bytes_field(2, union_result);
  const auto success = proto::bytes_field(1, pattern) + proto::bytes_field(2, path) +
                       proto::bytes_field(3, "content") + proto::bytes_field(4, entry);
  return finish(std::move(reply), req, 5, oneof_success(success));
}

CursorExecReply handle_fetch(const CursorExecRequest& req,
                             const std::string& workspace) {
  const auto fields = proto::parse_fields(req.args);
  const auto url = proto::field_string(fields, 1);
  CursorExecReply reply;
  reply.tool_name = "fetch";
  reply.tool_call_id = proto::field_string(fields, 2);
  if (reply.tool_call_id.empty()) reply.tool_call_id = req.exec_id;
  reply.arguments["url"] = url;
  const auto bash = run_bash(
      "curl -sS -L --max-time 30 -D /tmp/qcode-cursor-fetch.hdr -o /tmp/qcode-cursor-fetch.body " +
          std::string("'") + url +
          "'; echo '---STATUS---'; awk 'BEGIN{s=0} /^HTTP/{s=$2} END{print s}' "
          "/tmp/qcode-cursor-fetch.hdr; echo '---BODY---'; head -c 200000 "
          "/tmp/qcode-cursor-fetch.body",
      workspace, 45000, workspace, nullptr);
  const auto output = bash_output(bash);
  reply.result["output"] = output;
  reply.is_error = bash.contains("error");
  if (reply.is_error) {
    return finish(std::move(reply), req, 20,
                  proto::bytes_field(2, proto::bytes_field(1, url) +
                                            proto::bytes_field(2, output)));
  }
  const auto success = proto::bytes_field(1, url) + proto::bytes_field(2, output) +
                       proto::varint_field(3, 200) + proto::bytes_field(4, "text/plain");
  return finish(std::move(reply), req, 20, oneof_success(success));
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
    case 3:
      return handle_write(request, workspace, /*pi=*/false);
    case 4:
      return handle_delete(request, workspace);
    case 5:
      return handle_grep_or_find(request, workspace, /*find=*/false, /*pi=*/false);
    case 7:
    case 29:
      return handle_read(request, workspace, /*pi=*/false);
    case 8:
      return handle_ls(request, workspace, /*pi=*/false);
    case 20:
      return handle_fetch(request, workspace);
    case 27:
      return handle_hook(request);
    case 36:
      return handle_mcp_state(request);
    case 41:
    case 43:
      return handle_allowlist(request, /*allow=*/true);
    case 42:
      return handle_allowlist(request, /*allow=*/false);
    case 45:
      return handle_read(request, workspace, /*pi=*/true);
    case 46:
      return handle_pi_bash(request, workspace, abort_flag);
    case 47:
      return handle_pi_edit(request, workspace);
    case 48:
      return handle_write(request, workspace, /*pi=*/true);
    case 49:
      return handle_grep_or_find(request, workspace, /*find=*/false, /*pi=*/true);
    case 50:
      return handle_grep_or_find(request, workspace, /*find=*/true, /*pi=*/true);
    case 51:
      return handle_ls(request, workspace, /*pi=*/true);
    default:
      return unsupported(request, "unsupported cursor exec field " +
                                      std::to_string(request.args_field));
  }
}

}  // namespace cursor
}  // namespace qcode
