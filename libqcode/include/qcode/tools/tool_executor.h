#pragma once

// ToolExecutor — runs one or more tool calls against a ToolSet.
#include <qcode/types/generate_options.h>
#include <qcode/types/message.h>
#include <qcode/types/tool.h>

namespace qcode {

/// Executes tool calls (single, batch, sync/async) against a ToolSet.
class ToolExecutor {
 public:
  static ToolResult execute_tool(const ToolCall& tool_call,
                                 const ToolSet& tools,
                                 const Messages& messages = {},
                                 const GenerateOptions* options = nullptr);

  static std::vector<ToolResult> execute_tools(
      const std::vector<ToolCall>& tool_calls,
      const ToolSet& tools,
      const Messages& messages = {},
      bool parallel = true,
      const GenerateOptions* options = nullptr);

  static std::vector<ToolResult> execute_tools_with_options(
      const std::vector<ToolCall>& tool_calls,
      const GenerateOptions& options,
      bool parallel = false);

  static bool validate_tool_call(const ToolCall& tool_call, const Tool& tool);

  static bool tool_exists(const std::string& tool_name, const ToolSet& tools);

 private:
  static ToolResult execute_sync_tool(const ToolCall& tool_call,
                                      const Tool& tool,
                                      const ToolExecutionContext& context,
                                      const GenerateOptions* options = nullptr);

  static ToolResult execute_async_tool(const ToolCall& tool_call,
                                       const Tool& tool,
                                       const ToolExecutionContext& context,
                                       const GenerateOptions* options = nullptr);

  static bool validate_json_schema(const JsonValue& data,
                                   const JsonValue& schema);
};

}  // namespace qcode
