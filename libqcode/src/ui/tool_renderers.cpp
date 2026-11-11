#include <qcode/ui/tool_renderers.h>

namespace qcode {

std::string tool_icon(const std::string& tool_name) {
    if (tool_name == "bash" || tool_name == "shell" || tool_name == "run_command")
        return "$";
    if (tool_name == "task" || tool_name == "dispatch_agent")
        return "🤖";
    return "⚙";
}

std::string tool_display_name(const std::string& tool_name) {
    if (tool_name == "bash" || tool_name == "shell" || tool_name == "run_command")
        return "Bash";
    if (tool_name == "task" || tool_name == "dispatch_agent")
        return "Task";
    return tool_name;
}

}  // namespace qcode
