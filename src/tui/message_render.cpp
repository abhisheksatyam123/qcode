#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <cstdio>
#include <ai/tui/tool_renderers.h>
#include <ai/tui/message_render.h>
#include <ai/tui/views.h>
#include <nlohmann/json.hpp>
#include <ai/logger.h>

namespace ai {
namespace tui {

using namespace ftxui;

namespace {

Color prompt_green() { return Color::RGB(0x4E, 0xC9, 0xB0); }
Color command_fg() { return Color::RGB(0xE5, 0xE5, 0xE5); }
Color output_fg() { return Color::RGB(0xB0, 0xB0, 0xB0); }
Color error_fg() { return Color::RGB(0xF4, 0x87, 0x71); }
Color muted_fg() { return Color::RGB(0x6A, 0x6A, 0x6A); }
Color success_fg() { return Color::RGB(0x7F, 0xDB, 0x8C); }

std::string format_duration(double duration_ms) {
    if (duration_ms <= 0) return {};
    if (duration_ms < 1000) {
        return std::to_string(static_cast<int>(duration_ms)) + "ms";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1fs", duration_ms / 1000.0);
    return buf;
}

std::string truncate_utf8(std::string text, size_t max_chars) {
    if (text.size() <= max_chars) return text;
    if (max_chars <= 3) return text.substr(0, max_chars);
    return text.substr(0, max_chars - 3) + "...";
}

std::string json_string(const nlohmann::json& value,
                        std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (value.contains(key) && value[key].is_string()) {
            return value[key].get<std::string>();
        }
    }
    return {};
}

bool is_bash_tool(const std::string& tool_name) {
    return tool_name == "bash" || tool_name == "shell" ||
           tool_name == "run_command";
}

}  // namespace

// Forward declarations
static Element render_tool_pair(const ai::ToolCallContentPart& call_part,
                                const ai::ToolResultContentPart& result_part,
                                const std::string& theme,
                                bool collapsed,
                                bool collapsible,
                                bool focused = false);

// ════════════════════════════════════════════════════════════════════════════
//  ToolBlock — shell-session look with always-visible $ command
// ════════════════════════════════════════════════════════════════════════════
//
//  Collapsed:
//    ▸ $ find . -maxdepth 3 -not -path '*/.*'               ✓ 142ms
//
//  Expanded:
//    ▾ $ find . -maxdepth 3 -not -path '*/.*'               ✓ 142ms
//      ./src
//      ./include
//      …
//
Element ToolBlock(const std::string& icon,
                   const std::string& title,
                   const std::string& description,
                   Element content,
                   bool is_running,
                   const std::string& status,
                   Color accent_color,
                   double duration_ms,
                   bool collapsed,
                   bool collapsible,
                   bool focused,
                   const std::string& shell_command) {
    Elements header;

    if (collapsible) {
        header.push_back(
            text(collapsed ? "▸ " : "▾ ") |
            color(focused ? accent_color : muted_fg()) |
            (focused ? bold : nothing));
    } else {
        header.push_back(text("  "));
    }

    // Prefer an explicit shell command; otherwise fall back to title/desc.
    std::string command = shell_command;
    if (command.empty()) {
        if (!description.empty()) {
            command = description;
        } else if (!title.empty()) {
            command = title;
        } else {
            command = "tool";
        }
    }
    if (command.rfind("$ ", 0) == 0) {
        command = command.substr(2);
    }

    header.push_back(text("$ ") | bold | color(prompt_green()));
    header.push_back(
        text(truncate_utf8(std::move(command), 94)) |
        bold | color(command_fg()));

    // Bash sessions are just `$ command`. Other tools may show a dim label.
    const bool bash_like =
        title == "Bash" || title == "Shell" || is_bash_tool(title);
    if (!bash_like && !title.empty() &&
        shell_command.find(title) == std::string::npos) {
        header.push_back(text("  ") | dim);
        if (!icon.empty()) {
            header.push_back(text(icon + " ") | dim | color(accent_color));
        }
        header.push_back(text(title) | dim | color(muted_fg()));
    } else if (!bash_like && !description.empty() &&
               shell_command.find(description) == std::string::npos &&
               title != description) {
        header.push_back(text("  · ") | dim | color(muted_fg()));
        header.push_back(
            text(truncate_utf8(description, 36)) | dim | color(muted_fg()));
    }

    header.push_back(filler());

    const auto timing = format_duration(duration_ms);
    if (!timing.empty()) {
        header.push_back(text(timing + " ") | dim | color(muted_fg()));
    }

    if (is_running || status == "running" || status == "calling") {
        header.push_back(text("⠋") | color(Color::Yellow) | bold);
    } else if (status == "failed" || status == "error") {
        header.push_back(text("✗") | color(Color::Red) | bold);
    } else if (status == "success" || status == "completed" || !status.empty()) {
        header.push_back(text("✓") | color(success_fg()) | bold);
    }

    Elements block;
    auto header_row = hbox(std::move(header));
    if (focused) {
        header_row = header_row | bgcolor(Color::RGB(0x2A, 0x2A, 0x2A));
    }
    block.push_back(std::move(header_row));

    // Collapsed and expanded both show body content. Collapse only limits how
    // many output lines the caller includes (3-line preview vs full dump).
    block.push_back(hbox({
        text("  ") | color(prompt_green()),
        std::move(content) | flex,
    }));

    block.push_back(text(""));
    return vbox(std::move(block));
}

Element BlockTool(const std::string& title, Element content,
                   bool is_running, const std::string& status,
                   Color border_color) {
    return ToolBlock("🔧", title, "", std::move(content),
                      is_running, status, border_color, 0.0,
                      false, true, false, title);
}

// ════════════════════════════════════════════════════════════════════════════
//  Colored truncated output (shell stdout look)
// ════════════════════════════════════════════════════════════════════════════
Element render_truncated_output(const std::string& output,
                                 int max_lines,
                                 const std::string& theme,
                                 bool is_error) {
    Elements lines;
    std::istringstream stream(output);
    std::string line;
    int shown = 0;
    int total_lines = 0;
    {
        std::istringstream counter(output);
        std::string tmp;
        while (std::getline(counter, tmp)) ++total_lines;
        if (!output.empty() && output.back() != '\n' && total_lines == 0) {
            total_lines = 1;
        }
    }

    const Color line_color = is_error ? error_fg() : output_fg();
    const bool unlimited = max_lines <= 0;

    while (std::getline(stream, line)) {
        if (!unlimited && shown >= max_lines) {
            const int remaining = total_lines - max_lines;
            if (remaining > 0) {
                lines.push_back(hbox({
                    text("[+" + std::to_string(remaining) +
                         " more · l/→ expand  h/← collapse]") |
                        dim | color(accent(theme)),
                }));
            }
            break;
        }
        // Preserve empty lines so shell output spacing stays intact.
        lines.push_back(text(line.empty() ? " " : line) | color(line_color));
        ++shown;
    }

    if (lines.empty()) {
        lines.push_back(text("(no output)") | dim | color(muted_fg()));
    }
    return vbox(std::move(lines));
}

// ════════════════════════════════════════════════════════════════════════════
//  Helpers
// ════════════════════════════════════════════════════════════════════════════
static std::string extract_tool_description(const ai::ToolCallContentPart& part) {
    const auto& args = part.arguments;
    if (!args.is_object()) return "";
    return truncate_utf8(json_string(args, {"description", "desc", "prompt"}), 60);
}

static std::string extract_shell_command(const ai::ToolCallContentPart& part) {
    const auto& args = part.arguments;
    if (!args.is_object()) return part.tool_name;

    if (is_bash_tool(part.tool_name)) {
        auto command = json_string(args, {"command", "cmd", "script"});
        if (!command.empty()) return command;
    }

    if (part.tool_name == "read_file" || part.tool_name == "view_file" ||
        part.tool_name == "write_file" || part.tool_name == "edit_file") {
        auto path = json_string(args, {"path", "file", "file_path", "filename"});
        if (!path.empty()) return part.tool_name + " " + path;
    }

    if (part.tool_name == "search" || part.tool_name == "grep" ||
        part.tool_name == "ripgrep") {
        auto query = json_string(args, {"query", "pattern"});
        if (!query.empty()) return part.tool_name + " \"" + query + "\"";
    }

    if (part.tool_name == "task" || part.tool_name == "dispatch_agent") {
        auto desc = json_string(args, {"description", "prompt"});
        if (!desc.empty()) return "task " + truncate_utf8(std::move(desc), 72);
    }

    for (auto it = args.begin(); it != args.end(); ++it) {
        if (it.key() == "description" || it.key() == "desc") continue;
        if (it.value().is_string()) {
            return part.tool_name + " " +
                   truncate_utf8(it.value().get<std::string>(), 80);
        }
    }
    return part.tool_name;
}

static std::string extract_result_output(const ai::ToolResultContentPart& part) {
    if (part.result.is_string()) return part.result.get<std::string>();
    if (!part.result.is_object()) return part.result.dump(2);

    auto output = json_string(part.result, {"output", "content", "result",
                                            "summary", "matches", "error"});
    if (!output.empty()) return output;
    if (part.result.contains("error")) {
        return part.result["error"].dump(2);
    }
    return part.result.dump(2);
}

static Element render_shell_output(const ai::ToolCallContentPart& call_part,
                                   const ai::ToolResultContentPart& result_part,
                                   const std::string& theme,
                                   bool collapsed) {
    Elements body;
    constexpr int kCollapsedPreviewLines = 3;

    // Soft workdir hint for bash — only in the full expand view so the
    // collapsed preview stays to exactly three output lines.
    if (!collapsed && is_bash_tool(call_part.tool_name) &&
        call_part.arguments.is_object()) {
        const auto workdir =
            json_string(call_part.arguments, {"workdir", "cwd"});
        if (!workdir.empty()) {
            body.push_back(hbox({
                text("in ") | dim | color(muted_fg()),
                text(workdir) | dim | color(Color::RGB(0x7A, 0xA2, 0xF7)),
            }));
        }
    }

    const auto output = extract_result_output(result_part);
    body.push_back(render_truncated_output(
        output, collapsed ? kCollapsedPreviewLines : 0, theme,
        result_part.is_error));

    if (!collapsed && is_bash_tool(call_part.tool_name) &&
        result_part.result.is_object() &&
        result_part.result.contains("metadata") &&
        result_part.result["metadata"].is_object() &&
        result_part.result["metadata"].contains("exit")) {
        const int exit_code = result_part.result["metadata"]["exit"].get<int>();
        const auto exit_color = exit_code == 0 ? success_fg() : Color::Red;
        body.push_back(text(""));
        body.push_back(hbox({
            text(exit_code == 0 ? "✓" : "✗") | color(exit_color) | bold,
            text(" exit " + std::to_string(exit_code)) | color(exit_color),
        }));
    }

    return vbox(std::move(body));
}

// ════════════════════════════════════════════════════════════════════════════
//  In-progress tool call
// ════════════════════════════════════════════════════════════════════════════
static Element render_tool_call(const ai::ToolCallContentPart& part,
                                 const std::string& theme) {
    const auto command = extract_shell_command(part);
    const auto desc = extract_tool_description(part);
    return ToolBlock(tool_icon(part.tool_name),
                      tool_display_name(part.tool_name), desc,
                      text("running…") | dim | color(Color::Yellow), true,
                      "calling", accent(theme), 0.0, false, false, false,
                      command);
}

// ════════════════════════════════════════════════════════════════════════════
//  Orphan tool result
// ════════════════════════════════════════════════════════════════════════════
static Element render_tool_result(const ai::ToolResultContentPart& part,
                                   const std::string& theme) {
    std::string tool_name;
    if (part.result.is_object()) {
        tool_name = json_string(part.result, {"tool_name"});
    }
    const std::string status = part.is_error ? "failed" : "success";
    const auto output = extract_result_output(part);
    return ToolBlock(tool_icon(tool_name), tool_display_name(tool_name), "",
                      render_truncated_output(output, 18, theme, part.is_error),
                      false, status,
                      part.is_error ? Color::Red : success_fg(),
                      part.duration_ms, false, true, false,
                      tool_name.empty() ? "tool" : tool_name);
}

// ════════════════════════════════════════════════════════════════════════════
//  Reasoning
// ════════════════════════════════════════════════════════════════════════════
static Element render_reasoning(const ai::ReasoningContentPart& rp,
                                 const std::string& theme) {
    if (rp.text.empty()) return Element();
    Elements md = render_markdown(rp.text);
    Elements indented;
    for (auto& el : md) {
        indented.push_back(hbox({text("  "), std::move(el)}));
    }
    return vbox({
        hbox({
            text("💭 ") | color(accent(theme)),
            text("Thinking") | dim | color(accent(theme)),
        }),
        hbox({
            text("│") | color(muted_fg()),
            text(" "),
            vbox(std::move(indented)) | flex,
        }),
        text(""),
    });
}

// ════════════════════════════════════════════════════════════════════════════
//  Message renderer
// ════════════════════════════════════════════════════════════════════════════
Element render_message(const ai::Message& msg, const ChatState& state,
                        const std::vector<ProviderInfo>& providers_list,
                        int selected_provider, int selected_model,
                        const std::string& theme,
                        const ai::Message* adjacent_tool_results) {
    Elements parts;

    const bool has_text = msg.has_text();
    const bool has_tool_results = msg.has_tool_results();

    if (msg.role == kMessageRoleAssistant && has_text) {
        std::string model_label;
        if (selected_provider >= 0 &&
            selected_provider < static_cast<int>(providers_list.size()) &&
            selected_model >= 0 &&
            selected_model < static_cast<int>(
                providers_list[selected_provider].models.size())) {
            model_label =
                providers_list[selected_provider].models[selected_model].name;
        }

        Elements header;
        header.push_back(text("❯ ") | bold | color(accent(theme)));
        header.push_back(text("Assistant") | bold | color(accent(theme)));
        if (!model_label.empty()) {
            header.push_back(
                text(" · " + model_label) | dim | color(Color::GrayDark));
        }
        parts.push_back(hbox(std::move(header)));
    } else if (msg.role == kMessageRoleUser && !has_tool_results) {
        parts.push_back(hbox({
            text("❯ ") | bold | color(user_green()),
            text("You") | bold | color(user_green()),
        }));
    } else if (msg.role == kMessageRoleSystem) {
        parts.push_back(hbox({
            text("ℹ ") | dim | color(Color::GrayDark),
            text("System") | dim | color(Color::GrayDark),
        }));
    }

    std::unordered_map<std::string, const ai::ToolCallContentPart*> tool_calls;
    std::unordered_map<std::string, const ai::ToolResultContentPart*> tool_results;

    for (const auto& part : msg.content) {
        if (const auto* tool_part = std::get_if<ai::ToolCallContentPart>(&part)) {
            tool_calls[tool_part->id] = tool_part;
        } else if (const auto* result_part =
                       std::get_if<ai::ToolResultContentPart>(&part)) {
            tool_results[result_part->tool_call_id] = result_part;
        }
    }
    if (adjacent_tool_results != nullptr) {
        for (const auto& part : adjacent_tool_results->content) {
            if (const auto* result_part =
                    std::get_if<ai::ToolResultContentPart>(&part)) {
                tool_results[result_part->tool_call_id] = result_part;
            }
        }
    }

    std::unordered_set<std::string> rendered_tool_results;

    for (const auto& part : msg.content) {
        if (const auto* text_part = std::get_if<ai::TextContentPart>(&part)) {
            if (text_part->text.empty()) continue;
            if (msg.role == kMessageRoleSystem) {
                parts.push_back(
                    hbox({text("  "), text(text_part->text) | dim}));
            } else {
                Elements md = render_markdown(text_part->text);
                Elements indented;
                for (auto& el : md) {
                    indented.push_back(hbox({text("  "), std::move(el)}));
                }
                parts.push_back(vbox(std::move(indented)));
            }
        } else if (const auto* tool_part =
                       std::get_if<ai::ToolCallContentPart>(&part)) {
            auto result_it = tool_results.find(tool_part->id);
            if (result_it != tool_results.end()) {
                bool collapsed = true;
                if (state.tool_collapse_state &&
                    state.tool_collapse_state->count(tool_part->id)) {
                    collapsed = (*state.tool_collapse_state)[tool_part->id];
                }

                bool focused = false;
                if (state.tool_block_order) {
                    const int idx =
                        static_cast<int>(state.tool_block_order->size());
                    state.tool_block_order->push_back(tool_part->id);
                    if (state.focused_tool_index &&
                        *state.focused_tool_index == idx) {
                        focused = true;
                    }
                }

                parts.push_back(render_tool_pair(
                    *tool_part, *result_it->second, theme, collapsed, true,
                    focused));
                rendered_tool_results.insert(tool_part->id);
            } else {
                parts.push_back(render_tool_call(*tool_part, theme));
            }
        } else if (const auto* result_part =
                       std::get_if<ai::ToolResultContentPart>(&part)) {
            if (rendered_tool_results.count(result_part->tool_call_id)) {
                continue;
            }
            parts.push_back(render_tool_result(*result_part, theme));
            rendered_tool_results.insert(result_part->tool_call_id);
        } else if (const auto* reasoning_part =
                       std::get_if<ai::ReasoningContentPart>(&part)) {
            if (*state.show_thinking) {
                parts.push_back(render_reasoning(*reasoning_part, theme));
            }
        }
    }

    return vbox(std::move(parts));
}

// ════════════════════════════════════════════════════════════════════════════
//  Paired tool call + result as expandable shell session
// ════════════════════════════════════════════════════════════════════════════
static Element render_tool_pair(const ai::ToolCallContentPart& call_part,
                                 const ai::ToolResultContentPart& result_part,
                                 const std::string& theme,
                                 bool collapsed,
                                 bool collapsible,
                                 bool focused) {
    const auto command = extract_shell_command(call_part);
    auto desc = extract_tool_description(call_part);
    if (result_part.result.is_object()) {
        const auto title = json_string(result_part.result, {"title"});
        if (!title.empty()) desc = title;
        else if (result_part.result.contains("metadata") &&
                 result_part.result["metadata"].is_object()) {
            const auto meta_desc = json_string(
                result_part.result["metadata"], {"description"});
            if (!meta_desc.empty()) desc = meta_desc;
        }
    }

    const std::string status = result_part.is_error ? "failed" : "success";
    const Color status_color =
        result_part.is_error ? Color::Red : success_fg();

    return ToolBlock(
        tool_icon(call_part.tool_name), tool_display_name(call_part.tool_name),
        desc, render_shell_output(call_part, result_part, theme, collapsed),
        false, status, status_color, result_part.duration_ms, collapsed,
        collapsible, focused, command);
}

} // namespace tui
} // namespace ai
