#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <cstdio>
#include <qcode/ui/tool_renderers.h>
#include <qcode/ui/message_render.h>
#include <qcode/ui/markdown.h>
#include <qcode/ui/themes.h>
#include <nlohmann/json.hpp>
#include <qcode/core/logger.h>
#include <ftxui/screen/terminal.hpp>

namespace qcode {

using namespace ftxui;

namespace {
class ReflectSimple : public ftxui::Node {
 public:
  ReflectSimple(ftxui::Element child, HitBox& box)
      : ftxui::Node(unpack(std::move(child))), reflected_box_(box) {}

  void ComputeRequirement() final {
    ftxui::Node::ComputeRequirement();
    requirement_ = children_[0]->requirement();
  }

  void SetBox(ftxui::Box box) final {
    reflected_box_.x_min = box.x_min;
    reflected_box_.x_max = box.x_max;
    reflected_box_.y_min = box.y_min;
    reflected_box_.y_max = box.y_max;
    ftxui::Node::SetBox(box);
    children_[0]->SetBox(box);
  }

 private:
  HitBox& reflected_box_;
};

ftxui::Decorator reflect_simple(HitBox& box) {
  return [&](ftxui::Element child) -> ftxui::Element {
    return std::make_shared<ReflectSimple>(std::move(child), box);
  };
}

int plain_wrap_width() {
    int w = 80;
    try {
        w = Terminal::Size().dimx;
    } catch (...) {
    }
    if (w <= 0) w = 80;
    // Role gutter ("┃ " / "  ") + scrollbar + margin.
    return std::max(24, w - 8);
}

Elements wrap_plain_block(const std::string& body_text, int width) {
    Elements lines;
    std::istringstream body(body_text);
    std::string ln;
    bool any = false;
    while (std::getline(body, ln)) {
        any = true;
        if (ln.empty()) {
            lines.push_back(text(""));
            continue;
        }
        if (width <= 0 || static_cast<int>(ln.size()) <= width) {
            lines.push_back(text(ln));
            continue;
        }
        for (size_t i = 0; i < ln.size(); i += static_cast<size_t>(width)) {
            lines.push_back(text(ln.substr(i, static_cast<size_t>(width))));
        }
    }
    if (!any) lines.push_back(text(""));
    return lines;
}

Color prompt_green(const std::string& theme) { return theme_prompt(theme); }
Color command_fg() { return Color::RGB(0xE5, 0xE5, 0xE5); }
Color output_fg() { return Color::RGB(0xB0, 0xB0, 0xB0); }
Color error_fg(const std::string& theme) { return theme_error(theme); }
Color muted_fg(const std::string& theme) { return theme_muted(theme); }
Color success_fg(const std::string& theme) { return theme_success(theme); }
Color panel_bg(const std::string& theme) { return theme_panel_bg(theme); }
Color focus_bg(const std::string& theme) { return theme_focus_bg(theme); }

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
static Element render_tool_pair(const qcode::ToolCallContentPart& call_part,
                                const qcode::ToolResultContentPart& result_part,
                                const std::string& theme,
                                bool collapsed,
                                bool collapsible,
                                bool focused,
                                const ChatState& state);

// ════════════════════════════════════════════════════════════════════════════
//  ToolBlock — OpenCode-style multi-line shell session
// ════════════════════════════════════════════════════════════════════════════
//
//  ▸ # Read todo file                                    ✓ 4ms
//    $ cat path/to/file
//    line 1 of output
//    line 2
//    line 3
//    [+N more · l/→ expand]
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
                   const std::string& shell_command,
                   const std::string& theme,
                   ChatState* state,
                   const std::string& tool_call_id) {
    // ── Title row: ▸ # description · tool          ✓ 4ms ──
    Elements title_row;
    if (collapsible) {
        auto arrow_el = text(collapsed ? "▸" : "▾") | bold;
        if (state && !tool_call_id.empty() && state->tool_arrow_boxes) {
            arrow_el = std::move(arrow_el) | reflect_simple((*state->tool_arrow_boxes)[tool_call_id]);
        }
        title_row.push_back(
            std::move(arrow_el) |
            color(accent_color) |
            (focused ? bold : nothing));
        title_row.push_back(text(" "));
    } else {
        title_row.push_back(text("  "));
    }

    std::string command = shell_command;
    if (command.rfind("$ ", 0) == 0) command = command.substr(2);

    const bool has_distinct_description =
        !description.empty() && description != shell_command &&
        description != command && !title.empty();

    if (has_distinct_description) {
        if (!icon.empty() && icon != "$") {
            title_row.push_back(text(icon + " ") | dim | color(accent_color));
        }
        title_row.push_back(text("# " + truncate_utf8(description, 60)) | bold |
                            color(command_fg()));
        if (!title.empty() && description.find(title) == std::string::npos) {
            title_row.push_back(text(" · " + title) | dim | color(muted_fg(theme)));
        }
    } else if (!command.empty()) {
        title_row.push_back(text("$ ") | bold | color(prompt_green(theme)));
        title_row.push_back(text(truncate_utf8(command, 70)) | bold |
                            color(command_fg()));
    } else {
        if (!icon.empty() && icon != "$") {
            title_row.push_back(text(icon + " ") | dim | color(accent_color));
        }
        title_row.push_back(text(!title.empty() ? title : "tool") | bold |
                            color(command_fg()));
    }

    title_row.push_back(filler());

    const auto timing = format_duration(duration_ms);
    if (!timing.empty()) {
        title_row.push_back(text(timing + " ") | dim | color(muted_fg(theme)));
    }

    if (is_running || status == "running" || status == "calling") {
        title_row.push_back(text("⠋") | color(Color::Yellow) | bold);
    } else if (status == "failed" || status == "error") {
        title_row.push_back(text("✗") | color(error_fg(theme)) | bold);
    } else if (status == "success" || status == "completed" ||
               !status.empty()) {
        title_row.push_back(text("✓") | color(success_fg(theme)) | bold);
    }

    Elements body;
    body.push_back(hbox(std::move(title_row)));

    // When expanded, only render the explicit command line if there was a separate description
    if (!collapsed) {
        if (has_distinct_description && !command.empty()) {
            std::istringstream iss(command);
            std::string line;
            bool first_line = true;
            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                body.push_back(hbox({
                    text(first_line ? "  $ " : "    ") | bold | color(prompt_green(theme)),
                    text(line) | bold | color(command_fg()),
                }));
                first_line = false;
            }
        }

        // Output content
        if (content != emptyElement()) {
            body.push_back(hbox({
                text("  "),
                std::move(content) | flex,
            }));
        }
    }

    auto block = vbox(std::move(body));
    // Soft left rail + panel background (OpenCode BlockTool feel).
    block = hbox({
        text("┃") | color(focused ? accent_color : Color::RGB(0x44, 0x44, 0x55)),
        text(" "),
        std::move(block) | flex,
    });
    block = std::move(block) | bgcolor(focused ? focus_bg(theme) : panel_bg(theme));
    return vbox({std::move(block)});
}

Element BlockTool(const std::string& title, Element content,
                   bool is_running, const std::string& status,
                   Color border_color) {
    return ToolBlock("⚙", title, "", std::move(content),
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

    const Color line_color = is_error ? error_fg(theme) : output_fg();
    const bool unlimited = max_lines <= 0;

    while (std::getline(stream, line)) {
        if (!unlimited && shown >= max_lines) {
            const int remaining = total_lines - max_lines;
            if (remaining > 0) {
                lines.push_back(hbox({
                    text("[+" + std::to_string(remaining) +
                         " more · click arrow to expand") |
                        dim | color(accent(theme)),
                }));
            }
            break;
        }
        lines.push_back(text(line.empty() ? " " : line) | color(line_color));
        ++shown;
    }

    if (lines.empty()) return emptyElement();
    return vbox(std::move(lines));
}

// ════════════════════════════════════════════════════════════════════════════
//  Helpers
// ════════════════════════════════════════════════════════════════════════════
static std::string extract_tool_description(const qcode::ToolCallContentPart& part) {
    const auto& args = part.arguments;
    if (!args.is_object()) return "";
    return json_string(args, {"description", "desc", "prompt", "toolSummary", "Instruction", "instruction"});
}

static std::string extract_shell_command(const qcode::ToolCallContentPart& part) {
    const auto& args = part.arguments;
    if (!args.is_object()) return part.tool_name;

    if (is_bash_tool(part.tool_name)) {
        auto command = json_string(args, {"CommandLine", "command", "cmd", "script"});
        if (!command.empty()) return command;
    }

    if (part.tool_name == "read_file" || part.tool_name == "view_file" ||
        part.tool_name == "write_file" || part.tool_name == "edit_file" ||
        part.tool_name == "replace_file_content") {
        auto path = json_string(args, {"AbsolutePath", "TargetFile", "path", "file", "file_path", "filename"});
        if (!path.empty()) return part.tool_name + " " + path;
    }

    if (part.tool_name == "search" || part.tool_name == "grep" ||
        part.tool_name == "ripgrep" || part.tool_name == "grep_search") {
        auto query = json_string(args, {"Query", "query", "pattern"});
        auto path = json_string(args, {"SearchPath", "path", "directory", "dir"});
        if (!query.empty()) {
            return "grep " + (path.empty() ? "" : path + " ") + "\"" + query + "\"";
        }
    }

    if (part.tool_name == "list_dir" || part.tool_name == "list_files" || part.tool_name == "ls") {
        auto dir = json_string(args, {"DirectoryPath", "path", "dir"});
        if (!dir.empty()) return "ls " + dir;
    }

    if (part.tool_name == "task" || part.tool_name == "dispatch_agent" || part.tool_name == "manage_task") {
        auto action = json_string(args, {"Action", "action"});
        auto desc = json_string(args, {"description", "prompt", "TaskId", "taskId"});
        if (!action.empty() || !desc.empty()) {
            return "task " + (action.empty() ? "" : action + " ") + desc;
        }
    }

    for (auto it = args.begin(); it != args.end(); ++it) {
        if (it.key() == "description" || it.key() == "desc" ||
            it.key() == "toolAction" || it.key() == "toolSummary") continue;
        if (it.value().is_string()) {
            return part.tool_name + " " + it.value().get<std::string>();
        }
    }
    return part.tool_name;
}

static std::string extract_result_output(const qcode::ToolResultContentPart& part) {
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



static Element render_shell_output(const qcode::ToolCallContentPart& call_part,
                                   const qcode::ToolResultContentPart& result_part,
                                   const std::string& theme,
                                   bool collapsed) {
    if (collapsed) return emptyElement();

    Elements body;
    if (call_part.arguments.is_object()) {
        const auto workdir =
            json_string(call_part.arguments, {"workdir", "cwd", "Cwd"});
        if (!workdir.empty()) {
            body.push_back(hbox({
                text("in ") | dim | color(muted_fg(theme)),
                text(workdir) | dim | color(Color::RGB(0x7A, 0xA2, 0xF7)),
            }));
        }
    }

    const auto output = extract_result_output(result_part);
    if (!output.empty()) {
        body.push_back(render_truncated_output(
            output, 0, theme,
            result_part.is_error));
    }

    if (is_bash_tool(call_part.tool_name) &&
        result_part.result.is_object() &&
        result_part.result.contains("metadata") &&
        result_part.result["metadata"].is_object() &&
        result_part.result["metadata"].contains("exit")) {
        const int exit_code = result_part.result["metadata"]["exit"].get<int>();
        const auto exit_color = exit_code == 0 ? success_fg(theme) : error_fg(theme);
        body.push_back(hbox({
            text(exit_code == 0 ? "✓" : "✗") | color(exit_color) | bold,
            text(" exit " + std::to_string(exit_code)) | color(exit_color),
        }));
    }

    if (body.empty()) return emptyElement();
    return vbox(std::move(body));
}

static Element render_tool_call(const qcode::ToolCallContentPart& part,
                                 const std::string& theme,
                                 const ChatState& state) {
    const auto command = extract_shell_command(part);
    const auto desc = extract_tool_description(part);
    
    Elements body;
    body.push_back(text("running…") | dim | color(Color::Yellow));

    return ToolBlock(tool_icon(part.tool_name),
                      tool_display_name(part.tool_name), desc,
                      vbox(std::move(body)), true,
                      "calling", accent(theme), 0.0, false, false, false,
                      command, theme, const_cast<ChatState*>(&state), part.id);
}

static Element render_tool_result(const qcode::ToolResultContentPart& part,
                                   const std::string& theme,
                                   const ChatState& state) {
    std::string tool_name;
    if (part.result.is_object()) {
        tool_name = json_string(part.result, {"tool_name", "title"});
    }
    const std::string status = part.is_error ? "failed" : "success";
    const auto output = extract_result_output(part);
    return ToolBlock(tool_icon(tool_name), tool_display_name(tool_name), "",
                      output.empty()
                          ? emptyElement()
                          : render_truncated_output(output, 18, theme, part.is_error),
                      false, status,
                      part.is_error ? theme_error(theme) : theme_success(theme),
                      part.duration_ms, false, true, false,
                      tool_name.empty() ? "tool" : tool_name, theme, const_cast<ChatState*>(&state), part.tool_call_id);
}

// Opencode-style reasoning header: warning-coloured "+/- Thought" toggle
// (collapsed by default), live "Thinking" spinner while the model works.
static Element render_reasoning(const qcode::ReasoningContentPart& rp,
                                 const std::string& theme,
                                 bool expanded = true,
                                 bool busy = false) {
    if (rp.text.empty()) return emptyElement();

    auto thought_line = [&](const char* marker) {
        return hbox({
            text("   ") ,
            text(marker) | bold | color(theme_warning(theme)),
            text(" Thought") | color(theme_warning(theme)),
        });
    };

    // Collapsed summary — mirrors opencode hide-mode "+ Thought" (click the
    // header to expand; markers communicate the toggle).
    if (!expanded) {
        Element line;
        if (busy) {
            line = hbox({
                text("   ◐ ") | color(theme_warning(theme)),
                text("Thinking") | color(theme_warning(theme)),
            });
        } else {
            line = hbox({
                thought_line("+ "),
                text(" · click to expand") | dim | color(Color::GrayDark),
            });
        }
        return line;
    }

    Elements md = render_markdown(rp.text, theme);
    Elements indented;
    for (auto& el : md) {
        indented.push_back(
            hbox({text("     "), std::move(el) | flex}) | dim);
    }

    return vbox({
        hbox({
            thought_line("- "),
            text(" · click to collapse") | dim | color(Color::GrayDark),
        }),
        vbox(std::move(indented)),
    });
}

Element render_message(const qcode::Message& msg, const ChatState& state,
                        const std::vector<ProviderInfo>& providers_list,
                        int selected_provider, int selected_model,
                        const std::string& theme,
                        const qcode::Message* adjacent_tool_results) {
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
        (void)model_label;  // used by the completion tail below
    } else if (msg.role == kMessageRoleUser && !has_tool_results) {
        // Opencode-style user block: left border in the agent colour with a
        // subtle panel background — no label chrome.
        Elements user_lines;
        const int wrap_w = plain_wrap_width();
        for (const auto& part : msg.content) {
            const auto* tp = std::get_if<qcode::TextContentPart>(&part);
            if (!tp || tp->text.empty()) continue;
            for (auto& chunk : wrap_plain_block(tp->text, wrap_w)) {
                user_lines.push_back(
                    hbox({text("┃ ") | color(user_green()), std::move(chunk)}) |
                    bgcolor(theme_panel_bg(theme)));
            }
        }
        parts.push_back(vbox(std::move(user_lines)));
    } else if (msg.role == kMessageRoleSystem) {
        parts.push_back(hbox({
            text("ℹ ") | dim | color(theme_muted(theme)),
            text("System") | dim | color(theme_muted(theme)),
        }));
    }

    std::unordered_map<std::string, const qcode::ToolCallContentPart*> tool_calls;
    std::unordered_map<std::string, const qcode::ToolResultContentPart*> tool_results;

    for (const auto& part : msg.content) {
        if (const auto* tool_part = std::get_if<qcode::ToolCallContentPart>(&part)) {
            tool_calls[tool_part->id] = tool_part;
        } else if (const auto* result_part =
                       std::get_if<qcode::ToolResultContentPart>(&part)) {
            tool_results[result_part->tool_call_id] = result_part;
        }
    }
    if (adjacent_tool_results != nullptr) {
        for (const auto& part : adjacent_tool_results->content) {
            if (const auto* result_part =
                    std::get_if<qcode::ToolResultContentPart>(&part)) {
                tool_results[result_part->tool_call_id] = result_part;
            }
        }
    }

    std::unordered_set<std::string> rendered_tool_results;

    // Thinking traces toggle by CLICKING the "+/- Thought" header (mirrors
    // opencode's onMouseUp toggle); per-message state, default collapsed.
    const bool turn_busy =
        state.is_generating && state.is_generating->load();
    const unsigned long thinking_key =
        reinterpret_cast<unsigned long>(&msg);
    bool thinking_expanded = false;
    if (state.thinking_expand_state) {
        auto it = state.thinking_expand_state->find(thinking_key);
        thinking_expanded =
            it != state.thinking_expand_state->end() && it->second;
    }

    for (const auto& part : msg.content) {
        if (const auto* reasoning_part =
                       std::get_if<qcode::ReasoningContentPart>(&part)) {
            if (*state.show_thinking) {
                Element block = render_reasoning(*reasoning_part, theme,
                                                 thinking_expanded, turn_busy);
                // Make the header line clickable for the NEXT frame (same
                // hit-testing approach as tool-call arrows).
                if (state.thinking_header_boxes) {
                    HitBox& box = (*state.thinking_header_boxes)[thinking_key];
                    block = block | reflect_simple(box);
                }
                parts.push_back(std::move(block));
            }
        } else if (const auto* tool_part =
                       std::get_if<qcode::ToolCallContentPart>(&part)) {
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
                    focused, state));
                rendered_tool_results.insert(tool_part->id);
            } else {
                parts.push_back(render_tool_call(*tool_part, theme, state));
            }
        } else if (const auto* result_part =
                       std::get_if<qcode::ToolResultContentPart>(&part)) {
            if (rendered_tool_results.count(result_part->tool_call_id)) {
                continue;
            }
            parts.push_back(render_tool_result(*result_part, theme, state));
            rendered_tool_results.insert(result_part->tool_call_id);
        } else if (const auto* text_part = std::get_if<qcode::TextContentPart>(&part)) {
            if (text_part->text.empty()) continue;
            if (msg.role == kMessageRoleUser && !has_tool_results) {
                continue;
            }
            if (msg.role == kMessageRoleSystem) {
                Elements sys_lines;
                for (auto& chunk :
                     wrap_plain_block(text_part->text, plain_wrap_width())) {
                    sys_lines.push_back(
                        hbox({text("  "), std::move(chunk) | dim}));
                }
                parts.push_back(vbox(std::move(sys_lines)));
            } else {
                Elements md = render_markdown(text_part->text, theme);
                Elements indented;
                for (auto& el : md) {
                    indented.push_back(hbox({text("   "), std::move(el) | flex}));
                }
                parts.push_back(vbox(std::move(indented)));
            }
        }
    }

    // Opencode-style completion tail "▣ Build · Model" — only on the FINAL
    // assistant message of the conversation, never per tool-loop step (the
    // header already carries model/mode info).
    bool is_last_assistant = false;
    if (state.messages_history) {
        for (auto it = state.messages_history->rbegin();
             it != state.messages_history->rend(); ++it) {
            if (it->role == kMessageRoleAssistant) {
                is_last_assistant = (&*it == &msg);
                break;
            }
        }
    }
    if (msg.role == kMessageRoleAssistant && is_last_assistant && !turn_busy &&
        (!parts.empty() || has_text)) {
        std::string model_label;
        if (selected_provider >= 0 &&
            selected_provider < static_cast<int>(providers_list.size()) &&
            selected_model >= 0 &&
            selected_model < static_cast<int>(
                providers_list[selected_provider].models.size())) {
            model_label =
                providers_list[selected_provider].models[selected_model].name;
        }
        const std::string mode =
            state.agent_mode && *state.agent_mode == "plan" ? "Plan" : "Build";
        auto tail = hbox({
            text("   ▣ ") | color(accent(theme)),
            text(mode) | color(accent(theme)),
        });
        if (!model_label.empty()) {
            tail = hbox({
                std::move(tail),
                text(" · " + model_label) | dim | color(theme_muted(theme)),
            });
        }
        parts.push_back(text(""));
        parts.push_back(std::move(tail));
    }

    return vbox(std::move(parts));
}

static Element render_tool_pair(const qcode::ToolCallContentPart& call_part,
                                 const qcode::ToolResultContentPart& result_part,
                                 const std::string& theme,
                                 bool collapsed,
                                 bool collapsible,
                                 bool focused,
                                 const ChatState& state) {
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
        result_part.is_error ? theme_error(theme) : theme_success(theme);

    return ToolBlock(
        tool_icon(call_part.tool_name), tool_display_name(call_part.tool_name),
        desc, render_shell_output(call_part, result_part, theme, collapsed),
        false, status, status_color, result_part.duration_ms, collapsed,
        collapsible, focused, command, theme, const_cast<ChatState*>(&state), call_part.id);
}

}  // namespace qcode
