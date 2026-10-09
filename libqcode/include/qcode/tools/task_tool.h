#pragma once

#include <qcode/core/tool.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace qcode {

using JsonValue = nlohmann::json;

// `task` is the one tool for working with subagents. There are two kinds of
// agent: the orchestrator (a top-level session, addressed as "lead") and its
// subagents (child sessions, addressed by task_id). The orchestrator and every
// subagent have this tool. All subagents of one orchestrator form a flat team:
// a subagent that starts another one starts a sister (a child of the lead).
//
// Actions: run (start or resume a subagent; blocking, or background: true),
// status (the team, or one task), wait (until a task finishes or a message
// arrives), kill (stop a background task) and message (to a running
// subagent or the lead). Messages and background reports land in the
// recipient's inbox and are handed to its next model request.
class TaskTool {
 public:
  static constexpr const char* kDescription =
      "Work with subagents: other model instances with their own context. A subagent "
      "cannot see your conversation, so `prompt` must hold every detail it needs. "
      "action run (default) starts one on `model` (provider:model from the catalog; "
      "omit for your own model) and returns its final report; several calls in one "
      "message run in parallel. background: true returns a task_id at once; the report "
      "arrives later as a message. task_id + prompt resumes a finished subagent with "
      "its history. status lists the team (or one task_id); wait blocks until a "
      "background task finishes or a message arrives; kill stops a background task; "
      "message sends `prompt` to a running subagent or to \"lead\" (the orchestrator).";

  static JsonValue parameters();
  static Tool definition();
  static JsonValue execute(const JsonValue& args, const ToolExecutionContext& context);

  // Subagents of the team led by `lead_session_id` for the UI: running ones,
  // then finished child sessions. Shape: {output, metadata: {tasks:
  // [{task_id, sessionId, parent_session_id, description, agent, model, status}]}}.
  static JsonValue list_tasks(const std::string& lead_session_id = "");
  static bool is_session_running(const std::string& session_id);
  // Stop running subagents that are, or belong to, `session_id`.
  static void delete_session_tasks(const std::string& session_id);

  // Repairs Cursor's shredded Task protobuf arguments into {prompt,
  // description, model}. Only the Cursor exec path needs it.
  static JsonValue normalize_spawn_args(JsonValue args);
  // Child session id from a task tool result (current and legacy shapes).
  static std::string session_id_from_result(const JsonValue& result);

  // ── Inbox ──
  // Messages waiting for `session_id` (background task reports and messages
  // from the team), as message texts; taking them empties the inbox. The
  // lead's tool loop and every subagent step inject them into the next
  // request; an idle TUI queues the lead's as a prompt.
  static std::vector<std::string> take_notices(const std::string& session_id);
  static bool has_notices(const std::string& session_id);
  // Called whenever something lands in an inbox (on the sender's thread).
  static void set_notice_listener(std::function<void()> listener);
  // Stop every background task and wait up to `timeout` for them to end.
  static void shutdown_background(std::chrono::milliseconds timeout);
};

}  // namespace qcode
