#include <qcode/tools/task_tool.h>

#include <qcode/session/session_store.h>
#include <qcode/session/subagent_stats.h>
#include <qcode/tools/subagent_router.h>
#include <qcode/tools/task_target.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <cctype>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace qcode {

namespace {

constexpr const char* kModes[] = {"explore", "implement", "verify"};

bool is_mode(const std::string& s) {
  return std::find(std::begin(kModes), std::end(kModes), s) != std::end(kModes);
}

std::string new_child_session_id() {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  static thread_local std::mt19937 gen{std::random_device{}()};
  std::uniform_int_distribution<> dis(0, 35);
  const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string suffix;
  for (int i = 0; i < 6; i++) suffix += chars[dis(gen)];
  return "ses_" + std::to_string(ms) + suffix;
}

std::string error_text(const JsonValue& error) {
  if (error.is_string() && !error.get<std::string>().empty()) return error.get<std::string>();
  if (error.is_null() || (error.is_string() && error.get<std::string>().empty())) {
    return "subagent failed";
  }
  return error.dump();
}

// Subagents that are running right now, for the UI. An entry lives exactly as
// long as the blocking task call that owns it.
struct RunningTask {
  std::string parent_session_id;
  std::string description;
  std::string mode;
  std::string model;
  // The parent turn's abort flag: a subagent is part of that turn.
  std::shared_ptr<std::atomic<bool>> abort_flag;
};

class RunningTasks {
 public:
  static RunningTasks& instance() {
    static RunningTasks tasks;
    return tasks;
  }

  void add(const std::string& session_id, RunningTask task) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_[session_id] = std::move(task);
  }

  void remove(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.erase(session_id);
  }

  bool contains(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.contains(session_id);
  }

  std::vector<std::pair<std::string, RunningTask>> of_parent(const std::string& parent) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, RunningTask>> out;
    for (const auto& [sid, task] : tasks_) {
      if (parent.empty() || task.parent_session_id == parent) out.emplace_back(sid, task);
    }
    return out;
  }

  void abort_related(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [sid, task] : tasks_) {
      if ((sid == session_id || task.parent_session_id == session_id) && task.abort_flag) {
        task.abort_flag->store(true);
      }
    }
  }

 private:
  mutable std::mutex mutex_;
  std::map<std::string, RunningTask> tasks_;
};

// Subagents started with background: true. Each runs on its own thread with
// its own abort flag (the lead's turn ending or Esc does not stop it), and
// its result waits here until the lead reads it (notice, status or wait).
struct BackgroundTask {
  std::string parent_session_id;
  std::string title;
  std::shared_ptr<std::atomic<bool>> abort_flag;
  std::chrono::steady_clock::time_point started;
  bool finished = false;
  bool delivered = false;
  JsonValue result;
};

std::string notice_text(const std::string& id, const BackgroundTask& task) {
  const JsonValue& r = task.result;
  if (r.contains("error")) {
    return "[Background task " + id + " (" + task.title + ") failed]\nError: " +
           error_text(r["error"]);
  }
  return "[Background task " + id + " (" + task.title + ") finished]\n" +
         r.value("output", std::string("(no output)"));
}

class BackgroundTasks {
 public:
  // Leaked on purpose: detached workers may outlive static destruction.
  static BackgroundTasks& instance() {
    static auto* tasks = new BackgroundTasks();
    return *tasks;
  }

  void start(const std::string& id, BackgroundTask task, std::function<JsonValue()> job) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      task.started = std::chrono::steady_clock::now();
      tasks_[id] = std::move(task);
      ++running_;
    }
    std::thread([this, id, job = std::move(job)] {
      JsonValue result;
      try {
        result = job();
      } catch (const std::exception& e) {
        result = {{"error", std::string("subagent crashed: ") + e.what()}};
      } catch (...) {
        result = {{"error", "subagent crashed"}};
      }
      std::function<void()> listener;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& t = tasks_[id];
        t.finished = true;
        t.result = std::move(result);
        --running_;
        listener = listener_;
      }
      cv_.notify_all();
      if (listener) listener();
    }).detach();
  }

  std::optional<BackgroundTask> get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return std::nullopt;
    return it->second;
  }

  // The finished task's report, marked delivered.
  std::optional<std::string> take_result(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end() || !it->second.finished) return std::nullopt;
    it->second.delivered = true;
    return notice_text(id, it->second);
  }

  std::vector<std::string> running_of(const std::string& parent) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    for (const auto& [id, t] : tasks_) {
      if (!t.finished && t.parent_session_id == parent) out.push_back(id);
    }
    return out;
  }

  std::vector<std::string> take_notices(const std::string& parent) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    for (auto& [id, t] : tasks_) {
      if (t.finished && !t.delivered && t.parent_session_id == parent) {
        t.delivered = true;
        out.push_back(notice_text(id, t));
      }
    }
    return out;
  }

  bool has_notices(const std::string& parent) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, t] : tasks_) {
      if (t.finished && !t.delivered && t.parent_session_id == parent) return true;
    }
    return false;
  }

  // Wait until every id finished, `timeout` passed or `abort` is set.
  void wait(const std::vector<std::string>& ids, std::chrono::milliseconds timeout,
            const std::shared_ptr<std::atomic<bool>>& abort) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(mutex_);
    auto done = [&] {
      for (const auto& id : ids) {
        auto it = tasks_.find(id);
        if (it != tasks_.end() && !it->second.finished) return false;
      }
      return true;
    };
    while (!done() && !(abort && abort->load()) &&
           std::chrono::steady_clock::now() < deadline) {
      cv_.wait_for(lock, std::chrono::milliseconds(200));
    }
  }

  bool kill(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end() || it->second.finished) return false;
    if (it->second.abort_flag) it->second.abort_flag->store(true);
    return true;
  }

  void set_listener(std::function<void()> listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listener_ = std::move(listener);
  }

  void shutdown(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    listener_ = nullptr;
    for (auto& [id, t] : tasks_) {
      if (!t.finished && t.abort_flag) t.abort_flag->store(true);
    }
    cv_.wait_for(lock, timeout, [this] { return running_ == 0; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::map<std::string, BackgroundTask> tasks_;
  int running_ = 0;
  std::function<void()> listener_;
};

JsonValue task_item(const std::string& session_id, const std::string& parent_session_id,
                    const std::string& description, const std::string& mode,
                    const std::string& model, const std::string& status) {
  return {{"task_id", session_id},   {"sessionId", session_id},
          {"parent_session_id", parent_session_id},
          {"description", description}, {"agent", mode},
          {"mode", mode},             {"model", model},
          {"status", status}};
}

bool is_known_mode_token(const std::string& s) {
  return s == "explore" || s == "implement" || s == "verify" || s == "general" ||
         s == "generalPurpose";
}

bool looks_like_prompt_text(const std::string& s) {
  if (s.size() > 80) return true;
  if (s.find('\n') != std::string::npos) return true;
  if (s.find("Workspace:") != std::string::npos) return true;
  return s.find(' ') != std::string::npos && s.size() > 24;
}

bool looks_like_model_id(const std::string& s) {
  if (s.empty() || s.find(' ') != std::string::npos ||
      s.find('\n') != std::string::npos) {
    return false;
  }
  if (is_inherit_model_id(s)) return true;
  if (s.rfind("cursor-", 0) == 0 || s.rfind("claude-", 0) == 0 ||
      s.rfind("gemini-", 0) == 0 || s.rfind("composer-", 0) == 0) {
    return true;
  }
  if (s.find('/') != std::string::npos) return true;
  return s.size() < 96 && s.find(':') != std::string::npos;
}

bool is_numeric_json_key(const std::string& k) {
  if (k.empty()) return false;
  for (char c : k) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool looks_like_hex_id(const std::string& s) {
  if (s.size() != 32 && s.size() != 36) return false;
  for (char c : s) {
    if (c == '-') continue;
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool looks_like_tool_call_label(const std::string& s) {
  return s.rfind("call-", 0) == 0 && s.find("fc_") != std::string::npos;
}

// Cursor Task protobuf leftovers split a long prompt across numbered string
// fields (commonly 10 then 12, cut at 69 bytes mid-path). Join nearby shards.
std::string reassemble_numbered_prompt_shards(const JsonValue& args) {
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

}  // namespace

JsonValue TaskTool::list_tasks(const std::string& parent_session_id) {
  JsonValue tasks = JsonValue::array();
  std::string summary;
  std::vector<std::string> running_ids;
  for (const auto& [sid, task] : RunningTasks::instance().of_parent(parent_session_id)) {
    tasks.push_back(task_item(sid, task.parent_session_id, task.description, task.mode,
                              task.model, "running"));
    summary += "- " + sid + " [running] " + task.description + "\n";
    running_ids.push_back(sid);
  }

  // Finished subagents are durable child sessions; their last row says how
  // they ended (a run cut short by a crash has no final Assistant row).
  if (!parent_session_id.empty()) {
    for (const auto& s : qcode::session::list_sessions_full(true, parent_session_id)) {
      if (std::find(running_ids.begin(), running_ids.end(), s.id) != running_ids.end()) continue;
      std::string status = "interrupted";
      if (auto last = qcode::session::load_last_session_message(s.id);
          last && last->first == "Assistant") {
        status = last->second.rfind("Error", 0) == 0 ? "error" : "done";
      }
      const std::string model = !s.provider.empty() ? (s.provider + ":" + s.model) : s.model;
      tasks.push_back(task_item(s.id, s.parent_session_id, s.title, "", model, status));
      summary += "- " + s.id + " [" + status + "] " + s.title + "\n";
    }
  }

  return {{"title", "subagent tasks"},
          {"output", summary.empty() ? "No subagents." : summary},
          {"metadata", {{"tasks", tasks}}}};
}

bool TaskTool::is_session_running(const std::string& session_id) {
  return !session_id.empty() && RunningTasks::instance().contains(session_id);
}

void TaskTool::delete_session_tasks(const std::string& session_id) {
  if (!session_id.empty()) RunningTasks::instance().abort_related(session_id);
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
  std::string subagent_type = args.value("subagent_type", "");
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

  if (mode.empty() && is_mode(subagent_type)) mode = subagent_type;
  if (!is_mode(mode)) mode = "explore";

  if (prompt_text.empty()) prompt_text = args.value("task", "");
  if (prompt_text.empty()) prompt_text = args.value("objective", "");
  if (prompt_text.empty()) prompt_text = args.value("description", "");

  JsonValue out = {{"prompt", prompt_text}, {"mode", mode}};
  for (const char* key : {"description", "difficulty"}) {
    if (args.contains(key) && args[key].is_string()) out[key] = args[key];
  }
  model = args.value("model", "");
  const std::string provider = args.value("provider", "");
  if (!provider.empty() && !model.empty() && model.find(':') == std::string::npos) {
    model = provider + ":" + model;
  }
  if (!model.empty()) out["model"] = model;
  return out;
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

namespace {

// The task tool result for a finished subagent run; also saves its final row.
JsonValue finish_task(const std::string& session_id, const std::string& title,
                      const std::string& mode, const std::string& difficulty,
                      const std::string& model, JsonValue sub) {
  if (!sub.is_object()) sub = {{"error", "subagent failed"}};
  std::string used_model = sub.value("model", model);
  if (sub.contains("provider") && sub["provider"].is_string() && !used_model.empty()) {
    used_model = sub["provider"].get<std::string>() + ":" + used_model;
  }
  JsonValue metadata = {{"sessionId", session_id},       {"task_id", session_id},
                        {"mode", mode},                  {"difficulty", difficulty},
                        {"model", used_model},           {"attempts", sub.value("attempts", 1)},
                        {"route_reason", sub.value("route_reason", "")}};
  if (sub.contains("error")) {
    const std::string err = error_text(sub["error"]);
    qcode::session::save_message(session_id, "Assistant", "Error: " + err);
    metadata["status"] = "error";
    return {{"error", err}, {"metadata", metadata}};
  }
  const std::string output = sub.value("output", "(subagent finished without output)");
  qcode::session::save_message(session_id, "Assistant", output);
  metadata["status"] = "done";
  // One line so the lead knows what to rate / resume; it is read on a paid model.
  std::string footer = "\n\n[task_id: " + session_id;
  if (!used_model.empty()) footer += " · " + used_model;
  footer += " · resume with task_id · rate with rate_task]";
  return {{"title", "task: " + title}, {"output", output + footer}, {"metadata", metadata}};
}

// Earlier attempts of a task: the next attempt number, and the mode and
// model of its last successful attempt (a resumed task keeps them).
struct PriorRuns {
  int attempts = 0;
  std::string mode;
  std::string model;  // provider:model
};

PriorRuns prior_runs(const std::string& parent, const std::string& task_id) {
  PriorRuns out;
  for (const auto& run : qcode::session::list_subagent_runs(parent, 1000)) {
    if (run.task_id != task_id) continue;
    out.attempts = std::max(out.attempts, run.attempt);
    if (run.outcome == routing::Outcome::kSuccess && out.model.empty()) {
      out.model = run.provider + ":" + run.model;  // newest first
      out.mode = run.mode;
    }
    if (out.mode.empty()) out.mode = run.mode;
  }
  return out;
}

// What a finished (or interrupted) child session says, read from its rows:
// for tasks that ran before a restart or were already delivered.
std::string stored_task_status(const std::string& id, std::string* output) {
  auto last = qcode::session::load_last_session_message(id);
  if (!last || last->first != "Assistant") return "interrupted";
  if (output) *output = last->second;
  return last->second.rfind("Error", 0) == 0 ? "error" : "done";
}

JsonValue task_status(const JsonValue& args, const ToolExecutionContext& context) {
  const std::string id = args.value("task_id", "");
  if (id.empty()) {
    JsonValue list = TaskTool::list_tasks(context.session_id);
    const auto running = BackgroundTasks::instance().running_of(context.session_id);
    if (!running.empty()) {
      std::string extra = "\nBackground tasks still running:";
      for (const auto& r : running) extra += " " + r;
      list["output"] = list.value("output", std::string()) + extra;
    }
    return list;
  }
  if (auto report = BackgroundTasks::instance().take_result(id)) {
    return {{"title", "task status"}, {"output", *report}};
  }
  if (auto bg = BackgroundTasks::instance().get(id); bg && !bg->finished) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::steady_clock::now() - bg->started).count();
    return {{"title", "task status"},
            {"output", "Task " + id + " (" + bg->title + ") is still running (" +
                           std::to_string(secs) + "s). Use action \"wait\" to block for it."}};
  }
  if (TaskTool::is_session_running(id)) {
    return {{"title", "task status"}, {"output", "Task " + id + " is running."}};
  }
  if (qcode::session::get_session_title(id).empty()) {
    return {{"error", "Unknown task_id " + id}};
  }
  std::string output;
  const std::string status = stored_task_status(id, &output);
  std::string text = "Task " + id + ": " + status;
  if (!output.empty()) text += "\n" + output;
  return {{"title", "task status"}, {"output", text}};
}

JsonValue task_wait(const JsonValue& args, const ToolExecutionContext& context) {
  auto& tasks = BackgroundTasks::instance();
  std::vector<std::string> ids;
  if (const std::string id = args.value("task_id", ""); !id.empty()) {
    ids.push_back(id);
  } else {
    ids = tasks.running_of(context.session_id);
  }
  int timeout_s = 600;
  if (args.contains("timeout_s") && args["timeout_s"].is_number()) {
    timeout_s = std::clamp(args["timeout_s"].get<int>(), 1, 3600);
  }
  tasks.wait(ids, std::chrono::seconds(timeout_s), context.abort_flag);
  std::string text;
  for (const auto& id : ids) {
    if (!text.empty()) text += "\n\n";
    if (auto report = tasks.take_result(id)) {
      text += *report;
    } else if (auto bg = tasks.get(id); bg && !bg->finished) {
      text += "Task " + id + " (" + bg->title + ") is still running after " +
              std::to_string(timeout_s) + "s.";
    } else {
      text += task_status({{"task_id", id}}, context).value("output", "Task " + id + ": unknown");
    }
  }
  // Reports of other finished background tasks come along too.
  for (auto& notice : tasks.take_notices(context.session_id)) {
    text += (text.empty() ? "" : "\n\n") + notice;
  }
  if (text.empty()) text = "No background tasks to wait for.";
  return {{"title", "task wait"}, {"output", text}};
}

}  // namespace

JsonValue TaskTool::execute(const JsonValue& args, const ToolExecutionContext& context) {
  const std::string action = args.value("action", "run");
  if (action == "status") return task_status(args, context);
  if (action == "wait") return task_wait(args, context);
  if (action == "kill") {
    const std::string id = args.value("task_id", "");
    if (id.empty()) return {{"error", "kill needs task_id."}};
    if (BackgroundTasks::instance().kill(id)) {
      return {{"title", "task kill"}, {"output", "Stopping background task " + id + "."}};
    }
    return {{"error", "Task " + id + " is not a running background task."}};
  }
  if (action != "run") {
    return {{"error", "Unknown action '" + action + "'. Use run, status, wait or kill."}};
  }

  const std::string prompt = args.value("prompt", "");
  const std::string description = args.value("description", "");
  const std::string resume_id = args.value("task_id", "");
  std::string mode = args.value("mode", "");
  std::string model = args.value("model", "");
  const bool background = args.value("background", false);
  const std::string difficulty(
      routing::to_string(routing::parse_difficulty(args.value("difficulty", "medium"))));
  if (prompt.empty()) {
    return {{"error", "task needs a prompt: the full, self-contained instructions."}};
  }
  if (!context.subagent_runner) {
    return {{"error", "Subagents are not available in this context."}};
  }
  if (!context.session_id.empty() && qcode::session::is_child_session(context.session_id)) {
    return {{"error", "Subagents cannot delegate further. Do the work directly."}};
  }

  std::string session_id;
  std::string title;
  int attempt_base = 0;
  if (!resume_id.empty()) {
    // Resume: the subagent continues its own conversation with `prompt`.
    if (!qcode::session::is_child_session(resume_id) ||
        qcode::session::get_parent_session_id(resume_id) != context.session_id) {
      return {{"error", "Unknown task_id " + resume_id + " (not a subagent of this session)."}};
    }
    if (is_session_running(resume_id)) {
      return {{"error", "Task " + resume_id + " is still running; use action \"wait\" first."}};
    }
    const PriorRuns prior = prior_runs(context.session_id, resume_id);
    attempt_base = prior.attempts;
    if (mode.empty()) mode = prior.mode;
    if (model.empty()) model = prior.model;
    session_id = resume_id;
    title = qcode::session::get_session_title(resume_id);
  }
  if (mode.empty()) mode = "explore";
  if (!is_mode(mode)) {
    return {{"error", "Unknown mode '" + mode + "'. Use explore, implement, or verify."}};
  }
  if (session_id.empty()) {
    session_id = new_child_session_id();
    title = description.empty() ? mode : description;
    qcode::session::ensure_session_row(session_id, title, "", "", context.workspace,
                                       context.session_id);
  }
  if (title.empty()) title = description.empty() ? mode : description;
  qcode::session::save_message(session_id, "User", prompt);

  JsonValue sub_args = {{"prompt", prompt},          {"description", description},
                        {"mode", mode},              {"difficulty", difficulty},
                        {"session_id", session_id},  {"parent_session_id", context.session_id}};
  if (!model.empty()) sub_args["model"] = model;
  if (!resume_id.empty()) {
    sub_args["resume"] = true;
    sub_args["attempt_base"] = attempt_base;
  }

  if (background) {
    // Own abort flag: the lead's turn ending (or Esc) does not stop it;
    // action "kill" or deleting the session does.
    auto abort_flag = std::make_shared<std::atomic<bool>>(false);
    sub_args["background"] = true;
    RunningTasks::instance().add(session_id,
                                 {context.session_id, title, mode, model, abort_flag});
    BackgroundTask task;
    task.parent_session_id = context.session_id;
    task.title = title;
    task.abort_flag = abort_flag;
    BackgroundTasks::instance().start(
        session_id, std::move(task),
        [runner = context.subagent_runner, sub_args, abort_flag, session_id, title, mode,
         difficulty, model]() -> JsonValue {
          struct Unregister {
            std::string session_id;
            ~Unregister() { RunningTasks::instance().remove(session_id); }
          } unregister{session_id};
          JsonValue sub;
          try {
            sub = runner(sub_args, abort_flag);
          } catch (const std::exception& e) {
            sub = {{"error", std::string("subagent crashed: ") + e.what()}};
          }
          if (abort_flag->load() && sub.contains("error")) {
            sub = {{"error", "killed"}};
          }
          return finish_task(session_id, title, mode, difficulty, model, std::move(sub));
        });
    JsonValue metadata = {{"sessionId", session_id}, {"task_id", session_id},
                          {"mode", mode},            {"difficulty", difficulty},
                          {"status", "running"},     {"background", true}};
    return {{"title", "task (background): " + title},
            {"output", "Started background task " + session_id + " (" + title +
                           "). Keep working: its report arrives as a message when it "
                           "finishes. task {action: \"wait\" | \"status\" | \"kill\", "
                           "task_id} to block for, check or stop it."},
            {"metadata", metadata}};
  }

  RunningTasks::instance().add(
      session_id, {context.session_id, title, mode, model, context.abort_flag});
  struct Unregister {
    std::string session_id;
    ~Unregister() { RunningTasks::instance().remove(session_id); }
  } unregister{session_id};

  JsonValue sub;
  try {
    sub = context.subagent_runner(sub_args, context.abort_flag);
  } catch (const std::exception& e) {
    sub = {{"error", std::string("subagent crashed: ") + e.what()}};
  }
  return finish_task(session_id, title, mode, difficulty, model, std::move(sub));
}

std::vector<std::string> TaskTool::take_notices(const std::string& parent_session_id) {
  return BackgroundTasks::instance().take_notices(parent_session_id);
}

bool TaskTool::has_notices(const std::string& parent_session_id) {
  return BackgroundTasks::instance().has_notices(parent_session_id);
}

void TaskTool::set_notice_listener(std::function<void()> listener) {
  BackgroundTasks::instance().set_listener(std::move(listener));
}

void TaskTool::shutdown_background(std::chrono::milliseconds timeout) {
  BackgroundTasks::instance().shutdown(timeout);
}

JsonValue TaskTool::execute_rate(const JsonValue& args, const ToolExecutionContext& context) {
  const JsonValue ratings = args.is_object() ? args.value("ratings", JsonValue::array())
                                             : JsonValue::array();
  if (!ratings.is_array()) {
    return {{"error", "rate_task takes ratings: [{task_id, score 1-5, note?}]."}};
  }
  int rated = 0;
  std::string notes;
  for (const auto& r : ratings) {
    const std::string task_id =
        r.is_object() && r.contains("task_id") && r["task_id"].is_string()
            ? r["task_id"].get<std::string>()
            : "";
    const int score = r.is_object() && r.contains("score") && r["score"].is_number()
                          ? r["score"].get<int>()
                          : 0;
    if (task_id.empty() || score < 1 || score > 5) {
      notes += "\nSkipped " + r.dump() + ": needs task_id and score 1-5.";
      continue;
    }
    const std::string note =
        r.contains("note") && r["note"].is_string() ? r["note"].get<std::string>() : "";
    if (qcode::session::rate_subagent_run(task_id, score, note)) {
      ++rated;
    } else {
      notes += "\nUnknown task_id " + task_id + " (no finished task report).";
    }
  }
  // The board rides in this tool result rather than the system prompt: it
  // changes after every run, and a result in history stays cache-stable.
  std::string output = ratings.empty() ? std::string()
                                       : "Rated " + std::to_string(rated) + " task(s)." + notes;
  if (context.routing_board) {
    const std::string board = context.routing_board();
    if (!board.empty()) output += (output.empty() ? "" : "\n\n") + board;
  }
  if (output.empty()) output = "No ratings given and no model board available.";
  return {{"title", "rate_task"}, {"output", output}};
}

JsonValue TaskTool::parameters() {
  return {
      {"type", "object"},
      {"properties",
       {{"description",
         {{"type", "string"}, {"description", "Short label (3-6 words) shown in the UI."}}},
        {"prompt",
         {{"type", "string"},
          {"description",
           "Complete instructions: goal, relevant files/paths, constraints, and what to "
           "report back."}}},
        {"mode",
         {{"type", "string"},
          {"enum", {"explore", "implement", "verify"}},
          {"description",
           "explore = read-only (default), implement = may edit files, verify = "
           "build/test/audit."}}},
        {"difficulty",
         {{"type", "string"},
          {"enum", {"easy", "medium", "hard"}},
          {"default", "medium"},
          {"description",
           "easy = lookup/small edit, hard = design/tricky bug; hard jobs go to proven "
           "strong models."}}},
        {"model",
         {{"type", "string"},
          {"description",
           "Optional provider:model from the catalog. Omit to let the router pick a free "
           "model."}}},
        {"background",
         {{"type", "boolean"},
          {"description",
           "Return at once with a task_id; the report arrives later as a message."}}},
        {"task_id",
         {{"type", "string"},
          {"description",
           "With prompt: resume that subagent (it keeps its history). With action: the "
           "task to check, wait for or stop."}}},
        {"action",
         {{"type", "string"},
          {"enum", {"run", "status", "wait", "kill"}},
          {"default", "run"},
          {"description",
           "run (default) starts or resumes a subagent; status / wait / kill manage "
           "background tasks (without task_id: all of yours)."}}},
        {"timeout_s",
         {{"type", "integer"},
          {"description", "wait: give up after this many seconds (default 600)."}}}}},
  };
}

Tool TaskTool::definition() {
  Tool tool(TaskTool::kDescription, TaskTool::parameters(),
            [](const JsonValue& args, const ToolExecutionContext& context) -> JsonValue {
              return TaskTool::execute(args, context);
            });
  tool.name = "task";
  return tool;
}

JsonValue TaskTool::rate_parameters() {
  const JsonValue rating = {
      {"type", "object"},
      {"properties",
       {{"task_id", {{"type", "string"}, {"description", "From the task result footer."}}},
        {"score", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5}}},
        {"note", {{"type", "string"}, {"description", "Optional: what was wrong or missing."}}}}},
      {"required", JsonValue::array({"task_id", "score"})},
  };
  return {
      {"type", "object"},
      {"properties", {{"ratings", {{"type", "array"}, {"items", rating}}}}},
  };
}

Tool TaskTool::rate_definition() {
  Tool tool(TaskTool::kRateDescription, TaskTool::rate_parameters(),
            [](const JsonValue& args, const ToolExecutionContext& context) -> JsonValue {
              return TaskTool::execute_rate(args, context);
            });
  tool.name = "rate_task";
  return tool;
}

}  // namespace qcode
