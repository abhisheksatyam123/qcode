#pragma once

#include <qcode/core/tool.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace qcode {

using JsonValue = nlohmann::json;

// ── Task Tool Schema ──
// Direct port of opencode's TaskTool contract (src/tool/task/contract/port.ts)

namespace TaskToolSchema {

inline JsonValue spawn_parameters() {
  JsonValue s;
  s["type"] = "object";
  s["properties"] = JsonValue::object();
  auto& p = s["properties"];

  // op (default "spawn")
  p["op"] = JsonValue{{"type", "string"}, {"enum", {"spawn", "result", "kill", "pause", "resume", "resurrect", "model"}},
                       {"description", "Operation type. Default spawn."}};

  // Common spawn fields
  p["description"] = JsonValue{{"type", "string"}, {"description", "Short label for the subagent task."}};
  p["subagent_type"] = JsonValue{{"type", "string"},
    {"description", "Type of subagent to delegate to. Defaults to mode, or general."}};
  p["agent"] = JsonValue{{"type", "string"}, {"description", "Alias for subagent_type."}};
  p["prompt"] = JsonValue{{"type", "string"}, {"description", "Plain-language task for the subagent."}};
  p["task"] = JsonValue{{"type", "string"}, {"description", "Preferred alias for prompt."}};

  // Delegation mode
  p["mode"] = JsonValue{{"type", "string"}, {"enum", {"explore", "implement", "verify"}},
                         {"description", "explore=read-only analysis, implement=bounded edits, verify=tests/audits."}};
  p["objective"] = JsonValue{{"type", "string"}, {"description", "One-sentence outcome for this subagent."}};

  // Scope
  p["scope"] = JsonValue{{"type", "array"}, {"items", JsonValue{{"type", "string"}}},
                          {"description", "Files, directories, modules, or commands the subagent may inspect."}};
  p["out_of_scope"] = JsonValue{{"type", "array"}, {"items", JsonValue{{"type", "string"}}},
                                 {"description", "Areas the subagent must not inspect or change."}};

  // Policy
  p["filesystem_policy"] = JsonValue{{"type", "string"}, {"enum", {"bash-only"}},
                                       {"description", "Filesystem access policy."}};
  p["output_format"] = JsonValue{{"type", "string"}, {"enum", {"structured-summary"}},
                                   {"description", "Required result shape."}};
  p["can_edit"] = JsonValue{{"type", "boolean"}, {"description", "Whether subagent can edit files."}};

  // Paths
  p["allowed_paths"] = JsonValue{{"type", "array"}, {"items", JsonValue{{"type", "string"}}},
                                   {"description", "Non-empty path allow-list for implement mode."}};
  p["forbidden_paths"] = JsonValue{{"type", "array"}, {"items", JsonValue{{"type", "string"}}},
                                     {"description", "Path deny-list."}};

  // Budget
  p["budget"] = JsonValue{
    {"type", "object"},
    {"properties", JsonValue{
      {"max_files", JsonValue{{"type", "integer"}, {"exclusiveMinimum", 0}}},
      {"max_output_chars", JsonValue{{"type", "integer"}, {"exclusiveMinimum", 0}}},
      {"timeout_ms", JsonValue{{"type", "integer"}, {"exclusiveMinimum", 0}}}
    }}
  };

  // Model
  p["model"] = JsonValue{{"type", "string"}, {"description", "Optional explicit model override."}};
  p["models"] = JsonValue{{"type", "array"}, {"items", JsonValue{{"type", "string"}}},
                            {"description", "Optional provider/model fallback candidates."}};
  p["task_id"] = JsonValue{{"type", "string"}, {"description", "Resume a previous task by id."}};

  // Background
  p["background"] = JsonValue{{"type", "boolean"}, {"description", "Start subagent in background."}};
  p["run_in_background"] = JsonValue{{"type", "boolean"}, {"description", "Legacy alias for background."}};

  return s;
}

inline JsonValue result_parameters() {
  JsonValue s;
  s["type"] = "object";
  s["properties"] = JsonValue::object();
  auto& p = s["properties"];

  p["op"] = JsonValue{{"type", "string"}, {"const", "result"}};
  p["background_task_id"] = JsonValue{{"type", "string"}, {"description", "Background task id."}};
  p["timeout_ms"] = JsonValue{{"type", "integer"}, {"minimum", 0},
                                {"description", "How long to wait in ms. 0 = nonblocking status check."}};

  return s;
}

inline JsonValue lifecycle_or_model_parameters() {
  JsonValue s;
  s["type"] = "object";
  s["properties"] = JsonValue::object();
  auto& p = s["properties"];

  p["op"] = JsonValue{{"type", "string"}, {"enum", {"kill", "pause", "resume", "resurrect", "model"}}};
  p["task_id"] = JsonValue{{"type", "string"}};
  p["pid"] = JsonValue{{"type", "string"}};
  p["reason"] = JsonValue{{"type", "string"}, {"maxLength", 280}};
  p["model"] = JsonValue{{"type", "string"}};

  return s;
}

}  // namespace TaskToolSchema

// ── TaskTool executor ──

class TaskTool {
 public:
  static constexpr const char* kDescription =
      "Delegate bounded work to a subagent. Default op is spawn. "
      "Provide a prompt (or task/objective). Optional: subagent_type "
      "(default general), mode (explore|implement|verify), background, "
      "model. Collect with op=result and background_task_id. "
      "Cancel with op=kill.";

  static JsonValue execute(const JsonValue& args, const ToolExecutionContext& context);
  static Tool definition();
  static void clear_background_tasks();

 private:
  static JsonValue exec_spawn(const JsonValue& args, const ToolExecutionContext& context);
  static JsonValue exec_result(const JsonValue& args);
  static JsonValue exec_lifecycle(const JsonValue& args, const std::string& op);
  static JsonValue exec_model(const JsonValue& args);
};

}  // namespace qcode
