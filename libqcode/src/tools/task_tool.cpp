#include <qcode/tools/task_tool.h>
#include <qcode/session/session_store.h>
#include <cctype>
#include <qcode/tools/task_target.h>

#include <algorithm>
#include <utility>
#include <vector>
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

static void persist_subagent_output(const std::string& session_id,
                                    const std::string& text) {
  if (session_id.empty() || text.empty()) return;
  qcode::session::save_message(session_id, "Assistant", text);
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
    persist_subagent_output(session_id, output);
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
      persist_subagent_output(entry->session_id, entry->output);

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

  void harvest_ready() {
    std::vector<std::shared_ptr<SubagentTaskEntry>> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto& [_, t] : tasks_) {
        if (t && t->status == "running" && t->future.valid()) pending.push_back(t);
      }
    }
    for (auto& entry : pending) {
      if (entry->future.wait_for(std::chrono::milliseconds(0)) !=
          std::future_status::ready) {
        continue;
      }
      JsonValue out_json;
      try {
        out_json = entry->future.get();
      } catch (const std::exception& e) {
        out_json = JsonValue{{"error", std::string("Subagent crashed: ") + e.what()}};
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (entry->status != "running") continue;
      if (out_json.is_object() && out_json.contains("error")) {
        entry->status = "error";
        entry->error = out_json.value("error", "Subagent failed");
        entry->output = "Error: " + entry->error;
      } else {
        entry->status = "done";
        entry->output = out_json.value("output", "Subagent finished with no output");
      }
      persist_subagent_output(entry->session_id, entry->output);
    }
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

  bool is_session_running(const std::string& session_id) {
    if (session_id.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = by_session_.find(session_id);
    if (it != by_session_.end()) {
      return it->second->status == "running";
    }
    return false;
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
  SubagentRegistry::instance().harvest_ready();
  return SubagentRegistry::instance().list();
}

bool TaskTool::is_session_running(const std::string& session_id) {
  SubagentRegistry::instance().harvest_ready();
  return SubagentRegistry::instance().is_session_running(session_id);
}

static bool is_known_mode_token(const std::string& s) {
  return s == "explore" || s == "implement" || s == "verify" || s == "general" ||
         s == "generalPurpose";
}

static bool looks_like_prompt_text(const std::string& s) {
  if (s.size() > 80) return true;
  if (s.find('\n') != std::string::npos) return true;
  if (s.find("Workspace:") != std::string::npos) return true;
  return s.find(' ') != std::string::npos && s.size() > 24;
}

static bool looks_like_model_id(const std::string& s) {
  if (s.empty() || s.find(' ') != std::string::npos ||
      s.find('\n') != std::string::npos) {
    return false;
  }
  if (is_inherit_model_id(s)) return true;
  if (s.rfind("cursor-", 0) == 0 || s.rfind("claude-", 0) == 0 ||
      s.rfind("gemini-", 0) == 0) {
    return true;
  }
  if (s.find('/') != std::string::npos) return true;
  return s.size() < 96 && s.find(':') != std::string::npos;
}

static bool looks_like_provider_id(const std::string& s) {
  if (s.empty() || s.size() > 32) return false;
  if (!std::isalpha(static_cast<unsigned char>(s[0]))) return false;
  for (char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
      return false;
  }
  return true;
}

static bool is_numeric_json_key(const std::string& k) {
  if (k.empty()) return false;
  for (char c : k) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

static bool looks_like_hex_id(const std::string& s) {
  if (s.size() != 32 && s.size() != 36) return false;
  for (char c : s) {
    if (c == '-') continue;
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

static bool looks_like_tool_call_label(const std::string& s) {
  return s.rfind("call-", 0) == 0 && s.find("fc_") != std::string::npos;
}

// Cursor Task protobuf leftovers split a long prompt across numbered string
// fields (commonly 10 then 12, cut at 69 bytes mid-path). Join nearby shards.
static std::string reassemble_numbered_prompt_shards(const JsonValue& args) {
  if (!args.is_object()) return {};
  std::vector<std::pair<int, std::string>> shards;
  for (auto it = args.begin(); it != args.end(); ++it) {
    if (!is_numeric_json_key(it.key()) || !it.value().is_string()) continue;
    const std::string v = it.value().get<std::string>();
    if (v.empty() || is_known_mode_token(v) || looks_like_model_id(v) ||
        looks_like_hex_id(v) || looks_like_tool_call_label(v)) {
      continue;
    }
    const int num = std::stoi(it.key());
    // TaskArgs 1-4 are description/prompt/type/model. Leftover shards are 10+.
    if (num < 10) continue;
    shards.emplace_back(num, v);
  }
  if (shards.empty()) return {};
  std::sort(shards.begin(), shards.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  std::string best;
  std::string cur = shards.front().second;
  int prev = shards.front().first;
  auto consider = [&](const std::string& s) {
    if (s.size() > best.size()) best = s;
  };
  for (size_t i = 1; i < shards.size(); ++i) {
    const int num = shards[i].first;
    const std::string& piece = shards[i].second;
    const bool nearby = num >= prev && (num - prev) <= 2;
    const char lead = piece.empty() ? '\0' : piece.front();
    const bool mid_token = std::islower(static_cast<unsigned char>(lead)) ||
                           lead == '/' || lead == 'i' || lead == ' ';
    if (nearby && (looks_like_prompt_text(cur) || looks_like_prompt_text(cur + piece) ||
                   mid_token)) {
      cur += piece;
      prev = num;
    } else {
      consider(cur);
      cur = piece;
      prev = num;
    }
  }
  consider(cur);
  return best;
}

JsonValue TaskTool::normalize_spawn_args(JsonValue args) {
  // Cursor Task protobuf leftovers: field 10 is often the real prompt; long
  // text is split across nearby numbered fields (10 + 12).
  const std::string assembled = reassemble_numbered_prompt_shards(args);
  if (!assembled.empty()) {
    const std::string prompt = args.value("prompt", "");
    if (prompt.empty() || is_known_mode_token(prompt) ||
        assembled.size() > prompt.size()) {
      args["prompt"] = assembled;
    }
  } else if (args.contains("10") && args["10"].is_string()) {
    const std::string f10 = args["10"].get<std::string>();
    const std::string prompt = args.value("prompt", "");
    if (looks_like_prompt_text(f10) &&
        (prompt.empty() || is_known_mode_token(prompt))) {
      args["prompt"] = f10;
    }
  }

  std::string prompt_text = args.value("prompt", "");
  std::string model = args.value("model", "");
  std::string subagent_type = args.value("subagent_type", args.value("agent", ""));
  std::string mode = args.value("mode", "");

  // Lead models mix Cursor Task fields with qcode spawn fields:
  // prompt=mode, subagent_type=model id, model=actual prompt.
  if (is_known_mode_token(prompt_text) && looks_like_prompt_text(model)) {
    const std::string actual_prompt = model;
    if (mode.empty()) mode = prompt_text;
    if (looks_like_model_id(subagent_type)) {
      args["model"] = subagent_type;
      model = subagent_type;
    } else {
      args.erase("model");
      model.clear();
    }
    subagent_type = mode;
    args["subagent_type"] = subagent_type;
    args["prompt"] = actual_prompt;
    prompt_text = actual_prompt;
  }

  if (looks_like_model_id(subagent_type) &&
      (model.empty() || looks_like_prompt_text(model))) {
    if (looks_like_prompt_text(model) &&
        (prompt_text.empty() || is_known_mode_token(prompt_text))) {
      if (is_known_mode_token(prompt_text) && mode.empty()) mode = prompt_text;
      args["prompt"] = model;
      prompt_text = model;
    }
    args["model"] = subagent_type;
    model = subagent_type;
    subagent_type = !mode.empty() ? mode
                    : (is_known_mode_token(prompt_text) ? prompt_text : "general");
    args["subagent_type"] = subagent_type;
  }

  if (looks_like_prompt_text(model) &&
      (prompt_text.empty() || is_known_mode_token(prompt_text))) {
    if (is_known_mode_token(prompt_text) && mode.empty()) mode = prompt_text;
    args["prompt"] = model;
    prompt_text = model;
    args.erase("model");
    model.clear();
  }

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
  }
  args["mode"] = mode;

  if (prompt_text.empty()) prompt_text = args.value("task", "");
  if (prompt_text.empty()) prompt_text = args.value("objective", "");
  if (prompt_text.empty()) prompt_text = args.value("description", "");
  if (!prompt_text.empty()) args["prompt"] = prompt_text;

  model = args.value("model", "");
  std::string provider = args.value("provider", "");
  if (provider.empty() && !model.empty() && !is_inherit_model_id(model)) {
    const auto colon = model.find(':');
    if (colon != std::string::npos && colon > 0 && colon + 1 < model.size()) {
      const std::string prefix = model.substr(0, colon);
      // Only split catalog form provider:model. Prompt text like
      // "Read-only. Workspace: /path" must not become a provider.
      if (looks_like_provider_id(prefix) && prefix.find('/') == std::string::npos) {
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
  } else if (mode == "explore") {
    args["can_edit"] = false;
  }
  return args;
}

std::string TaskTool::session_id_from_result(const JsonValue& result) {
  if (!result.is_object()) {
    if (result.is_string()) {
      const std::string s = result.get<std::string>();
      const auto pos = s.find("task_id: ");
      if (pos != std::string::npos) {
        auto end = s.find_first_of(" \n\r\t", pos + 9);
        return s.substr(pos + 9, end == std::string::npos ? std::string::npos
                                                          : end - (pos + 9));
      }
    }
    return "";
  }
  if (result.contains("result") && result["result"].is_object()) {
    std::string nested = session_id_from_result(result["result"]);
    if (!nested.empty()) return nested;
  }
  if (result.contains("metadata") && result["metadata"].is_object()) {
    std::string sid = result["metadata"].value(
        "sessionId", result["metadata"].value("task_id", ""));
    if (!sid.empty() && sid.rfind("bg_", 0) != 0) return sid;
  }
  std::string sid = result.value("sessionId", result.value("task_id", ""));
  if (!sid.empty() && sid.rfind("bg_", 0) != 0) return sid;
  if (result.contains("output") && result["output"].is_string()) {
    const std::string s = result["output"].get<std::string>();
    auto pos = s.find("task_id: ");
    if (pos != std::string::npos) {
      auto end = s.find_first_of(" \n\r\t", pos + 9);
      sid = s.substr(pos + 9, end == std::string::npos ? std::string::npos
                                                       : end - (pos + 9));
      if (!sid.empty() && sid.rfind("bg_", 0) != 0) return sid;
    }
  }
  return "";
}

JsonValue TaskTool::exec_spawn(const JsonValue& raw_args, const ToolExecutionContext& context) {
  JsonValue args = TaskTool::normalize_spawn_args(raw_args);

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
  {
    std::string title = description.empty() ? subagent_type : description;
    qcode::session::ensure_session_row(session_id, title, args.value("provider", ""),
                                       args.value("model", ""), context.workspace);
    qcode::session::save_message(session_id, "User", prompt_text);
  }
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
      JsonValue sub_args = args;
      sub_args["sessionId"] = session_id;
      sub_args["session_id"] = session_id;
      sub_args["task_id"] = session_id;
      JsonValue sub = context.subagent_runner(sub_args, context.abort_flag);
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
      persist_subagent_output(session_id, output_text);
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
    JsonValue sub_args = args;
    sub_args["sessionId"] = session_id;
    sub_args["session_id"] = session_id;
    sub_args["task_id"] = session_id;
    auto fut = std::async(std::launch::async, [runner, sub_args, sub_abort]() -> JsonValue {
      if (sub_abort && sub_abort->load()) {
        return JsonValue{{"error", "Subagent cancelled"}};
      }
      return runner(sub_args, sub_abort);
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

  JsonValue err;
  err["error"] =
      "Unsupported task op '" + op +
      "'. Use spawn, result, kill, or list.";
  err["metadata"]["op"] = op;
  err["metadata"]["task_id"] = id;
  return err;
}

JsonValue TaskTool::exec_model(const JsonValue& args) {
  (void)args;
  JsonValue err;
  err["error"] =
      "task op 'model' is not supported. Pass provider and model on spawn "
      "(provider:model from opencode.json).";
  return err;
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
  props["op"]["enum"] = {"spawn", "result", "kill", "list", "status"};
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
