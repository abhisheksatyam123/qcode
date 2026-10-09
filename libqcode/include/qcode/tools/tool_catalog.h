#pragma once

#include <string>
#include <vector>

#include <qcode/core/tool.h>

namespace qcode {

// ── Tool descriptors for the system prompt ──

struct ToolDescriptor {
  std::string name;         // e.g. "bash", "task", "image"
  std::string description;  // one-line summary
  std::string schema_text;  // human-readable parameter schema
  bool enabled{true};
};

// ── Tool configuration ──

struct ToolConfig {
  bool enable_bash{true};
  bool enable_task{true};  // the task tool (orchestrator and subagents)
  bool enable_image{false};

  static ToolConfig orchestrator(bool vision = false) {
    return ToolConfig{true, true, vision};
  }
  static ToolConfig subagent(bool vision = false) {
    return ToolConfig{true, true, vision};
  }
};

// ── Agent tool catalog (prompt + ToolSet) ──

class ToolCatalog {
 public:
  // Get tool descriptors for the system prompt
  static std::vector<ToolDescriptor> descriptors();

  // Build the tool-description section for the system prompt
  static std::string build_tool_section(const ToolConfig& cfg = {});

  // Build the agent's tool definitions (for qcode::ToolSet)
  static qcode::ToolSet build_definitions(const ToolConfig& cfg = {});

  // Format a single tool call for display
  static std::string format_tool_call(const std::string& tool_name,
                                      const std::string& args,
                                      int step_number = 0,
                                      int max_steps = 0);
};

} // namespace qcode
