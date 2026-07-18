#pragma once

#include <qcode/types/tool.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ai {

/// Build a tool with basic parameter schema validation.
Tool create_simple_tool(const std::string& name,
                        const std::string& description,
                        const std::map<std::string, std::string>& parameters,
                        ToolExecuteFunction execute_func);

/// Build an async tool.
Tool create_simple_async_tool(
    const std::string& name,
    const std::string& description,
    const std::map<std::string, std::string>& parameters,
    AsyncToolExecuteFunction execute_func);

/// Build a ToolSet from name/tool pairs.
ToolSet create_tool_set(
    const std::vector<std::pair<std::string, Tool>>& tool_list);

/// Build a ToolCall (useful for tests).
ToolCall create_tool_call(const std::string& tool_name,
                          const JsonValue& arguments,
                          const std::string& call_id = "");

/// Generate a unique tool call id.
std::string generate_tool_call_id();

}  // namespace ai
