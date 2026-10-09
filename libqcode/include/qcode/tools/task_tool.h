#pragma once

#include <qcode/core/tool.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace qcode {

using JsonValue = nlohmann::json;

// `task` delegates one self-contained job to a subagent and blocks until the
// subagent reports back. Each subagent works in its own child session (shown
// in the UI) and is part of its parent's turn: the parent's abort stops it.
// Parallelism comes from issuing several task calls in one message; the tool
// executor runs them concurrently.
class TaskTool {
 public:
  static constexpr const char* kDescription =
      "Delegate a self-contained job to a subagent and wait for its final report. "
      "The subagent cannot see this conversation, so put every detail it needs in "
      "`prompt`. Issue several task calls in one message to run them in parallel. "
      "mode: explore (read-only, default), implement (may edit files), verify "
      "(build/test/audit). model: omit it and the router picks a free model by "
      "learned success; paid models run only when named. Rate reports with rate_task. "
      "background: true returns at once with a task_id and keeps the subagent "
      "running; its report arrives later as a message (or use action \"wait\"). "
      "task_id + prompt resumes that subagent with its own history (follow-up "
      "questions, fixes). action: \"status\" | \"wait\" | \"kill\" with task_id "
      "(status/wait without one: all of your background tasks).";

  static JsonValue parameters();
  static Tool definition();
  static JsonValue execute(const JsonValue& args, const ToolExecutionContext& context);

  // `rate_task`: the lead scores finished task reports; the scores train
  // which free models get future tasks (qcode/session/subagent_stats.h).
  static constexpr const char* kRateDescription =
      "Rate finished task reports 1-5 (5 = correct and complete, 3 = usable with "
      "fixes, 1 = wrong/useless). Ratings train which free models get future tasks; "
      "rate every report you relied on. Returns the learned model board (success per "
      "mode, rating, latency, resting); call with no ratings to just see it.";

  static JsonValue rate_parameters();
  static Tool rate_definition();
  static JsonValue execute_rate(const JsonValue& args, const ToolExecutionContext& context);

  // Subagents of `parent_session_id` for the UI: running ones, then finished
  // child sessions. Shape: {output, metadata: {tasks: [{task_id, sessionId,
  // parent_session_id, description, agent, mode, model, status}]}}.
  static JsonValue list_tasks(const std::string& parent_session_id = "");
  static bool is_session_running(const std::string& session_id);
  // Stop running subagents that are, or belong to, `session_id`.
  static void delete_session_tasks(const std::string& session_id);

  // Repairs Cursor's shredded Task protobuf arguments into {prompt,
  // description, mode, difficulty, model}. Only the Cursor exec path needs it.
  static JsonValue normalize_spawn_args(JsonValue args);
  // Child session id from a task tool result (current and legacy shapes).
  static std::string session_id_from_result(const JsonValue& result);

  // ── Background subagents ──
  // Reports of background tasks of `parent_session_id` that finished and
  // were not yet handed to the lead, as message texts; marks them delivered.
  // The lead's tool loop injects them into its next request; an idle TUI
  // queues them as a prompt.
  static std::vector<std::string> take_notices(const std::string& parent_session_id);
  static bool has_notices(const std::string& parent_session_id);
  // Called (on the subagent's thread) whenever a background task finishes.
  static void set_notice_listener(std::function<void()> listener);
  // Stop every background task and wait up to `timeout` for them to end.
  static void shutdown_background(std::chrono::milliseconds timeout);
};

}  // namespace qcode
