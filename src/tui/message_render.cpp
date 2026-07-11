#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <ai/tui/tool_renderers.h>
#include <ai/tui/message_render.h>
#include <ai/tui/views.h>
#include <nlohmann/json.hpp>
#include <ai/logger.h>

namespace ai {
namespace tui {

using namespace ftxui;

// Forward declarations
static Element render_tool_pair(const ai::ToolCallContentPart& call_part,
                                const ai::ToolResultContentPart& result_part,
                                const std::string& theme,
                                bool collapsed,
                                bool collapsible,
                                bool focused = false);

// ════════════════════════════════════════════════════════════════════════════
//  ToolBlock: OpenCode-style tool rendering block with collapse/expand
// ════════════════════════════════════════════════════════════════════════════
//
//  ⚡ Bash · Check project structure · 142ms        ✓ success
//  ▼ │ $ ls -la /home/user/project
//    │
//    │   ✓ exit 0
//    │
//    │    1 total 44
//    │    2 drwxrwxr-x  7 user user 4096 ...
//    │    3 ...
//    │   [+15 more lines]
//
//  ▸ Bash · Check project structure · 142ms        ✓ success
//  ▸ │ [collapsed - press 'c' to expand]

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
                   bool focused) {

    // ── Header line ──
    Elements header_parts;

    // Collapse/expand indicator
    if (collapsible) {
        std::string collapse_icon = collapsed ? "↓ " : "↑ ";
        header_parts.push_back(text(collapse_icon) | color(accent_color) | bold);
    } else if (!icon.empty()) {
        // Add spacing if not collapsible but has icon
        header_parts.push_back(text("  "));
    }

    // Icon
    if (!icon.empty()) {
        header_parts.push_back(text(" " + icon + " ") | color(accent_color));
    }

    // Tool name
    header_parts.push_back(text(title) | bold | color(accent_color));

    // Description
    if (!description.empty()) {
        std::string desc = description;
        if (desc.size() > 60) desc = desc.substr(0, 57) + "...";
        header_parts.push_back(text(" · ") | dim);
        header_parts.push_back(text(desc) | dim);
    }

    // Duration
    if (duration_ms > 0) {
        std::string timing;
        if (duration_ms < 1000) {
            timing = std::to_string((int)duration_ms) + "ms";
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1fs", duration_ms / 1000.0);
            timing = buf;
        }
        header_parts.push_back(text(" · ") | dim);
        header_parts.push_back(text(timing) | dim | color(Color::RGB(140, 140, 140)));
    }

    header_parts.push_back(filler());

    // Status badge
    if (!status.empty()) {
        Color badge_color = accent_color;
        std::string badge_icon;
        if (status == "success" || status == "completed") {
            badge_color = Color::Green;
            badge_icon = "✓ ";
        } else if (status == "failed" || status == "error") {
            badge_color = Color::Red;
            badge_icon = "✗ ";
        } else if (status == "running" || status == "calling") {
            badge_color = Color::Yellow;
            badge_icon = "⠋ ";
        }
        header_parts.push_back(
            text(badge_icon + status + " ") | color(badge_color));
    }

    // Collapsed hint
    if (collapsed && collapsible) {
        header_parts.push_back(text(" [collapsed - press 'c' to expand]") | dim | color(Color::GrayDark));
    }

    // ── Build the block with accent-colored left border ──
    Elements block_parts;
    block_parts.push_back(hbox(std::move(header_parts)));
    
    if (!collapsed) {
        block_parts.push_back(
            hbox({
                text("│") | color(accent_color) | (focused ? bold : nothing),
                text(" "),
                vbox(std::move(content)) | flex,
            })
        );
    }
    
    block_parts.push_back(text(""));  // spacing after block

    return vbox(std::move(block_parts));
}

// ── Legacy BlockTool (compatibility wrapper) ──
Element BlockTool(const std::string& title, Element content,
                   bool is_running, const std::string& status,
                   Color border_color) {
    return ToolBlock("🔧", title, "", std::move(content),
                      is_running, status, border_color, 0.0);
}

// ════════════════════════════════════════════════════════════════════════════
//  Output truncation
// ════════════════════════════════════════════════════════════════════════════
Element render_truncated_output(const std::string& output,
                                 int max_lines,
                                 const std::string& theme) {
    Elements lines;
    std::istringstream stream(output);
    std::string line;
    int line_num = 1;
    int total_lines = 0;

    // Count total lines first
    {
        std::istringstream counter(output);
        std::string tmp;
        while (std::getline(counter, tmp)) total_lines++;
    }

    while (std::getline(stream, line)) {
        if (line_num > max_lines) {
            int remaining = total_lines - max_lines;
            if (remaining > 0) {
                lines.push_back(hbox({
                    text("     ") | dim,
                    text("[+" + std::to_string(remaining) + " more lines]") |
                        dim | color(accent(theme)),
                }));
            }
            break;
        }

        // Right-aligned line numbers
        std::string num_str = std::to_string(line_num);
        while (num_str.size() < 4) num_str = " " + num_str;

        lines.push_back(hbox({
            text("  " + num_str) | dim | color(Color::GrayDark),
            text(" " + line) | color(Color::RGB(200, 200, 200)),
        }));
        line_num++;
    }

    return vbox(std::move(lines));
}

// ════════════════════════════════════════════════════════════════════════════
//  Extract summary for tool calls (used in the "calling" state)
// ════════════════════════════════════════════════════════════════════════════
static std::string extract_tool_description(const ai::ToolCallContentPart& part) {
    const auto& args = part.arguments;
    if (!args.is_object()) return "";

    // Bash: show command
    if (part.tool_name == "bash" || part.tool_name == "shell" ||
        part.tool_name == "run_command") {
        for (const auto& key : {"description", "desc"}) {
            if (args.contains(key) && args[key].is_string()) {
                std::string d = args[key].get<std::string>();
                if (d.size() > 60) d = d.substr(0, 57) + "...";
                return d;
            }
        }
    }

    // Task: show description
    if (part.tool_name == "task" || part.tool_name == "dispatch_agent") {
        for (const auto& key : {"description", "prompt"}) {
            if (args.contains(key) && args[key].is_string()) {
                std::string d = args[key].get<std::string>();
                if (d.size() > 60) d = d.substr(0, 57) + "...";
                return d;
            }
        }
    }

    return "";
}

static std::string extract_tool_summary(const ai::ToolCallContentPart& part) {
    const auto& args = part.arguments;
    if (!args.is_object()) return args.dump();

    // Bash: show command
    if (part.tool_name == "bash" || part.tool_name == "shell" ||
        part.tool_name == "run_command") {
        for (const auto& key : {"command", "cmd", "script"}) {
            if (args.contains(key) && args[key].is_string()) {
                std::string cmd = args[key].get<std::string>();
                if (cmd.size() > 100) cmd = cmd.substr(0, 97) + "...";
                return "$ " + cmd;
            }
        }
    }

    // File: show path
    if (part.tool_name == "read_file" || part.tool_name == "write_file" ||
        part.tool_name == "view_file" || part.tool_name == "edit_file") {
        for (const auto& key : {"path", "file", "file_path", "filename"}) {
            if (args.contains(key) && args[key].is_string()) {
                return args[key].get<std::string>();
            }
        }
    }

    // Search: show query
    if (part.tool_name == "search" || part.tool_name == "grep" ||
        part.tool_name == "ripgrep") {
        for (const auto& key : {"query", "pattern"}) {
            if (args.contains(key) && args[key].is_string()) {
                std::string q = args[key].get<std::string>();
                if (q.size() > 80) q = q.substr(0, 77) + "...";
                return "\"" + q + "\"";
            }
        }
    }

    // Task: show description
    if (part.tool_name == "task" || part.tool_name == "dispatch_agent") {
        for (const auto& key : {"description", "prompt"}) {
            if (args.contains(key) && args[key].is_string()) {
                std::string d = args[key].get<std::string>();
                if (d.size() > 80) d = d.substr(0, 77) + "...";
                return d;
            }
        }
    }

    // Generic: first string value
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (it.value().is_string()) {
            std::string val = it.value().get<std::string>();
            if (val.size() > 80) val = val.substr(0, 77) + "...";
            return val;
        }
    }

    auto s = args.dump();
    if (s.size() > 60) s = s.substr(0, 57) + "...";
    return s;
}

// ════════════════════════════════════════════════════════════════════════════
//  Render tool call (in-progress: "calling" state)
// ════════════════════════════════════════════════════════════════════════════
static Element render_tool_call(const ai::ToolCallContentPart& part,
                                 const std::string& theme) {
    std::string icon = tool_icon(part.tool_name);
    std::string display = tool_display_name(part.tool_name);
    std::string desc = extract_tool_description(part);
    std::string summary = extract_tool_summary(part);

    // Show the summary as content while calling
    Elements call_content;
    if (!summary.empty()) {
        call_content.push_back(hbox({
            text(summary) | dim,
        }));
    }

    return ToolBlock(icon, display, desc, vbox(std::move(call_content)),
                      true, "calling", accent(theme), 0.0);
}

// ════════════════════════════════════════════════════════════════════════════
//  Render tool result (completed)
// ════════════════════════════════════════════════════════════════════════════
static Element render_tool_result(const ai::ToolResultContentPart& part,
                                   const std::string& theme) {
    // Determine tool name from context — we look at the result structure
    // Since ToolResultContentPart doesn't carry tool_name, we infer from result
    std::string tool_name;
    nlohmann::json args;

    if (part.result.is_object()) {
        // Try to get tool info from result metadata
        if (part.result.contains("tool_name") && part.result["tool_name"].is_string()) {
            tool_name = part.result["tool_name"].get<std::string>();
        }
        // Extract args if embedded
        if (part.result.contains("arguments") && part.result["arguments"].is_object()) {
            args = part.result["arguments"];
        }
    }

    // Determine status
    std::string status = part.is_error ? "failed" : "success";
    Color status_color = part.is_error ? Color::Red : Color::Green;

    // If we can identify the tool type, use specialized renderer
    std::string icon = tool_icon(tool_name);
    std::string display = tool_display_name(tool_name);

    // Extract description from result
    std::string desc;
    if (part.result.is_object()) {
        if (part.result.contains("title") && part.result["title"].is_string()) {
            desc = part.result["title"].get<std::string>();
        } else if (part.result.contains("metadata") && part.result["metadata"].is_object()) {
            auto& meta = part.result["metadata"];
            if (meta.contains("description") && meta["description"].is_string()) {
                desc = meta["description"].get<std::string>();
            }
        }
    }

    // Error case: simple error block
    if (part.is_error) {
        Elements error_content;
        std::string err_detail;
        if (part.result.is_string()) {
            err_detail = part.result.get<std::string>();
        } else if (part.result.is_object() && part.result.contains("error")) {
            err_detail = part.result["error"].get<std::string>();
        } else if (part.result.is_object() && part.result.contains("output")) {
            err_detail = part.result["output"].get<std::string>();
        }
        if (!err_detail.empty()) {
            error_content.push_back(render_truncated_output(err_detail, 10, theme));
        }
        return ToolBlock(icon, display, desc, vbox(std::move(error_content)),
                          false, status, Color::Red, part.duration_ms);
    }

    // Route to per-tool renderer
    Element inner;
    bool is_bash = (tool_name == "bash" || tool_name == "shell" || tool_name == "run_command");
    bool is_task = (tool_name == "task" || tool_name == "dispatch_agent");
    bool is_file = (tool_name == "read_file" || tool_name == "write_file" ||
                    tool_name == "view_file" || tool_name == "edit_file");
    bool is_search = (tool_name == "search" || tool_name == "grep" || tool_name == "ripgrep");

    if (is_bash) {
        inner = RenderBashResult(args, part.result, part.is_error, part.duration_ms, theme);
    } else if (is_task) {
        inner = RenderTaskResult(args, part.result, part.is_error, part.duration_ms, theme);
    } else if (is_file) {
        inner = RenderFileResult(tool_name, args, part.result, part.is_error, part.duration_ms, theme);
    } else if (is_search) {
        inner = RenderSearchResult(args, part.result, part.is_error, part.duration_ms, theme);
    } else {
        // Fallback: structured result rendering
        inner = RenderGenericResult(tool_name, args, part.result, part.is_error, part.duration_ms, theme);
    }

    return ToolBlock(icon, display, desc, std::move(inner),
                      false, status, status_color, part.duration_ms);
}

// ════════════════════════════════════════════════════════════════════════════
//  Reasoning / thinking block
// ════════════════════════════════════════════════════════════════════════════
static Element render_reasoning(const ai::ReasoningContentPart& rp,
                                 const std::string& theme) {
    if (rp.text.empty()) return Element();
    Elements md = render_markdown(rp.text);
    Elements indented;
    for (auto& el : md) {
        indented.push_back(hbox({text("  "), std::move(el)}));
    }
    auto border_color = Color::GrayDark;
    return vbox({
        hbox({
            text(" 💭 ") | color(accent(theme)),
            text("Thinking") | dim | color(accent(theme)),
        }),
        hbox({
            text("│") | color(border_color),
            text(" "),
            vbox(std::move(indented)) | flex,
        }),
        text(""),
    });
}

// ════════════════════════════════════════════════════════════════════════════
//  Render a complete message (user/assistant/system)
// ════════════════════════════════════════════════════════════════════════════
//  This function pairs tool calls with their results by matching
//  ToolCallContentPart.id with ToolResultContentPart.tool_call_id
//  and renders them as collapsible combined blocks.
Element render_message(const ai::Message& msg, const ChatState& state,
                        const std::vector<ProviderInfo>& providers_list,
                        int selected_provider, int selected_model,
                        const std::string& theme) {

    Elements parts;

    // Reset focused tool navigation order (repopulated during render)
    if (state.tool_block_order) state.tool_block_order->clear();

    bool has_text = msg.has_text();
    bool has_tool_calls = msg.has_tool_calls();
    bool has_tool_results = msg.has_tool_results();

    // ── Role header ──
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
            header.push_back(text(" · " + model_label) | dim | color(Color::GrayDark));
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

    // ── Collect tool calls and results for pairing ──
    std::unordered_map<std::string, const ai::ToolCallContentPart*> tool_calls;
    std::unordered_map<std::string, const ai::ToolResultContentPart*> tool_results;

    for (const auto& part : msg.content) {
        if (const auto* tool_part = std::get_if<ai::ToolCallContentPart>(&part)) {
            tool_calls[tool_part->id] = tool_part;
        } else if (const auto* result_part = std::get_if<ai::ToolResultContentPart>(&part)) {
            tool_results[result_part->tool_call_id] = result_part;
        }
    }

    // ── Render content parts, pairing tool calls with results ──
    std::unordered_set<std::string> rendered_tool_calls;
    std::unordered_set<std::string> rendered_tool_results;

    for (const auto& part : msg.content) {
        if (const auto* text_part = std::get_if<ai::TextContentPart>(&part)) {
            if (!text_part->text.empty()) {
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
            }
        } else if (const auto* tool_part = std::get_if<ai::ToolCallContentPart>(&part)) {
            // Check if there's a matching result
            auto result_it = tool_results.find(tool_part->id);
            if (result_it != tool_results.end()) {
                // Paired: render combined tool call + result block
                const auto* result_part = result_it->second;
                
                // Check collapse state
                bool collapsed = false;
                if (state.tool_collapse_state && state.tool_collapse_state->count(tool_part->id)) {
                    collapsed = (*state.tool_collapse_state)[tool_part->id];
                }

                // Track for keyboard navigation
                bool focused = false;
                if (state.tool_block_order) {
                    int idx = static_cast<int>(state.tool_block_order->size());
                    state.tool_block_order->push_back(tool_part->id);
                    if (state.focused_tool_index &&
                        *state.focused_tool_index == idx) {
                        focused = true;
                    }
                }

                parts.push_back(render_tool_pair(*tool_part, *result_part, theme,
                                                 collapsed, true, focused));
                
                rendered_tool_calls.insert(tool_part->id);
                rendered_tool_results.insert(tool_part->id);
            } else {
                // Orphaned tool call (no result yet) - render as running
                parts.push_back(render_tool_call(*tool_part, theme));
                rendered_tool_calls.insert(tool_part->id);
            }
        } else if (const auto* result_part = std::get_if<ai::ToolResultContentPart>(&part)) {
            // Check if this result was already rendered with its call
            if (rendered_tool_results.count(result_part->tool_call_id)) {
                continue; // Already rendered as paired block
            }
            // Orphaned tool result (no matching call in this message) - render standalone
            parts.push_back(render_tool_result(*result_part, theme));
            rendered_tool_results.insert(result_part->tool_call_id);
        } else if (const auto* reasoning_part = std::get_if<ai::ReasoningContentPart>(&part)) {
            if (*state.show_thinking) {
                parts.push_back(render_reasoning(*reasoning_part, theme));
            }
        }
    }

    return vbox(std::move(parts));
}

// ════════════════════════════════════════════════════════════════════════════
//  Render paired tool call + result as collapsible block
// ════════════════════════════════════════════════════════════════════════════
static Element render_tool_pair(const ai::ToolCallContentPart& call_part,
                                 const ai::ToolResultContentPart& result_part,
                                 const std::string& theme,
                                 bool collapsed,
                                 bool collapsible,
                                 bool focused) {
    std::string icon = tool_icon(call_part.tool_name);
    std::string display = tool_display_name(call_part.tool_name);
    std::string desc = extract_tool_description(call_part);

    // Extract description from result if available
    std::string result_desc;
    if (result_part.result.is_object()) {
        if (result_part.result.contains("title") && result_part.result["title"].is_string()) {
            result_desc = result_part.result["title"].get<std::string>();
        } else if (result_part.result.contains("metadata") && result_part.result["metadata"].is_object()) {
            auto& meta = result_part.result["metadata"];
            if (meta.contains("description") && meta["description"].is_string()) {
                result_desc = meta["description"].get<std::string>();
            }
        }
    }
    // Prefer result description if available
    if (!result_desc.empty()) desc = result_desc;

    // Determine status and color
    std::string status = result_part.is_error ? "failed" : "success";
    Color status_color = result_part.is_error ? Color::Red : Color::Green;
    // unused: Color accent_clr = accent(theme);

    // Build combined content: call summary + result content
    Elements combined_content;

    // Call summary (arguments)
    std::string summary = extract_tool_summary(call_part);
    if (!summary.empty()) {
        combined_content.push_back(hbox({
            text("▸ ") | dim | color(Color::GrayDark),
            text("Input: ") | bold | dim,
            text(summary) | dim,
        }));
    }

    // Result content - use specialized renderers
    Element result_inner;
    nlohmann::json args = call_part.arguments;

    bool is_bash = (call_part.tool_name == "bash" || call_part.tool_name == "shell" || call_part.tool_name == "run_command");
    bool is_task = (call_part.tool_name == "task" || call_part.tool_name == "dispatch_agent");
    bool is_file = (call_part.tool_name == "read_file" || call_part.tool_name == "write_file" ||
                    call_part.tool_name == "view_file" || call_part.tool_name == "edit_file");
    bool is_search = (call_part.tool_name == "search" || call_part.tool_name == "grep" || call_part.tool_name == "ripgrep");

    if (result_part.is_error) {
        // Error case
        Elements error_content;
        std::string err_detail;
        if (result_part.result.is_string()) {
            err_detail = result_part.result.get<std::string>();
        } else if (result_part.result.is_object() && result_part.result.contains("error")) {
            err_detail = result_part.result["error"].get<std::string>();
        } else if (result_part.result.is_object() && result_part.result.contains("output")) {
            err_detail = result_part.result["output"].get<std::string>();
        }
        if (!err_detail.empty()) {
            error_content.push_back(render_truncated_output(err_detail, 10, theme));
        }
        result_inner = vbox(std::move(error_content));
    } else if (is_bash) {
        result_inner = RenderBashResult(args, result_part.result, result_part.is_error, result_part.duration_ms, theme);
    } else if (is_task) {
        result_inner = RenderTaskResult(args, result_part.result, result_part.is_error, result_part.duration_ms, theme);
    } else if (is_file) {
        result_inner = RenderFileResult(call_part.tool_name, args, result_part.result, result_part.is_error, result_part.duration_ms, theme);
    } else if (is_search) {
        result_inner = RenderSearchResult(args, result_part.result, result_part.is_error, result_part.duration_ms, theme);
    } else {
        // Fallback: structured result rendering
        result_inner = RenderGenericResult(call_part.tool_name, args, result_part.result, result_part.is_error, result_part.duration_ms, theme);
    }

    combined_content.push_back(std::move(result_inner));

    return ToolBlock(icon, display, desc, vbox(std::move(combined_content)),
                      false, status, Color::Default, result_part.duration_ms,
                      collapsed, collapsible, focused);
}

} // namespace tui
} // namespace ai
