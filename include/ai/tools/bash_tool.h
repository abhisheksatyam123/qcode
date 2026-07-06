#pragma once

#include "ai/types/tool.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ai {

using JsonValue = nlohmann::json;

// ── Background task entry ──

struct BackgroundTaskEntry {
  std::string id;
  int pid = 0;
  std::string command;
  std::string cwd;
  std::chrono::system_clock::time_point start_time;
  std::chrono::system_clock::time_point end_time;
  std::string status;  // "running", "exited", "killed", "failed"
  std::string output_path;
  int exit_code = -1;
};

// ── Background Registry ──

class BackgroundRegistry {
 public:
  static BackgroundRegistry& instance();

  void register_task(const std::string& id, int pid, const std::string& command,
                     const std::string& cwd, const std::string& output_path);

  void mark_exited(const std::string& id, int exit_code);
  void mark_killed(const std::string& id);
  void mark_failed(const std::string& id, const std::string& error);

  std::optional<BackgroundTaskEntry> get_task(const std::string& id) const;
  std::vector<BackgroundTaskEntry> list_tasks() const;
  std::vector<BackgroundTaskEntry> list_task_details() const;

  bool kill_task(const std::string& id);
  bool remove_task(const std::string& id);

  struct CleanupResult {
    int removed = 0;
    int kept = 0;
  };
  CleanupResult cleanup(std::chrono::milliseconds max_age);

 private:
  BackgroundRegistry() = default;
  mutable std::mutex mutex_;
  std::map<std::string, BackgroundTaskEntry> tasks_;
};

// ── Bash Tool Schema ──
// Direct port of opencode's BashTool parameter schema (src/tool/bash/index.ts)

namespace BashToolSchema {

inline JsonValue parameters() {
  JsonValue s;
  s["type"] = "object";
  s["properties"] = JsonValue::object();
  auto& p = s["properties"];

  p["mode"] = JsonValue{
    {"type", "string"},
    {"enum", {"run", "background", "list", "status", "kill", "cleanup", "remove"}},
    {"description", "run|background|list|status|kill|cleanup|remove. Default run."}
  };
  p["command"] = JsonValue{
    {"type", "string"},
    {"description", "Command for run/background."}
  };
  p["workdir"] = JsonValue{
    {"type", "string"},
    {"description", "Working directory."}
  };
  p["timeout"] = JsonValue{
    {"type", "integer"},
    {"description", "Timeout ms (default 120000). Leave undefined unless you specifically need to restrict execution time."}
  };
  p["auto_background"] = JsonValue{
    {"type", "boolean"},
    {"description", "Auto-background on timeout (default true). Leave undefined."}
  };
  p["max_output_chars"] = JsonValue{
    {"type", "integer"}, {"minimum", 1},
    {"description", "Optional inline output character budget for run results."}
  };
  p["max_output_lines"] = JsonValue{
    {"type", "integer"}, {"minimum", 1},
    {"description", "Optional inline output line budget for run results."}
  };
  p["run_in_background"] = JsonValue{
    {"type", "boolean"},
    {"description", "Deprecated alias for mode=background."}
  };
  p["description"] = JsonValue{
    {"type", "string"},
    {"description", "5-10 word purpose; do not echo command."}
  };
  p["id"] = JsonValue{
    {"type", "string"},
    {"description", "Background task id."}
  };
  p["max_age_ms"] = JsonValue{
    {"type", "integer"},
    {"description", "cleanup max age ms."}
  };

  return s;
}

}  // namespace BashToolSchema

// ── BashTool executor ──
// Direct port of opencode's BashTool (src/tool/bash/index.ts)

class BashTool {
 public:
  static JsonValue execute(const JsonValue& args, const ToolExecutionContext& context);
  static Tool definition();

 private:
  static JsonValue exec_run(const JsonValue& args, const ToolExecutionContext& context);
  static JsonValue exec_background(const JsonValue& args, const ToolExecutionContext& context);
  static JsonValue exec_list();
  static JsonValue exec_status(const JsonValue& args);
  static JsonValue exec_kill(const JsonValue& args);
  static JsonValue exec_remove(const JsonValue& args);
  static JsonValue exec_cleanup(const JsonValue& args);

  static std::string run_shell(const std::string& command, const std::string& cwd,
                                int timeout_ms, int& exit_code);
  static std::string apply_output_budget(const std::string& output,
                                          std::optional<int> max_chars,
                                          std::optional<int> max_lines);
  static std::string resolve_workdir(const std::string& workdir);
};

}  // namespace ai
