#include <qcode/tools/task_tool.h>
#include <qcode/tools/task_target.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <future>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include <qcode/core/logger.h>

namespace qcode {

// ── TaskTool implementation ──
// Multi-agent orchestration and subagent execution registry

static std::string generate_session_id() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 35);
  const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string suffix;
  for (int i = 0; i < 6; i++) suffix += chars[dis(gen)];
  return "ses_" + std::to_string(ms) + suffix;
}

static std::string generate_bg_id(const std::string& session_id) {
  return "bg_" + session_id;
}

// ── Subagent result parsing ──

struct ParsedSubagentResult {
  bool structured = false;
  bool empty = false;
  JsonValue result;
  std::string raw_result_text;
};

static ParsedSubagentResult parse_subagent_result(const std::string& text) {
  ParsedSubagentResult parsed;
  parsed.raw_result_text = text;
  std::string trimmed = text;
  trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
  trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

  if (trimmed.empty()) {
    parsed.structured = false;
    parsed.empty = true;
    return parsed;
  }

  try {
    JsonValue j = JsonValue::parse(trimmed);
    if (j.is_object()) {
      parsed.structured = true;
      parsed.empty = false;
      parsed.result = j;
      return parsed;
    }
  } catch (...) {
    // Not valid JSON
  }

  parsed.structured = false;
  parsed.empty = false;
  return parsed;
}

// ── Subagent Task Registry ──
// Manages asynchronous parallel subagent jobs, status tracking, waiting, and cancellation

struct SubagentTaskEntry {
  std::string background_task_id;
  std::string session_id;
  std::string description;
  std::string subagent_type;
  std::string mode;
  std::string model;
  std::string status;  // "running", "done", "error", "killed"
  std::string output;
  std::string error;
  std::shared_ptr<std::atomic<bool>> abort_flag;
  std::shared_future<JsonValue> future;
  std::chrono::steady_clock::time_point start_time;
};

class SubagentRegistry {
 public:
  static SubagentRegistry& instance() {
    static SubagentRegistry reg;
    return reg;
  }

  void register_task(const std::string& bg_id,
                     const std::string& session_id,
                     const std::string& description,
                     const std::string& subagent_type,
                     const std::string& mode,
                     const std::string& model,
                     std::shared_ptr<std::atomic<bool>> abort_flag,
                     std::future<JsonValue> fut) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entry = std::make_shared<SubagentTaskEntry>();
    entry->background_task_id = bg_id;
    entry->session_id = session_id;
    entry->description = description;
    entry->subagent_type = subagent_type;
    entry->mode = mode;
    entry->model = model;
    entry->status = "running";
    entry->abort_flag = abort_flag;
    entry->future = fut.share();
    entry->start_time = std::chrono::steady_clock::now();
    tasks_[bg_id] = entry;
    by_session_[session_id] = entry;
  }

  void register_completed(const std::string& bg_id,
                          const std::string& session_id,
                          const std::string& description,
                          const std::string& subagent_type,
                          const std::string& mode,
                          const std::string& model,
                          const std::string& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entry = std::make_shared<SubagentTaskEntry>();
    entry->background_task_id = bg_id;
    entry->session_id = session_id;
    entry->description = description;
    entry->subagent_type = subagent_type;
    entry->mode = mode;
    entry->model = model;
    entry->status = "done";
    entry->output = output;
    entry->start_time = std::chrono::steady_clock::now();
    tasks_[bg_id] = entry;
    by_session_[session_id] = entry;
  }

  JsonValue await_or_poll(const std::string& bg_id, int timeout_ms) {
    std::shared_ptr<SubagentTaskEntry> entry;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = tasks_.find(bg_id);
      if (it == tasks_.end()) {
        auto sit = by_session_.find(bg_id);
        if (sit != by_session_.end()) entry = sit->second;
      } else {
        entry = it->second;
      }
    }

    if (!entry) {
      JsonValue err;
      err["error"] = "Background task not found: " + bg_id;
      return err;
    }

    // If already terminal (done, error, killed)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (entry->status == "done" || entry->status == "error" || entry->status == "killed") {
        JsonValue res;
        res["title"] = "task " + entry->status + ": " + entry->description;
        res["output"] = entry->status == "error" ? ("Error: " + entry->error) : entry->output;
        res["metadata"] = {
            {"status", entry->status},
            {"background_task_id", entry->background_task_id},
            {"task_id", entry->session_id},
            {"sessionId", entry->session_id},
            {"agent", entry->subagent_type},
            {"mode", entry->mode},
        };
        if (entry->status == "error") res["error"] = entry->error;
        return res;
      }
    }

    std::shared_future<JsonValue> fut = entry->future;
    if (!fut.valid()) {
      JsonValue res;
      res["title"] = "task complete: " + entry->description;
      res["output"] = entry->output;
      res["metadata"] = {
          {"status", entry->status},
          {"background_task_id", entry->background_task_id},
          {"task_id", entry->session_id},
      };
      return res;
    }

    std::future_status status;
    if (timeout_ms == 0) {
      status = fut.wait_for(std::chrono::milliseconds(0));
    } else {
      status = fut.wait_for(std::chrono::milliseconds(timeout_ms));
    }

    if (status == std::future_status::ready) {
      JsonValue out_json;
      try {
        out_json = fut.get();
      } catch (const std::exception& e) {
        out_json = JsonValue{{"error", std::string("Subagent crashed: ") + e.what()}};
      }

      std::lock_guard<std::mutex> lock(mutex_);
      if (out_json.is_object() && out_json.contains("error")) {
        entry->status = "error";
        entry->error = out_json.value("error", "Subagent failed");
        entry->output = "Error: " + entry->error;
      } else {
        entry->status = "done";
        entry->output = out_json.value("output", "Subagent finished with no output");
      }

      JsonValue res;
      res["title"] = "task complete: " + entry->description;
      res["output"] = entry->output;
      res["metadata"] = {
          {"status", entry->status},
          {"background_task_id", entry->background_task_id},
          {"task_id", entry->session_id},
          {"sessionId", entry->session_id},
          {"agent", entry->subagent_type},
          {"mode", entry->mode},
      };
      if (entry->status == "error") res["error"] = entry->error;
      return res;
    }

    // Still pending / running
    JsonValue res;
    res["title"] = "task result: " + bg_id;
    res["output"] = "task_id: " + bg_id + "\nstatus: running\n\nStill running. Collect with a later result call.";
    res["metadata"] = {
        {"status", "running"},
        {"background_task_id", bg_id},
        {"task_id", entry->session_id},
    };
    return res;
  }

  JsonValue kill(const std::string& id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    std::shared_ptr<SubagentTaskEntry> entry;
    if (it == tasks_.end()) {
      auto sit = by_session_.find(id);
      if (sit != by_session_.end()) entry = sit->second;
    } else {
      entry = it->second;
    }

    if (!entry) {
      JsonValue res;
      res["title"] = "task kill: " + id;
      res["output"] = "Task " + id + " not found to kill.";
      res["metadata"] = {{"status", "not_found"}, {"task_id", id}};
      return res;
    }

    if (entry->abort_flag) {
      entry->abort_flag->store(true);
    }
    entry->status = "killed";
    entry->output = "Task killed by orchestrator." + (!reason.empty() ? (" Reason: " + reason) : "");

    JsonValue res;
    res["title"] = "task kill: " + id;
    res["output"] = entry->output;
    res["metadata"] = {
        {"status", "killed"},
        {"task_id", id},
        {"background_task_id", entry->background_task_id},
        {"sessionId", entry->session_id},
    };
    return res;
  }

  JsonValue list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    JsonValue res;
    res["title"] = "subagent tasks";
    JsonValue list_arr = JsonValue::array();
    std::stringstream ss;
    ss << "Active & Recent Subagent Tasks:\n";
    for (const auto& [id, t] : tasks_) {
      JsonValue item;
      item["background_task_id"] = t->background_task_id;
      item["task_id"] = t->session_id;
      item["description"] = t->description;
      item["agent"] = t->subagent_type;
      item["mode"] = t->mode;
      item["model"] = t->model;
      item["status"] = t->status;
      list_arr.push_back(item);
      ss << "- " << t->background_task_id << " [" << t->status << "] (" << t->subagent_type << " / " << t->mode << "): " << t->description << "\n";
    }
    res["output"] = tasks_.empty() ? "No background tasks registered." : ss.str();
    res["metadata"] = {{"tasks", list_arr}};
    return res;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.clear();
    by_session_.clear();
  }

 private:
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<SubagentTaskEntry>> tasks_;
  std::map<std::string, std::shared_ptr<SubagentTaskEntry>> by_session_;
};

void TaskTool::clear_background_tasks() {
  SubagentRegistry::instance().clear();
}

JsonValue TaskTool::list_tasks() {
  return SubagentRegistry::instance().list();
}

static JsonValue normalize_spawn_args(JsonValue args) {
  std::string mode = args.value("mode", "");
  std::string subagent_type = args.value("subagent_type", args.value("agent", ""));
  if (subagent_type.empty()) {
    if (mode == "explore" || mode == "implement" || mode == "verify") {
      subagent_type = mode;
    } else {
      subagent_type = "general";
    }
  }
  args["subagent_type"] = subagent_type;

  if (mode.empty()) {
    if (subagent_type == "explore" || subagent_type == "implement" ||
        subagent_type == "verify") {
      mode = subagent_type;
    } else {
      mode = "explore";
    }
    args["mode"] = mode;
  }

  std::string prompt_text = args.value("prompt", "");
  if (prompt_text.empty()) prompt_text = args.value("task", "");
  if (prompt_text.empty()) prompt_text = args.value("objective", "");
  if (prompt_text.empty()) prompt_text = args.value("description", "");
  if (!prompt_text.empty()) args["prompt"] = prompt_text;

  std::string model = args.value("model", "");
  std::string provider = args.value("provider", "");
  if (provider.empty() && !model.empty() && !is_inherit_model_id(model)) {
    const auto colon = model.find(':');
    if (colon != std::string::npos && colon > 0 && colon + 1 < model.size()) {
      const std::string prefix = model.substr(0, colon);
      // Without the catalog, only split provider:model when the prefix is a
      // simple id (no '/'). OpenRouter model ids use ':' (e.g. name:free).
      if (prefix.find('/') == std::string::npos) {
        args["provider"] = prefix;
        args["model"] = model.substr(colon + 1);
      }
    }
  }

  if (mode == "implement") {
    if (!args.contains("can_edit")) args["can_edit"] = true;
    if (!args.contains("allowed_paths") || args["allowed_paths"].empty()) {
      args["allowed_paths"] = JsonValue::array({"."});
    }
  }
  return args;
}

JsonValue TaskTool::exec_spawn(const JsonValue& raw_args, const ToolExecutionContext& context) {
  JsonValue args = normalize_spawn_args(raw_args);

  std::string subagent_type = args.value("subagent_type", "general");
  std::string description = args.value("description", "");
  std::string prompt_text = args.value("prompt", "");
  if (prompt_text.empty()) {
    JsonValue err;
    err["error"] =
        "task prompt is required (prompt, task, objective, or description)";
    return err;
  }

  std::string session_id = generate_session_id();
  bool is_background = args.value("background", args.value("run_in_background", false));

  std::string mode = args.value("mode", "explore");
  std::string objective = args.value("objective", "");
  bool can_edit = args.value("can_edit", false);

  if (mode == "implement" && !can_edit) {
    JsonValue err;
    err["error"] = "can_edit=true is required for implement-mode subagents";
    return err;
  }
  if (mode == "implement" && (!args.contains("allowed_paths") || args["allowed_paths"].empty())) {
    JsonValue err;
    err["error"] = "non-empty allowed_paths is required for implement-mode subagents";
    return err;
  }

  // Build the subagent output metadata
  JsonValue metadata;
  metadata["sessionId"] = session_id;
  metadata["agent"] = subagent_type;
  metadata["description"] = description;
  metadata["mode"] = mode;

  if (args.contains("provider")) metadata["provider"] = args["provider"];
  if (args.contains("model")) metadata["model"] = args["model"];
  if (args.contains("models")) metadata["model_candidates"] = args["models"];

  // Scope
  std::stringstream scope_ss;
  if (args.contains("scope") && args["scope"].is_array()) {
    for (const auto& s : args["scope"]) scope_ss << "- " << s.get<std::string>() << "\n";
  }
  std::stringstream out_of_scope_ss;
  if (args.contains("out_of_scope") && args["out_of_scope"].is_array()) {
    for (const auto& s : args["out_of_scope"]) out_of_scope_ss << "- " << s.get<std::string>() << "\n";
  }

  // Budget
  std::optional<int> budget_timeout;
  if (args.contains("budget") && args["budget"].is_object()) {
    if (args["budget"].contains("timeout_ms"))
      budget_timeout = args["budget"]["timeout_ms"].get<int>();
  }

  // For foreground spawn, run a REAL nested subagent turn synchronously
  if (!is_background) {
    std::string output_text;
    bool ran = false;
    if (context.subagent_runner) {
      JsonValue sub = context.subagent_runner(args, context.abort_flag);
      if (sub.is_object() && sub.contains("error")) {
        if (sub["error"].is_string() && sub["error"].get<std::string>().empty()) {
          sub["error"] = "subagent failed";
        } else if (!sub["error"].is_string()) {
          sub["error"] = sub["error"].is_null() ? "subagent failed"
                                                : sub["error"].dump();
        }
        return sub;
      }
      output_text =
          sub.value("output", std::string("Subagent finished with no output"));
      ran = true;
    }
    if (!ran) {
      // Direct tool invocation without runner: template acknowledgement
      std::stringstream fallback;
      fallback << "task_id: " << session_id << "\n\n";
      fallback << "<task_result>\n";
      fallback << "@subagent " << subagent_type << " " << description << "\n\n";
      if (!objective.empty()) fallback << "Objective: " << objective << "\n";
      if (scope_ss.tellp() > 0) fallback << "Scope:\n" << scope_ss.str();
      fallback << "Prompt: " << prompt_text << "\n";
      fallback << "</task_result>";
      output_text = fallback.str();
    }

    JsonValue result;
    result["title"] = std::string("task complete: ") + description;
    result["output"] = output_text;
    result["metadata"] = metadata;
    result["metadata"]["status"] = "done";
    result["metadata"]["real_subagent"] = ran;
    return result;
  }

  // Background spawn: runs concurrently in parallel with independent provider/model
  std::string bg_id = generate_bg_id(session_id);
  std::string chosen_model = args.value("model", "");

  if (context.subagent_runner) {
    auto runner = context.subagent_runner;
    auto sub_abort = std::make_shared<std::atomic<bool>>(false);
    auto fut = std::async(std::launch::async, [runner, args, sub_abort]() -> JsonValue {
      if (sub_abort && sub_abort->load()) {
        return JsonValue{{"error", "Subagent cancelled"}};
      }
      return runner(args, sub_abort);
    });
    SubagentRegistry::instance().register_task(
        bg_id, session_id, description, subagent_type, mode, chosen_model, sub_abort, std::move(fut));
  } else {
    // Direct or mock invocation without runner: register completed template
    std::stringstream fallback;
    fallback << "task_id: " << session_id << "\n\n";
    fallback << "<task_result>\n";
    fallback << "@subagent " << subagent_type << " " << description << "\n\n";
    if (!objective.empty()) fallback << "Objective: " << objective << "\n";
    if (scope_ss.tellp() > 0) fallback << "Scope:\n" << scope_ss.str();
    fallback << "Prompt: " << prompt_text << "\n";
    fallback << "</task_result>";
    SubagentRegistry::instance().register_completed(
        bg_id, session_id, description, subagent_type, mode, chosen_model, fallback.str());
  }

  JsonValue result;
  result["title"] = std::string("task started: ") + description;
  result["output"] = "background_task_id: " + bg_id + "\ntask_id: " + session_id + "\nstatus: running";
  result["metadata"] = metadata;
  result["metadata"]["status"] = "running";
  result["metadata"]["background_task_id"] = bg_id;
  result["metadata"]["task_id"] = session_id;
  result["metadata"]["sessionId"] = session_id;
  return result;
}

JsonValue TaskTool::exec_result(const JsonValue& args) {
  LOG_DEBUG("TaskTool: exec_result");
  std::string bg_id = args.value("background_task_id", args.value("task_id", ""));
  if (bg_id.empty()) {
    JsonValue err;
    err["error"] = "background_task_id is required for result operation";
    return err;
  }

  int timeout_ms = args.value("timeout_ms", 30000);
  return SubagentRegistry::instance().await_or_poll(bg_id, timeout_ms);
}

JsonValue TaskTool::exec_lifecycle(const JsonValue& args, const std::string& op) {
  std::string id = args.value("task_id", args.value("pid", args.value("background_task_id", "")));
  std::string reason = args.value("reason", "");

  if (op == "kill") {
    return SubagentRegistry::instance().kill(id, reason);
  }
  if (op == "status" || op == "list") {
    return SubagentRegistry::instance().list();
  }

  JsonValue result;
  result["title"] = std::string("task ") + op + ": " + (!id.empty() ? id : "");
  result["output"] = "Operation " + op + " completed for task " +
                     (!id.empty() ? id : "") +
                     (!reason.empty() ? " (reason: " + reason + ")" : "");
  result["metadata"]["op"] = op;
  result["metadata"]["task_id"] = id;
  return result;
}

JsonValue TaskTool::exec_model(const JsonValue& args) {
  std::string task_id = args.value("task_id", "");
  std::string pid = args.value("pid", "");
  std::string model = args.value("model", "");

  JsonValue result;
  result["title"] = "task model: " + (!task_id.empty() ? task_id : pid);
  result["output"] = "Model override set for task " + (!task_id.empty() ? task_id : pid) +
                     " to " + model;
  result["metadata"]["model"] = model;
  result["metadata"]["task_id"] = task_id;
  result["metadata"]["pid"] = pid;
  return result;
}

JsonValue TaskTool::execute(const JsonValue& args, const ToolExecutionContext& context) {
  std::string op = args.value("op", "spawn");

  if (op == "result") return exec_result(args);
  if (op == "kill" || op == "pause" || op == "resume" || op == "resurrect" || op == "status" || op == "list")
    return exec_lifecycle(args, op);
  if (op == "model") return exec_model(args);

  return exec_spawn(args, context);
}

Tool TaskTool::definition() {
  JsonValue schema = TaskToolSchema::spawn_parameters();
  auto& props = schema["properties"];
  props["op"]["enum"] = {"spawn", "result", "kill", "pause", "resume",
                         "resurrect", "model", "status", "list"};
  props["background_task_id"] = JsonValue{
      {"type", "string"},
      {"description", "Background task id for result/kill."}};
  props["timeout_ms"] = JsonValue{{"type", "integer"},
                                  {"minimum", 0},
                                  {"description", "How long to wait for result."}};
  props["pid"] = JsonValue{{"type", "string"}};
  props["reason"] = JsonValue{{"type", "string"}};

  Tool tool(
      TaskTool::kDescription,
      schema,
      [](const JsonValue& args, const ToolExecutionContext& context) -> JsonValue {
        return TaskTool::execute(args, context);
      });
  tool.name = "task";
  return tool;
}

}  // namespace qcode
