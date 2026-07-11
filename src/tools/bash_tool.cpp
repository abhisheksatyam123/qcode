#include <random>
#include <csignal>
#include "ai/tools/bash_tool.h"
#include <ai/logger.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <thread>

namespace ai {

// ── Background Registry ──

BackgroundRegistry& BackgroundRegistry::instance() {
  static BackgroundRegistry inst;
  return inst;
}

void BackgroundRegistry::register_task(const std::string& id, int pid,
                                        const std::string& command,
                                        const std::string& cwd,
                                        const std::string& output_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  BackgroundTaskEntry entry;
  entry.id = id;
  entry.pid = pid;
  entry.command = command;
  entry.cwd = cwd;
  entry.start_time = std::chrono::system_clock::now();
  entry.status = "running";
  entry.output_path = output_path;
  tasks_[id] = std::move(entry);
}

void BackgroundRegistry::mark_exited(const std::string& id, int exit_code) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(id);
  if (it != tasks_.end()) {
    it->second.status = "exited";
    it->second.exit_code = exit_code;
    it->second.end_time = std::chrono::system_clock::now();
  }
}

void BackgroundRegistry::mark_killed(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(id);
  if (it != tasks_.end()) {
    it->second.status = "killed";
    it->second.end_time = std::chrono::system_clock::now();
  }
}

void BackgroundRegistry::mark_failed(const std::string& id, const std::string& /*error*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(id);
  if (it != tasks_.end()) {
    it->second.status = "failed";
    it->second.end_time = std::chrono::system_clock::now();
  }
}

std::optional<BackgroundTaskEntry> BackgroundRegistry::get_task(const std::string& id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(id);
  if (it != tasks_.end()) return it->second;
  return std::nullopt;
}

std::vector<BackgroundTaskEntry> BackgroundRegistry::list_tasks() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<BackgroundTaskEntry> result;
  for (const auto& [k, v] : tasks_) result.push_back(v);
  return result;
}

std::vector<BackgroundTaskEntry> BackgroundRegistry::list_task_details() const {
  return list_tasks();
}

bool BackgroundRegistry::kill_task(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(id);
  if (it == tasks_.end()) return false;
  if (it->second.pid > 0) {
    ::kill(it->second.pid, SIGTERM);
  }
  it->second.status = "killed";
  it->second.end_time = std::chrono::system_clock::now();
  return true;
}

bool BackgroundRegistry::remove_task(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return tasks_.erase(id) > 0;
}

BackgroundRegistry::CleanupResult BackgroundRegistry::cleanup(std::chrono::milliseconds max_age) {
  std::lock_guard<std::mutex> lock(mutex_);
  CleanupResult result{0, 0};
  auto now = std::chrono::system_clock::now();
  auto it = tasks_.begin();
  while (it != tasks_.end()) {
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.start_time);
    if (age > max_age) {
      it = tasks_.erase(it);
      result.removed++;
    } else {
      ++it;
      result.kept++;
    }
  }
  return result;
}

// ── BashTool implementation ──
// Direct port of opencode's BashTool (src/tool/bash/index.ts)

static std::string generate_task_id() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  std::string rand_part = std::to_string(rand() % 1000000);
  return "bg-" + std::to_string(ms) + "-" + rand_part;
}

// Quote a path as a single-quoted POSIX shell literal (escapes embedded
// single quotes) so a working directory containing shell metacharacters
// cannot inject commands into the `cd` prefix we prepend to the command.
static std::string shell_quote_literal(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += '\'';
      out += '\\';
      out += '\'';
      out += '\'';
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

static std::string resolve_cwd(const std::string& workdir) {
  if (workdir.empty()) return std::filesystem::current_path().string();
  std::error_code ec;
  auto p = std::filesystem::absolute(workdir, ec);
  return ec ? std::filesystem::current_path().string() : p.string();
}

static bool is_safe_command(const std::string& command, std::string& warning) {
  // Heredoc validation
  size_t pos = 0;
  while ((pos = command.find("<<", pos)) != std::string::npos) {
    pos += 2;
    // Skip whitespace
    while (pos < command.length() && std::isspace(command[pos])) {
      pos++;
    }
    if (pos >= command.length()) break;
    
    // Extract label
    std::string label;
    char quote = 0;
    if (command[pos] == '\'' || command[pos] == '"') {
      quote = command[pos];
      pos++;
    }
    
    while (pos < command.length()) {
      char c = command[pos];
      if (quote) {
        if (c == quote) {
          pos++;
          break;
        }
        label += c;
      } else {
        if (std::isalnum(c) || c == '_' || c == '-') {
          label += c;
        } else {
          break;
        }
      }
      pos++;
    }
    
    if (label.empty()) continue;
    
    // Check if label appears on its own line in the rest of the command
    bool found = false;
    size_t search_pos = pos;
    while ((search_pos = command.find(label, search_pos)) != std::string::npos) {
      bool start_ok = (search_pos == 0 || command[search_pos - 1] == '\n' || command[search_pos - 1] == '\r');
      size_t end_pos = search_pos + label.length();
      bool end_ok = (end_pos == command.length() || command[end_pos] == '\n' || command[end_pos] == '\r');
      if (start_ok && end_ok) {
        found = true;
        break;
      }
      search_pos += label.length();
    }
    
    if (!found) {
      warning = "Syntax Error: Heredoc marker '" + label + "' is not terminated. "
                "Make sure you close the heredoc on its own line.";
      return false;
    }
  }
  return true;
}



static std::string generate_unique_tool_file_name() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<int> distribution(0, 61);
  
  static const char charset[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  std::string suffix;
  suffix.reserve(12);
  for (int i = 0; i < 12; ++i) {
    suffix += charset[distribution(generator)];
  }
  
  return "tool_" + std::to_string(ms) + suffix;
}

static std::string get_tool_output_dir() {
    // Truncated command output is written here. Configurable via
    // QCODE_TOOL_OUTPUT_DIR / OPENCODE_TOOL_OUTPUT_DIR; falls back to a
    // cache dir under HOME (or /tmp) so no absolute path is hardcoded.
    if (const char* d = std::getenv("QCODE_TOOL_OUTPUT_DIR")) return d;
    if (const char* d = std::getenv("OPENCODE_TOOL_OUTPUT_DIR")) return d;
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/.cache/qcode/tool-output";
    return "/tmp/qcode-tool-output";
}

static void trim_incomplete_utf8_suffix(std::string& text) {
  if (text.empty()) return;
  auto lead = text.size() - 1;
  while (lead > 0 &&
         (static_cast<unsigned char>(text[lead]) & 0xC0U) == 0x80U) {
    --lead;
  }
  const auto first = static_cast<unsigned char>(text[lead]);
  size_t expected = 1;
  if ((first & 0xE0U) == 0xC0U) expected = 2;
  else if ((first & 0xF0U) == 0xE0U) expected = 3;
  else if ((first & 0xF8U) == 0xF0U) expected = 4;
  if (lead + expected > text.size()) text.resize(lead);
}

std::string BashTool::run_shell(const std::string& command,
                                const std::string& cwd, int /*timeout_ms*/,
                                std::optional<int> max_chars,
                                std::optional<int> max_lines, int& exit_code) {
  constexpr size_t kReadBufferSize = 4096;
  const auto char_limit = static_cast<size_t>(max_chars.value_or(4096));
  const auto line_limit = max_lines.value_or(0);

  exit_code = -1;
  const auto cmd =
      "cd -- " + shell_quote_literal(cwd) + " 2>/dev/null; " + command;

  const auto dir_path = get_tool_output_dir();
  std::error_code ec;
  std::filesystem::create_directories(dir_path, ec);
  const auto file_path =
      dir_path + "/" + generate_unique_tool_file_name();
  std::ofstream output_file(file_path, std::ios::out | std::ios::binary);

  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"),
                                                pclose);
  if (!pipe) {
    output_file.close();
    std::filesystem::remove(file_path, ec);
    return "Error: Failed to execute command";
  }

  std::array<char, kReadBufferSize> buffer;
  std::string inline_output;
  size_t total_size = 0;
  size_t line_count = 0;
  std::optional<size_t> truncate_index;
  std::optional<size_t> line_boundary;

  while (const auto bytes_read =
             std::fread(buffer.data(), 1, buffer.size(), pipe.get())) {
    if (output_file.is_open()) {
      output_file.write(buffer.data(),
                        static_cast<std::streamsize>(bytes_read));
    }

    auto chunk_inline_size = bytes_read;
    if (!truncate_index.has_value()) {
      if (total_size + bytes_read > char_limit) {
        truncate_index = char_limit;
      }

      if (line_limit > 0 && !line_boundary.has_value()) {
        for (size_t i = 0; i < bytes_read; ++i) {
          if (buffer[i] != '\n') continue;
          ++line_count;
          if (line_count == static_cast<size_t>(line_limit)) {
            line_boundary = total_size + i + 1;
            break;
          }
        }
      }

      if (line_boundary.has_value() &&
          total_size + bytes_read > line_boundary.value() &&
          (!truncate_index.has_value() ||
           line_boundary.value() < truncate_index.value())) {
        truncate_index = line_boundary;
      }
      if (truncate_index.has_value()) {
        chunk_inline_size =
            truncate_index.value() > total_size
                ? std::min(bytes_read,
                           truncate_index.value() - total_size)
                : 0;
      } else if (line_boundary.has_value()) {
        chunk_inline_size = std::min(
            bytes_read, line_boundary.value() - total_size);
      }
      inline_output.append(buffer.data(), chunk_inline_size);
    }
    total_size += bytes_read;
  }

  exit_code = pclose(pipe.release());
  if (exit_code != 0 && WIFEXITED(exit_code)) {
    exit_code = WEXITSTATUS(exit_code);
  }
  output_file.close();

  if (!truncate_index.has_value()) {
    std::filesystem::remove(file_path, ec);
    return inline_output;
  }

  trim_incomplete_utf8_suffix(inline_output);
  if (!output_file) {
    LOG_ERROR("BashTool: failed to write full output to {}", file_path);
  }

  return inline_output + "\n\n" +
      "[Output truncated at " + std::to_string(truncate_index.value()) +
      " characters (total " + std::to_string(total_size) +
      "). Full output saved to " + file_path + "]\n" +
      "Inspect the saved output with targeted range reads (for example: nl -ba " + file_path + " | sed -n '<start>,<end>p') or a one-pass rg/python summarizer; avoid raw full-file dumps. " +
      "If you truly need more inline text, request the smallest useful output budget (for bash, max_output_chars); large values can bloat context.";
}

JsonValue BashTool::exec_run(const JsonValue& args, const ToolExecutionContext& context) {
  LOG_DEBUG("BashTool: exec_run timeout={}", args.value("timeout", 30000));
  (void)context;
  std::string command = args.value("command", "");
  if (command.empty()) {
    JsonValue err;
    err["error"] = "mode=run requires command";
    return err;
  }
  std::string desc = args.value("description", "");
  if (desc.empty()) {
    JsonValue err;
    err["error"] = "description is required for bash run (5-10 word purpose).";
    return err;
  }

  // Safety checks
  std::string warning;
  if (!is_safe_command(command, warning)) {
    JsonValue err;
    err["error"] = warning;
    return err;
  }

  std::string cwd = resolve_cwd(args.value("workdir", ""));
  int timeout = args.value("timeout", 120000);

  std::optional<int> max_chars, max_lines;
  if (args.contains("max_output_chars")) max_chars = args["max_output_chars"].get<int>();
  if (args.contains("max_output_lines")) max_lines = args["max_output_lines"].get<int>();

  int exit_code = 0;
  auto output =
      run_shell(command, cwd, timeout, max_chars, max_lines, exit_code);

  JsonValue result;
  result["title"] = desc;
  result["output"] = output;
  result["metadata"]["exit"] = exit_code;
  result["metadata"]["description"] = desc;
  result["metadata"]["backgrounded"] = false;
  return result;
}

JsonValue BashTool::exec_background(const JsonValue& args, const ToolExecutionContext& context) {
  (void)context;
  std::string command = args.value("command", "");
  if (command.empty()) {
    JsonValue err;
    err["error"] = "mode=background requires command";
    return err;
  }
  std::string desc = args.value("description", "");
  if (desc.empty()) {
    JsonValue err;
    err["error"] = "description is required for bash background (5-10 word purpose).";
    return err;
  }

  // Safety checks
  std::string warning;
  if (!is_safe_command(command, warning)) {
    JsonValue err;
    err["error"] = warning;
    return err;
  }

  std::string task_id = generate_task_id();
  std::string cwd = resolve_cwd(args.value("workdir", ""));
  int timeout = args.value("timeout", 120000);
  (void)timeout;

  // Spawn background process
  pid_t pid = fork();
  if (pid == 0) {
    // Child process
    std::string cmd = "cd -- " + shell_quote_literal(cwd) + " 2>/dev/null; " + command;
    execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
    _exit(1);
  } else if (pid > 0) {
    // Parent
    BackgroundRegistry::instance().register_task(task_id, pid, command, cwd, "");
    JsonValue result;
    result["title"] = "Background: " + command.substr(0, 60);
    result["output"] = "Started background command (task ID: " + task_id + "). Use bash with mode=status to check status.";
    result["metadata"]["run_in_background"] = true;
    result["metadata"]["backgroundTaskId"] = task_id;
    return result;
  }

  JsonValue err;
  err["error"] = "Failed to fork process";
  return err;
}

JsonValue BashTool::exec_list() {
  auto tasks = BackgroundRegistry::instance().list_task_details();
  if (tasks.empty()) {
    JsonValue result;
    result["title"] = "bash list";
    result["output"] = "No background tasks.";
    result["metadata"]["count"] = 0;
    result["metadata"]["tasks"] = JsonValue::array();
    return result;
  }

  std::stringstream ss;
  for (const auto& t : tasks) {
    auto runtime = std::chrono::duration_cast<std::chrono::seconds>(
      (t.end_time.time_since_epoch() > std::chrono::system_clock::time_point().time_since_epoch()
        ? t.end_time : std::chrono::system_clock::now()) - t.start_time).count();
    ss << "- " << t.id << ": " << t.status << " pid=" << t.pid
       << " runtime=" << runtime << "s cmd=\"" << t.command << "\"\n";
  }
  JsonValue result;
  result["title"] = "bash list (" + std::to_string(tasks.size()) + ")";
  result["output"] = ss.str();
  result["metadata"]["count"] = tasks.size();
  return result;
}

JsonValue BashTool::exec_status(const JsonValue& args) {
  std::string id = args.value("id", "");
  if (id.empty()) {
    JsonValue err;
    err["error"] = "mode=status requires id";
    return err;
  }
  auto task = BackgroundRegistry::instance().get_task(id);
  if (!task.has_value()) {
    JsonValue result;
    result["title"] = "bash status " + id;
    result["output"] = "Task " + id + " not found.";
    result["metadata"]["found"] = false;
    result["metadata"]["id"] = id;
    return result;
  }

  auto runtime = std::chrono::duration_cast<std::chrono::seconds>(
    (task->end_time.time_since_epoch() > std::chrono::system_clock::time_point().time_since_epoch()
      ? task->end_time : std::chrono::system_clock::now()) - task->start_time).count();

  std::stringstream ss;
  ss << "Task " << task->id << "\n"
     << "status: " << task->status << "\n"
     << "pid: " << task->pid << "\n"
     << "cwd: " << task->cwd << "\n"
     << "runtime: " << runtime << "s\n"
     << "outputPath: " << (task->output_path.empty() ? "unknown" : task->output_path);

  JsonValue result;
  result["title"] = "bash status " + id;
  result["output"] = ss.str();
  result["metadata"]["found"] = true;
  return result;
}

JsonValue BashTool::exec_kill(const JsonValue& args) {
  std::string id = args.value("id", "");
  if (id.empty()) {
    JsonValue err;
    err["error"] = "mode=kill requires id";
    return err;
  }
  bool ok = BackgroundRegistry::instance().kill_task(id);
  JsonValue result;
  result["title"] = "bash kill " + id;
  if (ok) {
    result["output"] = "Kill signal sent for task " + id + ".";
    result["metadata"]["ok"] = true;
  } else {
    result["output"] = "Failed to kill task " + id + ": not found";
    result["metadata"]["ok"] = false;
  }
  result["metadata"]["id"] = id;
  return result;
}

JsonValue BashTool::exec_remove(const JsonValue& args) {
  std::string id = args.value("id", "");
  if (id.empty()) {
    JsonValue err;
    err["error"] = "mode=remove requires id";
    return err;
  }
  bool removed = BackgroundRegistry::instance().remove_task(id);
  JsonValue result;
  result["title"] = "bash remove " + id;
  result["output"] = removed ? "Removed task record " + id + "." : "Task " + id + " not found.";
  result["metadata"]["removed"] = removed;
  result["metadata"]["id"] = id;
  return result;
}

JsonValue BashTool::exec_cleanup(const JsonValue& args) {
  int max_age_ms = args.value("max_age_ms", 6 * 60 * 60 * 1000); // default 6 hours
  auto result = BackgroundRegistry::instance().cleanup(std::chrono::milliseconds(max_age_ms));
  JsonValue j;
  j["title"] = "bash cleanup";
  j["output"] = "Cleanup complete. Removed " + std::to_string(result.removed) +
                " task(s), kept " + std::to_string(result.kept) + ".";
  j["metadata"]["removed"] = result.removed;
  j["metadata"]["kept"] = result.kept;
  j["metadata"]["maxAgeMs"] = max_age_ms;
  return j;
}

JsonValue BashTool::execute(const JsonValue& args, const ToolExecutionContext& context) {
  std::string mode = "run";
  if (args.contains("mode")) mode = args["mode"].get<std::string>();
  else if (args.value("run_in_background", false)) mode = "background";

  if (mode == "list") return exec_list();
  if (mode == "status") return exec_status(args);
  if (mode == "kill") return exec_kill(args);
  if (mode == "remove") return exec_remove(args);
  if (mode == "cleanup") return exec_cleanup(args);
  if (mode == "background") return exec_background(args, context);
  return exec_run(args, context);
}

Tool BashTool::definition() {
  return Tool(
    "bash",
    BashToolSchema::parameters(),
    [](const JsonValue& args, const ToolExecutionContext& context) -> JsonValue {
      return BashTool::execute(args, context);
    }
  );
}

}  // namespace ai
