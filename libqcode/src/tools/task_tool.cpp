#include <qcode/tools/task_tool.h>

#include <qcode/session/session_store.h>
#include <qcode/tools/task_target.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <utility>
#include <vector>

namespace qcode {

namespace {

constexpr const char* kLead = "lead";

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

// The orchestrator session of the team `session_id` belongs to.
std::string lead_of(const std::string& session_id) {
  if (!session_id.empty() && qcode::session::is_child_session(session_id)) {
    const std::string parent = qcode::session::get_parent_session_id(session_id);
    if (!parent.empty()) return parent;
  }
  return session_id;
}

bool in_team(const std::string& lead, const std::string& id) {
  return !id.empty() && id != lead && qcode::session::is_child_session(id) &&
         qcode::session::get_parent_session_id(id) == lead;
}

// A subagent that is running now; the entry lives exactly as long as its run.
struct RunningTask {
  std::string lead_session_id;
  std::string owner_session_id;  // who started it; a background report goes there
  std::string title;
  std::string model;
  // Background: its own flag (only kill or shutdown stop it). Blocking: the
  // starter's turn flag (the subagent is part of that turn).
  std::shared_ptr<std::atomic<bool>> abort_flag;
  bool background = false;
  std::chrono::steady_clock::time_point started;
};

struct InboxItem {
  std::string task_id;  // set for a background report (delivered at most once)
  std::string text;
};

struct Report {
  std::string text;
  bool delivered = false;
};

std::string report_text(const std::string& id, const std::string& title, const JsonValue& r) {
  if (r.contains("error")) {
    return "[Background task " + id + " (" + title + ") failed]\nError: " +
           error_text(r["error"]);
  }
  return "[Background task " + id + " (" + title + ") finished]\n" +
         r.value("output", std::string("(no output)"));
}

// Running subagents, background reports and inboxes. Leaked on purpose:
// detached background workers may outlive static destruction.
class Team {
 public:
  static Team& instance() {
    static auto* team = new Team();
    return *team;
  }

  void add(const std::string& id, RunningTask task) {
    std::lock_guard<std::mutex> lock(mutex_);
    task.started = std::chrono::steady_clock::now();
    running_[id] = std::move(task);
  }

  void remove(const std::string& id) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_.erase(id);
    }
    cv_.notify_all();
  }

  std::optional<RunningTask> running(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = running_.find(id);
    if (it == running_.end()) return std::nullopt;
    return it->second;
  }

  // Running subagents of `lead` (all when empty).
  std::vector<std::pair<std::string, RunningTask>> running_of(const std::string& lead) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, RunningTask>> out;
    for (const auto& [id, t] : running_) {
      if (lead.empty() || t.lead_session_id == lead) out.emplace_back(id, t);
    }
    return out;
  }

  void abort_related(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, t] : running_) {
      if ((id == session_id || t.lead_session_id == session_id) && t.abort_flag) {
        t.abort_flag->store(true);
      }
    }
  }

  // Runs `job` on its own thread; its report goes to the starter's inbox, or
  // to the lead's when the starter is a subagent that is no longer running.
  void start_background(const std::string& id, RunningTask task,
                        std::function<JsonValue()> job) {
    task.background = true;
    add(id, task);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++workers_;
    }
    std::thread([this, id, task, job = std::move(job)] {
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
        running_.erase(id);
        const bool owner_alive = task.owner_session_id == task.lead_session_id ||
                                 running_.contains(task.owner_session_id);
        const std::string to = owner_alive ? task.owner_session_id : task.lead_session_id;
        const std::string text = report_text(id, task.title, result);
        reports_[id] = {text, false};
        inbox_[to].push_back({id, text});
        --workers_;
        listener = listener_;
      }
      cv_.notify_all();
      if (listener) listener();
    }).detach();
  }

  // A finished background task's report (marked delivered), if there is one.
  std::optional<std::string> take_report(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = reports_.find(id);
    if (it == reports_.end()) return std::nullopt;
    it->second.delivered = true;
    return it->second.text;
  }

  void post(const std::string& to, std::string text) {
    std::function<void()> listener;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inbox_[to].push_back({"", std::move(text)});
      listener = listener_;
    }
    cv_.notify_all();
    if (listener) listener();
  }

  std::vector<std::string> take(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    auto it = inbox_.find(session_id);
    if (it == inbox_.end()) return out;
    for (auto& item : it->second) {
      if (stale(item)) continue;
      if (!item.task_id.empty()) reports_[item.task_id].delivered = true;
      out.push_back(std::move(item.text));
    }
    inbox_.erase(it);
    return out;
  }

  bool has(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_locked(session_id);
  }

  // Until `id` (when set) stops running, something lands in `session_id`'s
  // inbox, `timeout` passes or `abort` is set.
  void wait(const std::string& session_id, const std::string& id,
            std::chrono::milliseconds timeout, const std::shared_ptr<std::atomic<bool>>& abort) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(mutex_);
    auto ready = [&] {
      return has_locked(session_id) || (!id.empty() && !running_.contains(id));
    };
    while (!ready() && !(abort && abort->load()) &&
           std::chrono::steady_clock::now() < deadline) {
      cv_.wait_for(lock, std::chrono::milliseconds(200));
    }
  }

  bool kill(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = running_.find(id);
    if (it == running_.end() || !it->second.background || !it->second.abort_flag) return false;
    it->second.abort_flag->store(true);
    return true;
  }

  void set_listener(std::function<void()> listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listener_ = std::move(listener);
  }

  void shutdown(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    listener_ = nullptr;
    for (auto& [id, t] : running_) {
      if (t.background && t.abort_flag) t.abort_flag->store(true);
    }
    cv_.wait_for(lock, timeout, [this] { return workers_ == 0; });
  }

 private:
  bool stale(const InboxItem& item) const {
    if (item.task_id.empty()) return false;
    auto it = reports_.find(item.task_id);
    return it != reports_.end() && it->second.delivered;
  }

  bool has_locked(const std::string& session_id) const {
    auto it = inbox_.find(session_id);
    if (it == inbox_.end()) return false;
    for (const auto& item : it->second) {
      if (!stale(item)) return true;
    }
    return false;
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::map<std::string, RunningTask> running_;
  std::map<std::string, Report> reports_;
  std::map<std::string, std::vector<InboxItem>> inbox_;
  int workers_ = 0;
  std::function<void()> listener_;
};

JsonValue task_item(const std::string& session_id, const std::string& lead,
                    const std::string& title, const std::string& model,
                    const std::string& status) {
  return {{"task_id", session_id},   {"sessionId", session_id},
          {"parent_session_id", lead}, {"description", title},
          {"agent", "subagent"},     {"model", model},
          {"status", status}};
}

std::string seconds_since(std::chrono::steady_clock::time_point t) {
  return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - t)
                            .count()) +
         "s";
}

// What a finished (or interrupted) child session says, read from its rows.
std::string stored_task_status(const std::string& id, std::string* output) {
  auto last = qcode::session::load_last_session_message(id);
  if (!last || last->first != "Assistant") return "interrupted";
  if (output) *output = last->second;
  return last->second.rfind("Error", 0) == 0 ? "error" : "done";
}

std::string session_model(const std::string& id) {
  const auto [provider, model] = qcode::session::get_session_provider_model(id);
  if (provider.empty() || model.empty()) return model;
  return provider + ":" + model;
}

// ── Cursor Task argument repair ──

bool is_cursor_agent_type(const std::string& s) {
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
    if (v.empty() || is_cursor_agent_type(v) || looks_like_model_id(v) ||
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

JsonValue TaskTool::list_tasks(const std::string& lead_session_id) {
  JsonValue tasks = JsonValue::array();
  std::string summary;
  std::vector<std::string> running_ids;
  for (const auto& [id, t] : Team::instance().running_of(lead_session_id)) {
    const std::string status = t.background ? "running (background)" : "running";
    std::string model = session_model(id);  // set by the runner once it resolved the model
    if (model.empty()) model = t.model;
    tasks.push_back(task_item(id, t.lead_session_id, t.title, model, "running"));
    summary += "- " + id + " [" + status + ", " + seconds_since(t.started) + "] " + model +
               " · " + t.title + "\n";
    running_ids.push_back(id);
  }
  // Finished subagents are durable child sessions; their last row says how
  // they ended (a run cut short by a crash has no final Assistant row).
  if (!lead_session_id.empty()) {
    for (const auto& s : qcode::session::list_sessions_full(true, lead_session_id)) {
      if (std::find(running_ids.begin(), running_ids.end(), s.id) != running_ids.end()) continue;
      const std::string status = stored_task_status(s.id, nullptr);
      const std::string model = !s.provider.empty() ? (s.provider + ":" + s.model) : s.model;
      tasks.push_back(task_item(s.id, s.parent_session_id, s.title, model, status));
      summary += "- " + s.id + " [" + status + "] " + model + " · " + s.title + "\n";
    }
  }
  return {{"title", "subagent tasks"},
          {"output", summary.empty() ? "No subagents." : summary},
          {"metadata", {{"tasks", tasks}}}};
}

bool TaskTool::is_session_running(const std::string& session_id) {
  return !session_id.empty() && Team::instance().running(session_id).has_value();
}

void TaskTool::delete_session_tasks(const std::string& session_id) {
  if (!session_id.empty()) Team::instance().abort_related(session_id);
}

JsonValue TaskTool::normalize_spawn_args(JsonValue args) {
  // Cursor Task protobuf leftovers: field 10 is often the real prompt; long
  // text is split across nearby numbered fields (10 + 12).
  const std::string assembled = reassemble_numbered_prompt_shards(args);
  if (!assembled.empty()) {
    const std::string prompt = args.value("prompt", "");
    if (prompt.empty() || is_cursor_agent_type(prompt) ||
        assembled.size() > prompt.size()) {
      args["prompt"] = assembled;
    }
  } else if (args.contains("10") && args["10"].is_string()) {
    const std::string f10 = args["10"].get<std::string>();
    const std::string prompt = args.value("prompt", "");
    if (looks_like_prompt_text(f10) &&
        (prompt.empty() || is_cursor_agent_type(prompt))) {
      args["prompt"] = f10;
    }
  }

  std::string prompt_text = args.value("prompt", "");
  std::string model = args.value("model", "");
  std::string subagent_type = args.value("subagent_type", "");
  std::string mode = args.value("mode", "");

  // Lead models mix Cursor Task fields with qcode spawn fields:
  // prompt=mode, subagent_type=model id, model=actual prompt.
  if (is_cursor_agent_type(prompt_text) && looks_like_prompt_text(model)) {
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
        (prompt_text.empty() || is_cursor_agent_type(prompt_text))) {
      if (is_cursor_agent_type(prompt_text) && mode.empty()) mode = prompt_text;
      args["prompt"] = model;
      prompt_text = model;
    }
    args["model"] = subagent_type;
    model = subagent_type;
    subagent_type = !mode.empty() ? mode
                    : (is_cursor_agent_type(prompt_text) ? prompt_text : "general");
    args["subagent_type"] = subagent_type;
  }

  if (looks_like_prompt_text(model) &&
      (prompt_text.empty() || is_cursor_agent_type(prompt_text))) {
    if (is_cursor_agent_type(prompt_text) && mode.empty()) mode = prompt_text;
    args["prompt"] = model;
    prompt_text = model;
    args.erase("model");
    model.clear();
  }


  if (prompt_text.empty()) prompt_text = args.value("task", "");
  if (prompt_text.empty()) prompt_text = args.value("objective", "");
  if (prompt_text.empty()) prompt_text = args.value("description", "");

  JsonValue out = {{"prompt", prompt_text}};
  for (const char* key : {"description"}) {
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
        auto end = s.find_first_of(" \n\r\t]", pos + 9);
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
      auto end = s.find_first_of(" \n\r\t]", pos + 9);
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
                      const std::string& model, JsonValue sub) {
  if (!sub.is_object()) sub = {{"error", "subagent failed"}};
  std::string used_model = sub.value("model", model);
  if (sub.contains("provider") && sub["provider"].is_string() && !used_model.empty()) {
    used_model = sub["provider"].get<std::string>() + ":" + used_model;
  }
  JsonValue metadata = {{"sessionId", session_id}, {"task_id", session_id}, {"model", used_model}};
  if (sub.contains("error")) {
    const std::string err = error_text(sub["error"]);
    qcode::session::save_message(session_id, "Assistant", "Error: " + err);
    metadata["status"] = "error";
    return {{"error", err + "\n[task_id: " + session_id + "]"}, {"metadata", metadata}};
  }
  const std::string output = sub.value("output", "(subagent finished without output)");
  qcode::session::save_message(session_id, "Assistant", output);
  metadata["status"] = "done";
  std::string footer = "\n\n[task_id: " + session_id;
  if (!used_model.empty()) footer += " · " + used_model;
  footer += "]";
  return {{"title", "task: " + title}, {"output", output + footer}, {"metadata", metadata}};
}

JsonValue tool_text(const std::string& title, const std::string& text) {
  return {{"title", title}, {"output", text}};
}

JsonValue task_status(const std::string& id, const ToolExecutionContext& context) {
  const std::string lead = lead_of(context.session_id);
  if (id.empty() || id == kLead) {
    JsonValue list = TaskTool::list_tasks(lead);
    std::string head = "Lead (orchestrator): " + (lead.empty() ? std::string("-") : lead);
    if (context.session_id != lead) head += "\nYou: " + context.session_id;
    list["output"] = head + "\n\n" + list.value("output", std::string());
    return list;
  }
  if (auto t = Team::instance().running(id)) {
    return tool_text("task status", "Task " + id + " (" + t->title + ") is running (" +
                                        seconds_since(t->started) + ", " + t->model + ").");
  }
  if (auto report = Team::instance().take_report(id)) return tool_text("task status", *report);
  if (qcode::session::get_session_title(id).empty()) {
    return {{"error", "Unknown task_id " + id}};
  }
  std::string output;
  std::string text = "Task " + id + ": " + stored_task_status(id, &output);
  if (!output.empty()) text += "\n" + output;
  return tool_text("task status", text);
}

JsonValue task_wait(const JsonValue& args, const ToolExecutionContext& context) {
  auto& team = Team::instance();
  const std::string me = context.session_id;
  const std::string lead = lead_of(me);
  std::string id = args.value("task_id", "");
  // Waiting "for the lead" (or for yourself) means waiting for a message.
  if (id == kLead || id == me) id.clear();
  int timeout_s = 600;
  if (args.contains("timeout_s") && args["timeout_s"].is_number()) {
    timeout_s = std::clamp(args["timeout_s"].get<int>(), 1, 3600);
  }
  if (id.empty() && !team.has(me) && me == lead) {
    bool any_running = false;
    for (const auto& [tid, t] : team.running_of(lead)) {
      if (t.background) any_running = true;
    }
    if (!any_running) {
      return tool_text("task wait", "Nothing to wait for: no background task is running and "
                                    "no message is waiting.");
    }
  }
  team.wait(me, id, std::chrono::seconds(timeout_s), context.abort_flag);
  std::string text;
  if (!id.empty()) {
    if (auto report = team.take_report(id)) {
      text = *report;
    } else if (auto t = team.running(id)) {
      text = "Task " + id + " (" + t->title + ") is still running.";
    } else {
      text = task_status(id, context).value("output", "Task " + id + ": unknown");
    }
  }
  for (auto& item : team.take(me)) text += (text.empty() ? "" : "\n\n") + item;
  if (text.empty()) text = "Nothing arrived within " + std::to_string(timeout_s) + "s.";
  return tool_text("task wait", text);
}

JsonValue task_message(const JsonValue& args, const ToolExecutionContext& context) {
  const std::string to = args.value("task_id", "");
  const std::string text = args.value("prompt", "");
  if (to.empty() || text.empty()) {
    return {{"error", "message needs task_id (a subagent, or \"lead\") and prompt."}};
  }
  const std::string me = context.session_id;
  const std::string lead = lead_of(me);
  std::string recipient = to;
  if (to == kLead) {
    if (me == lead) return {{"error", "You are the lead; message a subagent by task_id."}};
    recipient = lead;
  } else if (!Team::instance().running(to)) {
    if (in_team(lead, to)) {
      return {{"error", "Task " + to + " is not running; resume it with action run, task_id "
                        "and prompt."}};
    }
    return {{"error", "Unknown or finished task_id " + to + "."}};
  }
  const std::string from = me == lead ? std::string(kLead) : me;
  std::string label = from;
  if (me != lead) {
    const std::string title = qcode::session::get_session_title(me);
    if (!title.empty()) label += " (" + title + ")";
  }
  Team::instance().post(recipient, "[Message from " + label + "]\n" + text +
                                       "\n(reply: task action \"message\", task_id \"" + from +
                                       "\")");
  return tool_text("task message", "Sent to " + to + ".");
}

JsonValue task_run(const JsonValue& args, const ToolExecutionContext& context) {
  const std::string prompt = args.value("prompt", "");
  const std::string description = args.value("description", "");
  const std::string resume_id = args.value("task_id", "");
  std::string model = args.value("model", "");
  const bool background = args.value("background", false);
  if (prompt.empty()) {
    return {{"error", "task needs a prompt: the full, self-contained instructions."}};
  }
  if (!context.subagent_runner) {
    return {{"error", "Subagents are not available in this context."}};
  }
  const std::string lead = lead_of(context.session_id);

  std::string session_id;
  std::string title;
  if (!resume_id.empty()) {
    if (!in_team(lead, resume_id)) {
      return {{"error", "Unknown task_id " + resume_id + " (not a subagent of this team)."}};
    }
    if (TaskTool::is_session_running(resume_id)) {
      return {{"error", "Task " + resume_id + " is still running; use action \"message\" "
                        "to talk to it or \"wait\" for it."}};
    }
    if (model.empty()) model = session_model(resume_id);
    session_id = resume_id;
    title = qcode::session::get_session_title(resume_id);
  } else {
    session_id = new_child_session_id();
    title = description;
    if (title.empty()) {
      title = prompt.substr(0, std::min(prompt.find('\n'), std::size_t{48}));
    }
    qcode::session::ensure_session_row(session_id, title, "", "", context.workspace, lead);
  }
  qcode::session::save_message(session_id, "User", prompt);

  JsonValue sub_args = {{"prompt", prompt},
                        {"description", title},
                        {"session_id", session_id},
                        {"parent_session_id", lead}};
  if (!model.empty()) sub_args["model"] = model;
  if (!resume_id.empty()) sub_args["resume"] = true;

  RunningTask task;
  task.lead_session_id = lead;
  task.owner_session_id = context.session_id;
  task.title = title;
  task.model = model.empty() ? std::string("(caller's model)") : model;

  if (background) {
    auto abort_flag = std::make_shared<std::atomic<bool>>(false);
    task.abort_flag = abort_flag;
    Team::instance().start_background(
        session_id, task,
        [runner = context.subagent_runner, sub_args, abort_flag, session_id, title,
         model]() -> JsonValue {
          JsonValue sub;
          try {
            sub = runner(sub_args, abort_flag);
          } catch (const std::exception& e) {
            sub = {{"error", std::string("subagent crashed: ") + e.what()}};
          }
          if (abort_flag->load() && sub.contains("error")) sub = {{"error", "killed"}};
          return finish_task(session_id, title, model, std::move(sub));
        });
    return {{"title", "task (background): " + title},
            {"output", "Started background task " + session_id + " (" + title +
                           "). Keep working; its report arrives as a message."},
            {"metadata",
             {{"sessionId", session_id}, {"task_id", session_id}, {"status", "running"},
              {"background", true}}}};
  }

  task.abort_flag = context.abort_flag;
  Team::instance().add(session_id, task);
  struct Unregister {
    std::string id;
    ~Unregister() { Team::instance().remove(id); }
  } unregister{session_id};
  JsonValue sub;
  try {
    sub = context.subagent_runner(sub_args, context.abort_flag);
  } catch (const std::exception& e) {
    sub = {{"error", std::string("subagent crashed: ") + e.what()}};
  }
  return finish_task(session_id, title, model, std::move(sub));
}

}  // namespace

JsonValue TaskTool::execute(const JsonValue& args, const ToolExecutionContext& context) {
  const std::string action = args.value("action", "run");
  if (action == "run") return task_run(args, context);
  if (action == "status") return task_status(args.value("task_id", ""), context);
  if (action == "wait") return task_wait(args, context);
  if (action == "message") return task_message(args, context);
  if (action == "kill") {
    const std::string id = args.value("task_id", "");
    if (id.empty()) return {{"error", "kill needs task_id."}};
    if (Team::instance().kill(id)) return tool_text("task kill", "Stopping task " + id + ".");
    return {{"error", "Task " + id + " is not a running background task."}};
  }
  return {{"error", "Unknown action '" + action + "'. Use run, status, wait, kill or message."}};
}

std::vector<std::string> TaskTool::take_notices(const std::string& session_id) {
  return Team::instance().take(session_id);
}

bool TaskTool::has_notices(const std::string& session_id) {
  return Team::instance().has(session_id);
}

void TaskTool::set_notice_listener(std::function<void()> listener) {
  Team::instance().set_listener(std::move(listener));
}

void TaskTool::shutdown_background(std::chrono::milliseconds timeout) {
  Team::instance().shutdown(timeout);
}

JsonValue TaskTool::parameters() {
  auto str = [](const char* d) { return JsonValue{{"type", "string"}, {"description", d}}; };
  return {
      {"type", "object"},
      {"properties",
       {{"action",
         {{"type", "string"},
          {"enum", {"run", "status", "wait", "kill", "message"}},
          {"default", "run"},
          {"description",
           "run (default): start or resume a subagent. status: the team, or one task_id. "
           "wait: block until a background task finishes or a message arrives. kill: stop "
           "a background task. message: send prompt to task_id."}}},
        {"prompt", str("run: complete instructions (goal, paths, constraints, what to report). "
                       "message: the message text.")},
        {"description", str("run: short name (3-6 words) shown in the UI.")},
        {"model", str("run: provider:model from the catalog. Omit for your own model.")},
        {"background", {{"type", "boolean"},
                        {"description", "run: return at once; the report arrives later as a "
                                        "message."}}},
        {"task_id", str("run: resume this finished subagent. status/wait/kill: the task. "
                        "message: the recipient (a subagent, or \"lead\").")},
        {"timeout_s", {{"type", "integer"},
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

}  // namespace qcode
