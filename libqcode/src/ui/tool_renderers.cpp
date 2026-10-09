#include <qcode/ui/tool_renderers.h>

namespace qcode {

std::string tool_icon(const std::string& tool_name) {
    if (tool_name == "bash" || tool_name == "shell" || tool_name == "run_command")
        return "$";
    if (tool_name == "read_file" || tool_name == "view_file")
        return "📄";
    if (tool_name == "write_file" || tool_name == "edit_file")
        return "✏️";
    if (tool_name == "search" || tool_name == "grep" || tool_name == "ripgrep")
        return "🔍";
    if (tool_name == "task" || tool_name == "dispatch_agent")
        return "🤖";
    if (tool_name == "list_files" || tool_name == "ls")
        return "📁";
    return "⚙";
}

std::string tool_display_name(const std::string& tool_name) {
    if (tool_name == "bash" || tool_name == "shell" || tool_name == "run_command")
        return "Bash";
    if (tool_name == "read_file" || tool_name == "view_file")
        return "Read File";
    if (tool_name == "write_file")
        return "Write File";
    if (tool_name == "edit_file")
        return "Edit File";
    if (tool_name == "search" || tool_name == "grep" || tool_name == "ripgrep")
        return "Search";
    if (tool_name == "task" || tool_name == "dispatch_agent")
        return "Task";
    if (tool_name == "list_files" || tool_name == "ls")
        return "List Files";
    return tool_name;
}

}  // namespace qcode
