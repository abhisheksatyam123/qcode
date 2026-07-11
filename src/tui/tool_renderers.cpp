#include <ai/tui/tool_renderers.h>
#include <ai/tui/message_render.h>
#include <ai/tui/views.h>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <algorithm>

namespace ai {
namespace tui {

using namespace ftxui;

namespace {

Color prompt_green() { return Color::RGB(0x4E, 0xC9, 0xB0); }
Color command_fg() { return Color::RGB(0xE5, 0xE5, 0xE5); }
Color muted_fg() { return Color::RGB(0x6A, 0x6A, 0x6A); }
Color success_fg() { return Color::RGB(0x7F, 0xDB, 0x8C); }

std::string json_str(const nlohmann::json& j,
                     std::initializer_list<const char*> keys,
                     const std::string& fallback = "") {
    for (const auto& key : keys) {
        if (j.contains(key) && j[key].is_string()) {
            return j[key].get<std::string>();
        }
    }
    return fallback;
}

}  // namespace

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

Element RenderBashResult(const nlohmann::json& args,
                          const nlohmann::json& result,
                          bool is_error, double /*duration_ms*/,
                          const std::string& theme) {
    Elements content;

    const std::string command = json_str(args, {"command", "cmd", "script"});
    const std::string workdir = json_str(args, {"workdir"});

    if (!command.empty()) {
        content.push_back(hbox({
            text("$ ") | bold | color(prompt_green()),
            text(command.size() > 200 ? command.substr(0, 197) + "..." : command) |
                bold | color(command_fg()),
        }));
    }

    if (!workdir.empty()) {
        content.push_back(hbox({
            text("in ") | dim | color(muted_fg()),
            text(workdir) | dim | color(Color::RGB(0x7A, 0xA2, 0xF7)),
        }));
    }

    std::string output;
    int exit_code = 0;
    bool has_exit = false;
    if (result.is_object()) {
        output = json_str(result, {"output"});
        if (result.contains("metadata") && result["metadata"].is_object() &&
            result["metadata"].contains("exit")) {
            exit_code = result["metadata"]["exit"].get<int>();
            has_exit = true;
        }
    } else if (result.is_string()) {
        output = result.get<std::string>();
    }

    if (!output.empty()) {
        content.push_back(text(""));
        content.push_back(render_truncated_output(output, 28, theme, is_error));
    }

    if (has_exit) {
        const auto exit_color = exit_code == 0 ? success_fg() : Color::Red;
        content.push_back(text(""));
        content.push_back(hbox({
            text(exit_code == 0 ? "✓" : "✗") | color(exit_color) | bold,
            text(" exit " + std::to_string(exit_code)) | color(exit_color),
        }));
    }

    return vbox(std::move(content));
}

Element RenderTaskResult(const nlohmann::json& args,
                          const nlohmann::json& result,
                          bool is_error, double /*duration_ms*/,
                          const std::string& theme) {
    Elements content;
    const std::string desc = json_str(args, {"description", "prompt", "task"});
    if (!desc.empty()) {
        content.push_back(hbox({
            text("$ ") | bold | color(prompt_green()),
            text("task " + desc) | color(command_fg()),
        }));
    }

    std::string output;
    if (result.is_string()) output = result.get<std::string>();
    else if (result.is_object()) {
        output = json_str(result, {"output", "result", "summary"});
    }
    if (!output.empty()) {
        content.push_back(text(""));
        content.push_back(render_truncated_output(output, 18, theme, is_error));
    }
    return vbox(std::move(content));
}

Element RenderFileResult(const std::string& tool_name,
                          const nlohmann::json& args,
                          const nlohmann::json& result,
                          bool is_error, double /*duration_ms*/,
                          const std::string& theme) {
    Elements content;
    const std::string path =
        json_str(args, {"path", "file", "file_path", "filename"});
    if (!path.empty()) {
        content.push_back(hbox({
            text("$ ") | bold | color(prompt_green()),
            text(tool_name + " " + path) | color(command_fg()),
        }));
    }

    std::string output;
    if (result.is_object()) output = json_str(result, {"output", "content"});
    else if (result.is_string()) output = result.get<std::string>();
    if (!output.empty()) {
        content.push_back(text(""));
        content.push_back(render_truncated_output(output, 16, theme, is_error));
    }
    return vbox(std::move(content));
}

Element RenderSearchResult(const nlohmann::json& args,
                            const nlohmann::json& result,
                            bool is_error, double /*duration_ms*/,
                            const std::string& theme) {
    Elements content;
    const std::string query = json_str(args, {"query", "pattern", "search"});
    const std::string path = json_str(args, {"path", "directory", "dir"});
    if (!query.empty()) {
        content.push_back(hbox({
            text("$ ") | bold | color(prompt_green()),
            text("rg \"" + query + "\"" + (path.empty() ? "" : " " + path)) |
                color(command_fg()),
        }));
    }

    std::string output;
    if (result.is_string()) output = result.get<std::string>();
    else if (result.is_object()) {
        output = json_str(result, {"output", "matches", "results"});
    }
    if (!output.empty()) {
        content.push_back(text(""));
        content.push_back(render_truncated_output(output, 22, theme, is_error));
    }
    return vbox(std::move(content));
}

Element RenderGenericResult(const std::string& tool_name,
                             const nlohmann::json& args,
                             const nlohmann::json& result,
                             bool is_error, double /*duration_ms*/,
                             const std::string& theme) {
    Elements content;
    std::string summary = tool_name;
    if (args.is_object()) {
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (it.key() == "description" || it.key() == "desc") continue;
            if (it.value().is_string()) {
                auto val = it.value().get<std::string>();
                if (val.size() > 100) val = val.substr(0, 97) + "...";
                summary += " " + val;
                break;
            }
        }
    }
    content.push_back(hbox({
        text("$ ") | bold | color(prompt_green()),
        text(summary) | color(command_fg()),
    }));

    std::string output;
    if (result.is_string()) output = result.get<std::string>();
    else if (result.is_object()) {
        output = json_str(result, {"output", "result"});
        if (output.empty()) output = result.dump(2);
    }
    if (!output.empty()) {
        content.push_back(text(""));
        content.push_back(render_truncated_output(output, 18, theme, is_error));
    }
    return vbox(std::move(content));
}

Element BashToolRender(const std::string& command, const std::string& output,
                        int exit_code, bool is_running,
                        const std::string& workdir) {
    Elements content;
    content.push_back(hbox({
        text("$ ") | bold | color(prompt_green()),
        text(command) | bold | color(command_fg()),
    }));
    if (!workdir.empty()) {
        content.push_back(hbox({
            text("in ") | dim | color(muted_fg()),
            text(workdir) | dim,
        }));
    }
    if (!output.empty()) {
        content.push_back(text(""));
        content.push_back(render_truncated_output(output, 28, "orange",
                                                 exit_code != 0));
    }
    const std::string status =
        is_running ? "running" : (exit_code == 0 ? "success" : "failed");
    const Color status_color =
        is_running ? Color::Yellow
                   : (exit_code == 0 ? success_fg() : Color::Red);
    return ToolBlock("$", "Bash", "", vbox(std::move(content)), is_running,
                      status, status_color, 0.0, false, true, false, command);
}

} // namespace tui
} // namespace ai
