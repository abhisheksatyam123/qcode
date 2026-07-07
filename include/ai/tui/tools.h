#pragma once

#include <string>
#include <vector>

#include <ai/types/tool.h>

namespace ai {
namespace tui {

// ── Tool descriptors for the system prompt ──

struct ToolDescriptor {
  std::string name;         // e.g. "bash", "task"
  std::string description;  // one-line summary
  std::string schema_text;  // human-readable parameter schema
  bool enabled{true};
};

// ── Tool configuration ──

struct ToolConfig {
  bool enable_bash{true};
  bool enable_task{true};
  // Reserved for future tools
};

// ── Tools module ──

class Tools {
 public:
  // Get tool descriptors for the system prompt
  static std::vector<ToolDescriptor> descriptors();

  // Build the tool-description section for the system prompt
  static std::string build_tool_section(const ToolConfig& cfg = {});

  // Build the agent's tool definitions (for ai::ToolSet)
  static ai::ToolSet build_definitions(const ToolConfig& cfg = {});

  // Format a single tool call for display
  static std::string format_tool_call(const std::string& tool_name,
                                      const std::string& args,
                                      int step_number = 0,
                                      int max_steps = 0);

  // Format a single tool result for display
  static std::string format_tool_result(const std::string& tool_name,
                                        bool success,
                                        const std::string& result_or_error,
                                        int truncate_at = 500,
                                        double duration_seconds = -1.0);
};

} // namespace tui
} // namespace ai
