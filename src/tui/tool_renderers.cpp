#include <ai/tui/tool_renderers.h>
#include <ai/tui/message_render.h>
#include <ai/tui/views.h>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <algorithm>

namespace ai {
namespace tui {

using namespace ftxui;

// ── Tool icon mapping ──
std::string tool_icon(const std::string& tool_name) {
    if (tool_name == "bash" || tool_name == "shell" || tool_name == "run_command")
        return "⚡";
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
    return "🔧";
}

// ── Tool display name ──
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

// ── Helper: extract string from JSON with fallback keys ──
static std::string json_str(const nlohmann::json& j,
                              std::initializer_list<const char*> keys,
                              const std::string& fallback = "") {
    for (const auto& key : keys) {
        if (j.contains(key) && j[key].is_string()) {
            return j[key].get<std::string>();
        }
    }
    return fallback;
}

// ── Bash result renderer ──
Element RenderBashResult(const nlohmann::json& args,
                          const nlohmann::json& result,
                          bool is_error, double duration_ms,
                          const std::string& theme) {
    Elements content;

    // Extract command from args
    std::string command = json_str(args, {"command", "cmd", "script"});
    std::string description = json_str(args, {"description"});
    std::string workdir = json_str(args, {"workdir"});

    // Command line
    if (!command.empty()) {
        Elements cmd_line;
        cmd_line.push_back(text("$ ") | color(Color::RGB(120, 120, 255)));
        // Truncate very long commands
        std::string display_cmd = command;
        if (display_cmd.size() > 200) {
            display_cmd = display_cmd.substr(0, 197) + "...";
        }
        cmd_line.push_back(text(display_cmd) | bold);
        content.push_back(hbox(std::move(cmd_line)));
    }

    // Working directory
    if (!workdir.empty()) {
        content.push_back(hbox({
            text("  in ") | dim,
            text(workdir) | dim | color(Color::RGB(150, 150, 200)),
        }));
    }

    // Extract output from result
    std::string output;
    int exit_code = 0;
    bool has_exit = false;

    if (result.is_object()) {
        output = json_str(result, {"output"});
        if (result.contains("metadata") && result["metadata"].is_object()) {
            auto& meta = result["metadata"];
            if (meta.contains("exit")) {
                exit_code = meta["exit"].get<int>();
                has_exit = true;
            }
        }
    } else if (result.is_string()) {
        output = result.get<std::string>();
    }

    // Exit code
    if (has_exit) {
        Color exit_color = (exit_code == 0) ? Color::Green : Color::Red;
        content.push_back(text(""));  // spacing
        content.push_back(hbox({
            text("  ") | dim,
            text(exit_code == 0 ? "✓" : "✗") | color(exit_color) | bold,
            text(" exit " + std::to_string(exit_code)) | color(exit_color),
        }));
    }

    // Output (truncated)
    if (!output.empty()) {
        content.push_back(text(""));  // spacing
        content.push_back(render_truncated_output(output, 25, theme));
    }

    return vbox(std::move(content));
}

// ── Task result renderer ──
Element RenderTaskResult(const nlohmann::json& args,
                          const nlohmann::json& result,
                          bool is_error, double duration_ms,
                          const std::string& theme) {
    Elements content;

    std::string desc = json_str(args, {"description", "prompt", "task"});
    if (!desc.empty()) {
        content.push_back(hbox({
            text("  ") | dim,
            text(desc) | dim,
        }));
    }

    // Show result summary
    std::string output;
    if (result.is_string()) {
        output = result.get<std::string>();
    } else if (result.is_object()) {
        output = json_str(result, {"output", "result", "summary"});
    }

    if (!output.empty()) {
        content.push_back(text(""));
        content.push_back(render_truncated_output(output, 15, theme));
    }

    return vbox(std::move(content));
}

// ── File result renderer ──
Element RenderFileResult(const std::string& tool_name,
                          const nlohmann::json& args,
                          const nlohmann::json& result,
                          bool is_error, double duration_ms,
                          const std::string& theme) {
    Elements content;

    std::string path = json_str(args, {"path", "file", "file_path", "filename"});

    if (!path.empty()) {
        content.push_back(hbox({
            text("  ") | dim,
            text(path) | color(Color::RGB(150, 200, 255)),
        }));
    }

    // Show line count or content preview for read
    if (result.is_object()) {
        std::string output = json_str(result, {"output", "content"});
        if (!output.empty()) {
            // Count lines
            int line_count = 1;
            for (char c : output) {
                if (c == '\n') line_count++;
            }
            content.push_back(hbox({
                text("  ") | dim,
                text(std::to_string(line_count) + " lines") | dim,
            }));
            content.push_back(text(""));
            content.push_back(render_truncated_output(output, 10, theme));
        }
    } else if (result.is_string()) {
        std::string r = result.get<std::string>();
        if (!r.empty()) {
            content.push_back(text(""));
            content.push_back(render_truncated_output(r, 10, theme));
        }
    }

    return vbox(std::move(content));
}

// ── Search result renderer ──
Element RenderSearchResult(const nlohmann::json& args,
                            const nlohmann::json& result,
                            bool is_error, double duration_ms,
                            const std::string& theme) {
    Elements content;

    std::string query = json_str(args, {"query", "pattern", "search"});
    std::string path = json_str(args, {"path", "directory", "dir"});

    if (!query.empty()) {
        content.push_back(hbox({
            text("  ") | dim,
            text("\"" + query + "\"") | color(Color::RGB(255, 200, 100)),
        }));
    }
    if (!path.empty()) {
        content.push_back(hbox({
            text("  in ") | dim,
            text(path) | dim | color(Color::RGB(150, 150, 200)),
        }));
    }

    // Show results
    std::string output;
    if (result.is_string()) {
        output = result.get<std::string>();
    } else if (result.is_object()) {
        output = json_str(result, {"output", "matches", "results"});
    }

    if (!output.empty()) {
        content.push_back(text(""));
        content.push_back(render_truncated_output(output, 20, theme));
    }

    return vbox(std::move(content));
}

// ── Generic result renderer (fallback) ──
Element RenderGenericResult(const std::string& tool_name,
                             const nlohmann::json& args,
                             const nlohmann::json& result,
                             bool is_error, double duration_ms,
                             const std::string& theme) {
    Elements content;

    // Show first string argument as description
    if (args.is_object()) {
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (it.value().is_string()) {
                std::string val = it.value().get<std::string>();
                if (val.size() > 120) val = val.substr(0, 117) + "...";
                content.push_back(hbox({
                    text("  " + it.key() + ": ") | dim,
                    text(val) | dim,
                }));
                break;
            }
        }
    }

    // Show result
    std::string output;
    if (result.is_string()) {
        output = result.get<std::string>();
    } else if (result.is_object()) {
        output = json_str(result, {"output", "result"});
        if (output.empty()) {
            output = result.dump(2);
        }
    }

    if (!output.empty()) {
        content.push_back(text(""));
        content.push_back(render_truncated_output(output, 15, theme));
    }

    return vbox(std::move(content));
}

// ── Legacy API ──
Element BashToolRender(const std::string& command, const std::string& output,
                        int exit_code, bool is_running,
                        const std::string& workdir) {
    Elements content;
    content.push_back(hbox({
        text("$ ") | color(Color::RGB(100, 100, 255)),
        text(command) | bold,
    }));
    if (!output.empty()) {
        content.push_back(text(output) | dim);
    }
    std::string status =
        is_running ? "running" : (exit_code == 0 ? "success" : "failed");
    Color status_color =
        is_running ? Color::Yellow
                   : (exit_code == 0 ? Color::Green : Color::Red);
    return BlockTool("Bash", vbox(std::move(content)), is_running, status,
                      status_color);
}

} // namespace tui
} // namespace ai
