#pragma once

#include <qcode/core/tool.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace qcode {

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

}  // namespace qcode
