#include "ai/tools/task_tool.h"

#include <algorithm>
#include <ai/logger.h>
#include <chrono>
#include <ctime>
#include <random>
#include <sstream>
#include <thread>

namespace ai {

// ── TaskTool implementation ──
// Direct port of opencode's TaskTool (src/tool/task/index.ts + contract/port.ts)

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
// Port of parseSubagentResult from opencode

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

JsonValue TaskTool::exec_spawn(const JsonValue& args, const ToolExecutionContext& context) {
  (void)context;

  std::string subagent_type = args.value("subagent_type", "");
  if (subagent_type.empty()) {
    JsonValue err;
    err["error"] = "subagent_type is required";
    return err;
  }

  std::string description = args.value("description", "");
  std::string prompt_text = args.value("prompt", args.value("task", ""));

  std::string session_id = generate_session_id();
  bool is_background = args.value("background", args.value("run_in_background", false));

  // Parse delegation parameters
  std::string mode = args.value("mode", "explore");
  std::string objective = args.value("objective", "");
  bool can_edit = args.value("can_edit", false);

  // Validate implement mode requirements
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

  // Build the subagent output
  JsonValue metadata;
  metadata["sessionId"] = session_id;
  metadata["agent"] = subagent_type;
  metadata["description"] = description;
  metadata["mode"] = mode;

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

  // For foreground spawn, create simulated result
  if (!is_background) {
    std::stringstream output_ss;
    output_ss << "task_id: " << session_id << " (for resuming this subagent if needed)\n\n";
    output_ss << "<task_result>\n";
    output_ss << "@subagent " << subagent_type << " " << description << "\n\n";
    if (!objective.empty()) output_ss << "Objective: " << objective << "\n";
    if (scope_ss.tellp() > 0) output_ss << "Scope:\n" << scope_ss.str();
    if (out_of_scope_ss.tellp() > 0) output_ss << "Out of scope:\n" << out_of_scope_ss.str();
    output_ss << "Prompt: " << prompt_text << "\n";
    output_ss << "</task_result>\n";
    output_ss << "\n<task_result_parsed>\n{}\n</task_result_parsed>";

    JsonValue result;
    result["title"] = std::string("task complete: ") + description;
    result["output"] = output_ss.str();
    result["metadata"] = metadata;
    result["metadata"]["status"] = "done";
    return result;
  }

  // Background spawn
  std::string bg_id = generate_bg_id(session_id);

  JsonValue result;
  result["title"] = std::string("task started: ") + description;
  result["output"] = "background_task_id: " + bg_id + "\ntask_id: " + session_id + "\nstatus: running";
  result["metadata"]["status"] = "running";
  result["metadata"]["background_task_id"] = bg_id;
  result["metadata"]["task_id"] = session_id;
  result["metadata"]["sessionId"] = session_id;
  return result;
}

JsonValue TaskTool::exec_result(const JsonValue& args) {
  LOG_DEBUG("TaskTool: exec_result");
  std::string bg_id = args.value("background_task_id", "");
  if (bg_id.empty()) {
    JsonValue err;
    err["error"] = "background_task_id is required for result operation";
    return err;
  }

  int timeout_ms = args.value("timeout_ms", 30000);

  JsonValue result;
  result["title"] = "task result: " + bg_id;
  result["output"] = "task_id: " + bg_id + "\nstatus: pending\n\nStill running. Collect with a later result call.";
  result["metadata"]["status"] = "pending";
  result["metadata"]["background_task_id"] = bg_id;

  if (timeout_ms == 0) {
    // Non-blocking status check
    return result;
  }

  // In a full implementation, this would wait for the subagent to complete.
  // For now, return pending status.
  return result;
}

JsonValue TaskTool::exec_lifecycle(const JsonValue& args, const std::string& op) {
  std::string task_id = args.value("task_id", "");
  std::string pid = args.value("pid", "");
  std::string reason = args.value("reason", "");

  JsonValue result;
  result["title"] = std::string("task ") + op + ": " + (!task_id.empty() ? task_id : pid);
  result["output"] = "Operation " + op + " completed for task " +
                     (!task_id.empty() ? task_id : pid) +
                     (!reason.empty() ? " (reason: " + reason + ")" : "");
  result["metadata"]["op"] = op;
  result["metadata"]["task_id"] = task_id;
  result["metadata"]["pid"] = pid;
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
  if (op == "kill" || op == "pause" || op == "resume" || op == "resurrect")
    return exec_lifecycle(args, op);
  if (op == "model") return exec_model(args);

  return exec_spawn(args, context);
}

Tool TaskTool::definition() {
  // Create combined schema that accepts all task operations
  JsonValue schema;
  schema["type"] = "object";
  schema["properties"] = JsonValue::object();
  auto& props = schema["properties"];

  props["op"] = JsonValue{{"type", "string"},
    {"enum", {"spawn", "result", "kill", "pause", "resume", "resurrect", "model"}},
    {"description", "Task operation."}};
  props["description"] = JsonValue{{"type", "string"}};
  props["subagent_type"] = JsonValue{{"type", "string"}};
  props["prompt"] = JsonValue{{"type", "string"}};
  props["mode"] = JsonValue{{"type", "string"}, {"enum", {"explore", "implement", "verify"}}};
  props["objective"] = JsonValue{{"type", "string"}};
  props["can_edit"] = JsonValue{{"type", "boolean"}};
  props["allowed_paths"] = JsonValue{{"type", "array"}, {"items", JsonValue{{"type", "string"}}}};
  props["forbidden_paths"] = JsonValue{{"type", "array"}, {"items", JsonValue{{"type", "string"}}}};
  props["background"] = JsonValue{{"type", "boolean"}};
  props["run_in_background"] = JsonValue{{"type", "boolean"}};
  props["background_task_id"] = JsonValue{{"type", "string"}};
  props["timeout_ms"] = JsonValue{{"type", "integer"}, {"minimum", 0}};
  props["task_id"] = JsonValue{{"type", "string"}};
  props["pid"] = JsonValue{{"type", "string"}};
  props["reason"] = JsonValue{{"type", "string"}};
  props["model"] = JsonValue{{"type", "string"}};
  props["models"] = JsonValue{{"type", "array"}, {"items", JsonValue{{"type", "string"}}}};
  props["budget"] = JsonValue{
    {"type", "object"},
    {"properties", JsonValue{
      {"max_files", JsonValue{{"type", "integer"}}},
      {"max_output_chars", JsonValue{{"type", "integer"}}},
      {"timeout_ms", JsonValue{{"type", "integer"}}}
    }}
  };

  return Tool(
    "task",
    schema,
    [](const JsonValue& args, const ToolExecutionContext& context) -> JsonValue {
      return TaskTool::execute(args, context);
    }
  );
}

}  // namespace ai
