#include <ai/tui/views.h>
#include <ai/tui/db.h>
#include <ai/logger.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <cstdio>
#include <unordered_map>

namespace ai {
namespace tui {

using namespace ftxui;

// Raw helper to get git diff of the selected file
static std::string get_file_diff_raw(const std::string& path) {
    if (path.empty()) return "";
    
    std::string diff_output;
    std::array<char, 512> buffer;
    
    // Check if the file is tracked
    std::string check_cmd = "git ls-files --error-unmatch \"" + path + "\" 2>/dev/null";
    FILE* check_pipe = popen(check_cmd.c_str(), "r");
    bool is_tracked = false;
    if (check_pipe) {
        is_tracked = (pclose(check_pipe) == 0);
    }

    std::string cmd;
    if (is_tracked) {
        // Tracked file: show diff against HEAD (unstaged + staged changes)
        cmd = "git diff HEAD -- \"" + path + "\" 2>/dev/null";
    } else {
        // Untracked file: show diff as a new file (all lines added)
        cmd = "git diff --no-index /dev/null \"" + path + "\" 2>/dev/null";
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            diff_output += buffer.data();
        }
        pclose(pipe);
    }

    // Fallback if diff is empty (e.g., untracked new file outside git index entirely)
    if (diff_output.empty() && std::filesystem::exists(path)) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                diff_output += "+" + line + "\n";
            }
        }
    }

    return diff_output;
}

// Cached helper using file modification timestamp to optimize rendering performance
static std::string get_file_diff(const std::string& path) {
    static std::unordered_map<std::string, std::string> diff_cache;
    static std::unordered_map<std::string, std::filesystem::file_time_type> time_cache;

    if (path.empty()) return "";

    try {
        bool needs_update = (diff_cache.find(path) == diff_cache.end());
        if (std::filesystem::exists(path)) {
            auto current_time = std::filesystem::last_write_time(path);
            if (!needs_update && time_cache[path] != current_time) {
                needs_update = true;
            }
            if (needs_update) {
                time_cache[path] = current_time;
            }
        }

        if (needs_update) {
            diff_cache[path] = get_file_diff_raw(path);
        }
    } catch (...) {
        return get_file_diff_raw(path);
    }

    return diff_cache[path];
}

ftxui::Element render_logo() {
    auto cyan  = Color::RGB(22, 184, 243);
    auto blue  = Color::RGB(72, 124, 255);
    return vbox({
        hbox({ text("        ") | color(cyan), text("                         ") | color(blue) }),
        hbox({ text("█▀▀█    ") | color(cyan), text("  ▀▀▀▀ █▀▀█ █▀▀▄ █▀▀ ") | color(blue) | bold }),
        hbox({ text("█  █ ▀▀ ") | color(cyan), text("  █    █  █ █  █ ▀▀ ") | color(blue) | bold }),
        hbox({ text("▀▀▀█▀   ") | color(cyan), text("  ▀▀▀▀ ▀▀▀▀ ▀▀▀  ▀▀▀ ") | color(blue) | bold }),
    }) | hcenter;
}

// Helper to render code/diff content line by line to preserve formatting and highlight changes
static ftxui::Element render_diff_content(const std::string& content) {
    using namespace ftxui;
    Elements lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        Color c = Color::Default;
        if (!line.empty()) {
            if (line[0] == '+') c = Color::Green;
            else if (line[0] == '-') c = Color::Red;
            else if (line[0] == '@') c = Color::Cyan;
        }
        lines.push_back(text(line) | color(c));
    }
    return vbox(std::move(lines));
}

// Forward declarations of popups with theme support
ftxui::Element build_model_popup(
    const std::vector<ModelEntry>& entries,
    int select_idx,
    int selected_provider,
    int selected_model,
    const std::string& theme
);
ftxui::Element build_session_popup(
    const std::vector<std::pair<std::string, std::string>>& entries,
    int select_idx,
    const std::string& active_session_id,
    const std::string& theme
);


// ── Basic markdown rendering (code blocks, bold, inline code, lists) ──
static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    return lines;
}

// ── Basic markdown rendering (code blocks, inline code, bold, lists) ──
static ftxui::Elements render_markdown(const std::string& input_text) {
    using namespace ftxui;
    Elements result;

    if (input_text.empty()) return result;

    std::string remain = input_text;
    
    // Process code blocks first (```...```)
    while (!remain.empty()) {
        auto code_start = remain.find("```");
        if (code_start == std::string::npos) {
            // No more code blocks, process remaining as inline text
            break;
        }
        
        // Text before code block
        if (code_start > 0) {
            std::string before = remain.substr(0, code_start);
            if (!before.empty()) {
                result.push_back(paragraph(before) | flex);
            }
        }
        
        remain.erase(0, code_start + 3);
        
        // Skip optional language tag line
        auto code_end = remain.find("```");
        if (code_end == std::string::npos) {
            // Unclosed code block — treat rest as code
            result.push_back(text("") | size(HEIGHT, EQUAL, 1));
            for (auto& line : split_lines(remain)) {
                result.push_back(
                    text("  " + line) | bgcolor(Color::RGB(0x1E, 0x1E, 0x2E)) | color(Color::RGB(0xBB, 0xBB, 0xBB))
                );
            }
            result.push_back(text(""));
            remain.clear();
            break;
        }
        
        std::string code_content = remain.substr(0, code_end);
        if (!code_content.empty() && code_content.front() == '\n') code_content.erase(0, 1);
        
        result.push_back(text("") | size(HEIGHT, EQUAL, 1));
        
        std::istringstream code_ss(code_content);
        std::string code_line;
        while (std::getline(code_ss, code_line)) {
            result.push_back(
                text("  " + code_line) | bgcolor(Color::RGB(0x1E, 0x1E, 0x2E)) | color(Color::RGB(0xBB, 0xBB, 0xBB))
            );
        }
        result.push_back(text(""));
        
        remain.erase(0, code_end + 3);
    }
    
    // Process remaining text line by line for inline markdown
    if (!remain.empty()) {
        auto lines = split_lines(remain);
        for (const auto& line : lines) {
            if (line.empty()) {
                result.push_back(text(""));
                continue;
            }
            
            // Check for list markers
            bool is_list = false;
            std::string prefix;
            if (line.size() >= 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ') {
                is_list = true;
                prefix = "  \u2022 ";  // bullet
            } else if (line.size() >= 3 && std::isdigit(line[0]) && line[1] == '.' && line[2] == ' ') {
                is_list = true;
                prefix = "  " + line.substr(0, 2) + " ";
            }
            
            std::string line_prefix = is_list ? prefix : "";
            
            // Parse inline formatting (**bold**, `inline code`)
            // Split into segments by format markers
            Elements inline_elems;
            if (!line_prefix.empty()) {
                inline_elems.push_back(text(line_prefix) | dim);
            }
            
            // Parse segments
            // We handle **bold** and `code` markers
            struct Segment {
                std::string text;
                bool is_bold;
                bool is_code;
            };
            std::vector<Segment> segments;
            std::string parsing_text = is_list 
                ? line.substr(2)  // skip "- " or "* " or "1."
                : line;
            // For numbered lists, skip the number part too
            if (is_list && std::isdigit(line[0])) {
                auto dot_pos = line.find('.');
                if (dot_pos != std::string::npos && dot_pos + 2 <= line.size()) {
                    parsing_text = line.substr(dot_pos + 2);
                }
            }
            
            std::string buf;
            bool in_bold = false;
            bool in_code = false;
            size_t j = 0;
            while (j < parsing_text.size()) {
                if (!in_code && !in_bold && parsing_text.substr(j, 2) == "**") {
                    if (!buf.empty()) {
                        segments.push_back({buf, false, false});
                        buf.clear();
                    }
                    in_bold = true;
                    j += 2;
                } else if (in_bold && parsing_text.substr(j, 2) == "**") {
                    if (!buf.empty()) {
                        segments.push_back({buf, true, false});
                        buf.clear();
                    }
                    in_bold = false;
                    j += 2;
                } else if (!in_bold && parsing_text[j] == '`') {
                    if (!buf.empty()) {
                        segments.push_back({buf, false, false});
                        buf.clear();
                    }
                    in_code = true;
                    j++;
                } else if (in_code && parsing_text[j] == '`') {
                    if (!buf.empty()) {
                        segments.push_back({buf, false, true});
                        buf.clear();
                    }
                    in_code = false;
                    j++;
                } else {
                    buf += parsing_text[j];
                    j++;
                }
            }
            if (!buf.empty()) {
                segments.push_back({buf, false, false});
            }
            
            // Build inline elements from segments
            for (const auto& seg : segments) {
                if (seg.is_code) {
                    inline_elems.push_back(
                        text(seg.text) | bgcolor(Color::RGB(0x2D, 0x2D, 0x3D)) | color(Color::RGB(0xEE, 0x99, 0x77))
                    );
                } else if (seg.is_bold) {
                    inline_elems.push_back(text(seg.text) | bold);
                } else {
                    inline_elems.push_back(text(seg.text));
                }
            }
            
            result.push_back(hbox(std::move(inline_elems)));
        }
    }
    
    return result;
}

ftxui::Element render_view(
    const ChatState& state,
    const std::vector<ProviderInfo>& providers_list,
    int selected_provider,
    int selected_model,
    bool enable_tools,
    const std::string& prompt_input,
    bool show_slash,
    int slash_idx,
    const std::vector<SlashCommand>& slash_commands,
    bool show_model_select,
    int model_select_idx,
    const std::vector<ModelEntry>& model_entries,
    bool show_session_select,
    int session_select_idx,
    const std::vector<std::pair<std::string, std::string>>& session_entries,
    const ftxui::Component& tab_toggle,
    const ftxui::Component& files_menu,
    const std::shared_ptr<int>& scroll_offset,
    const ftxui::Component& input
) {
    std::string theme = state.theme ? *state.theme : "orange";

    // ── Outer Layout — header (tab selection) + body + footer ──
    auto header = hbox({
        text(" QCODE ") | bold | bgcolor(accent(theme)) | color(Color::White),
        text("  "),
        tab_toggle->Render() | flex,
    }) | borderLight | color(accent(theme));

    Element body;

    // ── Tab 0: Chat ──
    if (state.tab_selected == 0) {
        bool empty = state.chat_history->empty();

        if (empty) {
            // Home screen
            auto prompt_bar = vbox({
                hbox({
                    text(" ❯ ") | color(accent2(theme)) | bold,
                    input->Render() | flex,
                }),
                separatorLight() | color(accent(theme)),
                hbox({
                    text(" " + providers_list[selected_provider].models[selected_model].name + " ") | bold | bgcolor(accent(theme)) | color(Color::Black),
                    text(" "),
                    text(enable_tools ? " 🔧 Tools: ON " : " ⚙ Tools: OFF ") | bold | bgcolor(enable_tools ? Color::RGB(0x22, 0xBB, 0x88) : Color::RGB(0x44, 0x44, 0x44)) | color(Color::White),
                    filler(),
                    text("Press Enter to send · Alt+Enter for newline ") | dim,
                })
            }) | border | color(accent(theme));

            body = vbox({
                filler() | flex,
                render_logo(),
                text("") | size(HEIGHT, EQUAL, 1),
                text("What can I help you build today?") | dim | hcenter,
                prompt_bar | size(WIDTH, EQUAL, 80) | hcenter,
                filler() | flex,
            }) | flex;
        } else {
            // Chat history list
            Elements msgs;
            for (int i = 0; i < static_cast<int>(state.chat_history->size()); i++) {
                auto& entry = (*state.chat_history)[i];
                Color c = entry.first == "User" ? user_green()
                        : entry.first == "System" ? dim_gray() : accent2(theme);
                
                Element bubble;
                if (entry.first == "System") {
                    /* ── Centered dimmed info banner ── */
                    bubble = hbox({
                        filler(),
                        vbox({
                            paragraph("  ℹ " + entry.second) | dim | hcenter,
                        }) | flex,
                        filler(),
                    });
                } else if (entry.first == "ToolCall") {
                    /* ── OpenCode-style ToolCall rendering ── */
                    auto nl = entry.second.find(static_cast<char>(10));
                    std::string tc_header = (nl != std::string::npos)
                        ? entry.second.substr(0, nl) : entry.second;
                    std::string tc_body = (nl != std::string::npos && nl + 1 < entry.second.size())
                        ? entry.second.substr(nl + 1) : "";
                    
                    // Strip box-drawing symbols from old DB entries if any are left
                    if (tc_header.starts_with("┌ ")) tc_header = tc_header.substr(2);
                    if (tc_header.starts_with("┌ Tool Call · ")) {
                        auto suffix = tc_header.substr(14);
                        tc_header = "Tool Call · " + suffix;
                    } else if (tc_header.starts_with("Tool Call · ")) {
                        // Keep as is
                    }

                    while (!tc_body.empty() && tc_body.front() == ' ') tc_body.erase(0, 1);
                    /* Trim trailing whitespace */
                    while (!tc_body.empty() && (tc_body.back() == '\n' || tc_body.back() == ' ')) tc_body.pop_back();

                    Color tc_color = Color::RGB(180, 220, 120);
                    Elements tc_lines;
                    tc_lines.push_back(text("  🔧 " + tc_header) | bold | color(tc_color));
                    if (!tc_body.empty()) {
                        std::istringstream tc_ss(tc_body);
                        std::string tc_line;
                        while (std::getline(tc_ss, tc_line)) {
                            // Strip old box drawing characters
                            if (tc_line.starts_with("│ ")) tc_line = tc_line.substr(2);
                            else if (tc_line == "│") tc_line = "";
                            
                            tc_lines.push_back(text("    " + tc_line) | color(tc_color) | dim);
                        }
                    }
                    bubble = hbox({
                        separatorLight() | color(tc_color),
                        vbox(std::move(tc_lines)) | flex
                    });
                } else if (entry.first == "ToolResult") {
                    /* ── OpenCode-style ToolResult rendering ── */
                    auto nl = entry.second.find(static_cast<char>(10));
                    std::string tr_status = (nl != std::string::npos)
                        ? entry.second.substr(0, nl) : entry.second;
                    std::string tr_output = (nl != std::string::npos && nl + 1 < entry.second.size())
                        ? entry.second.substr(nl + 1) : "";
                    
                    // Strip box-drawing symbols from old DB entries if any are left
                    if (tr_status.starts_with("└─ ")) tr_status = tr_status.substr(3);
                    if (tr_status.starts_with("└ ")) tr_status = tr_status.substr(2);

                    while (!tr_output.empty() && tr_output.front() == ' ') tr_output.erase(0, 1);
                    while (!tr_output.empty() && (tr_output.back() == '\n' || tr_output.back() == ' ')) tr_output.pop_back();

                    bool tr_success = tr_status.find("Failed") == std::string::npos;
                    Color tr_color = tr_success ? Color::Green : Color::Red;
                    std::string icon = tr_success ? "✔ " : "❌ ";

                    Elements tr_lines;
                    tr_lines.push_back(text("  " + icon + tr_status) | bold | color(tr_color));
                    if (!tr_output.empty()) {
                        std::istringstream tr_ss(tr_output);
                        std::string tr_line;
                        while (std::getline(tr_ss, tr_line)) {
                            // Strip old box drawing characters
                            if (tr_line.starts_with("│ ")) tr_line = tr_line.substr(2);
                            else if (tr_line == "│") tr_line = "";
                            
                            tr_lines.push_back(text("    " + tr_line) | dim);
                        }
                    }
                    bubble = hbox({
                        separatorLight() | color(tr_color),
                        vbox(std::move(tr_lines)) | flex
                    });
                } else if (entry.first == "User") {
                    /* ── User: left-aligned, green accent, "You" header ── */
                    Elements user_parsed = render_markdown(entry.second);
                    user_parsed.insert(user_parsed.begin(),
                        text("  You") | bold | color(user_green()));
                    bubble = hbox({
                        separatorLight() | color(user_green()),
                        vbox(std::move(user_parsed)) | flex,
                    });
                } else {
                    /* ── Assistant message with thinking token support ── */
                    auto& msg_text = entry.second;
                    Elements assistant_elems;
                    
                    // Unified header with model name matching OpenCode style
                    assistant_elems.push_back(
                        hbox({
                            text("  Assistant") | bold | color(accent2(theme)),
                            text(" · " + providers_list[selected_provider].models[selected_model].name) | dim | color(accent(theme))
                        })
                    );

                    /* Parse <thinking>...</thinking> blocks */
                    LOG_DEBUG("Views: parsing assistant msg size={}", entry.second.size());
                    std::string remain = entry.second;
                    bool has_thinking = false;
                    std::string text_buf;

                    while (!remain.empty()) {
                        auto pos = remain.find("<thinking>");
                        if (pos == std::string::npos) {
                            text_buf += remain;
                            break;
                        }
                        text_buf += remain.substr(0, pos);
                        remain.erase(0, pos + 10);  // skip <thinking>
                        has_thinking = true;
                        LOG_DEBUG("Views: found <thinking> block at pos {}", pos);

                        auto close_pos = remain.find("</thinking>");
                        if (close_pos == std::string::npos) {
                            text_buf += remain;
                            break;
                        }
                        std::string think_content = remain.substr(0, close_pos);
                        remain.erase(0, close_pos + 12);  // skip </thinking>

                        // Flush accumulated text buffer (text before thinking)
                        if (!text_buf.empty()) {
                            Elements flushed = render_markdown("  " + text_buf);
                            for (auto& el : flushed)
                                assistant_elems.push_back(std::move(el));
                            text_buf.clear();
                        }

                        // Render thinking content dimmed (OpenCode reasoning-part style)
                        if (!think_content.empty()) {
                            // Trim leading/trailing newlines
                            while (!think_content.empty() && (think_content.front() == '\n' || think_content.front() == ' '))
                                think_content.erase(0, 1);
                            while (!think_content.empty() && (think_content.back() == '\n' || think_content.back() == ' '))
                                think_content.pop_back();
                            if (!think_content.empty()) {
                                LOG_DEBUG("Views: rendering thinking block ({} chars)", think_content.size());
                                // Match OpenCode: dimmed markdown, no header
                                Elements thought_el = render_markdown("    " + think_content);
                                for (auto& el : thought_el)
                                    assistant_elems.push_back(el | dim | color(Color::RGB(0x99, 0x99, 0x99)));
                            }
                        }
                    }

                    // Append remaining text after thinking
                    if (!text_buf.empty()) {
                        Elements remaining = render_markdown("  " + text_buf);
                        for (auto& el : remaining)
                            assistant_elems.push_back(std::move(el));
                    }

                    bubble = hbox({
                        separatorLight() | color(accent2(theme)),
                        vbox(std::move(assistant_elems)) | flex
                    });
                }
                if (state.selection_mode) {
                    if (state.selected_message == i) {
                        bubble = bubble | bgcolor(Color::RGB(0x33, 0x33, 0x33)) | bold | focus;
                    }
                } else {
                    if (i == static_cast<int>(state.chat_history->size()) - 1) {
                        bubble = bubble | focus;
                    }
                }

                msgs.push_back(bubble);
                msgs.push_back(text(""));
            }

            // Animated spinner during generation
            static const std::array<const char*, 10> spinner_frames = {
                "⠋", "⠙", "⠹", "⠸", "⠼",
                "⠴", "⠦", "⠧", "⠏", "⠋"
            };
            std::string status;
            if (*state.is_generating) {
                int frame = *state.generation_frame % spinner_frames.size();
                status = std::string(spinner_frames[frame]) + " Generating...";
            } else {
                status = "";
            }
            if (state.selection_mode) {
                status = "↑↓ navigate  y/Enter copy  Esc exit visual selection";
            }

            // OpenCode-style input bar: compact, no inner separator, model badge inline
            auto prompt_box = vbox({
                hbox({
                    text(" ❯ ") | color(accent2(theme)) | bold,
                    input->Render() | flex,
                }),
                separatorLight() | color(accent(theme)),
                hbox({
                    text(" " + providers_list[selected_provider].models[selected_model].name + " ") | bold | bgcolor(accent(theme)) | color(Color::Black),
                    text(" "),
                    text(enable_tools ? " 🔧 Tools: ON " : " ⚙ Tools: OFF ") | bold | bgcolor(enable_tools ? Color::RGB(0x22, 0xBB, 0x88) : Color::RGB(0x44, 0x44, 0x44)) | color(Color::White),
                    filler(),
                    text("Press Enter to send · Alt+Enter for newline ") | dim,
                })
            }) | border | color(accent(theme));

            // Dynamic inline slash command / session autocomplete matching opencode
            if (prompt_input.size() >= 9 && prompt_input.substr(0, 9) == "/session ") {
                std::string filter_str = prompt_input.substr(9);
                auto all_sessions = db::list_sessions();
                std::vector<std::pair<std::string, std::string>> matches;
                for (const auto& s : all_sessions) {
                    if (s.first.find(filter_str) != std::string::npos ||
                        s.second.find(filter_str) != std::string::npos) {
                        matches.push_back(s);
                    }
                }
                if (!matches.empty()) {
                    Elements rows;
                    rows.push_back(text(" Select Session to Load:") | bold | color(accent2(theme)));
                    rows.push_back(separatorLight() | color(accent(theme)));
                    for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
                        bool active = (state.slash_suggestion_mode && state.slash_suggestion_idx == i);
                        std::string marker = active ? " ▶ " : "   ";
                        auto row = hbox({
                            text(marker + matches[i].first) | color(active ? accent2(theme) : Color::Default) | bold,
                            text("  (" + matches[i].second + ")") | dim
                        });
                        if (active) {
                            row = row | bgcolor(bg_popup()) | bold;
                        }
                        rows.push_back(row);
                    }
                    auto suggestions_panel = vbox(std::move(rows)) | border | color(accent(theme));
                    prompt_box = vbox({
                        suggestions_panel,
                        prompt_box
                    });
                }
            } else if (prompt_input.size() > 0 && prompt_input[0] == '/' && prompt_input.find(' ') == std::string::npos) {
                std::string filter_str = prompt_input.substr(1);
                std::vector<SlashCommand> matches;
                for (const auto& cmd : slash_commands) {
                    if (cmd.name.find(filter_str) != std::string::npos) {
                        matches.push_back(cmd);
                    }
                }
                if (!matches.empty()) {
                    Elements rows;
                    rows.push_back(text(" Autocomplete Commands:") | bold | color(accent2(theme)));
                    rows.push_back(separatorLight() | color(accent(theme)));
                    for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
                        bool active = (state.slash_suggestion_mode && state.slash_suggestion_idx == i);
                        std::string marker = active ? " ▶ " : "   ";
                        auto row = hbox({
                            text(marker + "/" + matches[i].name) | color(active ? accent2(theme) : Color::Default) | bold,
                            text("  " + matches[i].description) | dim
                        });
                        if (active) {
                            row = row | bgcolor(bg_popup()) | bold;
                        }
                        rows.push_back(row);
                    }
                    auto suggestions_panel = vbox(std::move(rows)) | border | color(accent(theme));
                    prompt_box = vbox({
                        suggestions_panel,
                        prompt_box
                    });
                }
            }

            LOG_DEBUG("Views: chat tab render, auto_scroll={}, scroll_offset={}", state.auto_scroll, *scroll_offset);
            body = vbox({
                vbox(std::move(msgs)) | vscroll_indicator | (state.auto_scroll ? focusPositionRelative(0.f, 1.f) : focusPosition(0, *scroll_offset)) | yframe | yflex,
                prompt_box,
            }) | flex;
        }
    }
    // ── Tab 1: Files preview ──
    else if (state.tab_selected == 1) {
        if (state.modified_files->empty()) {
            body = vbox({
                filler() | flex,
                text("No modified files detected in project (git is clean).") | hcenter | dim,
                filler() | flex,
            }) | flex;
        } else {
            Elements file_blocks;
            for (const auto& filepath : *state.modified_files) {
                // Get cached diff content (optimized)
                std::string content = get_file_diff(filepath);
                
                // Count additions/deletions
                int additions = 0;
                int deletions = 0;
                std::stringstream ss(content);
                std::string line;
                while (std::getline(ss, line)) {
                    if (!line.empty()) {
                        if (line[0] == '+' && (line.size() < 2 || line[1] != '+')) additions++;
                        else if (line[0] == '-' && (line.size() < 2 || line[1] != '-')) deletions++;
                    }
                }
                
                auto file_header = hbox({
                    text(" " + filepath) | bold | color(accent2(theme)),
                    filler(),
                    text("+" + std::to_string(additions)) | color(Color::Green),
                    text(" "),
                    text("-" + std::to_string(deletions)) | color(Color::Red),
                });
                
                auto file_block = vbox({
                    file_header,
                    separatorLight() | color(accent(theme)),
                    hbox({ text("  "), render_diff_content(content) | flex }),
                    text(""),
                }) | borderLight | color(accent(theme));
                
                file_blocks.push_back(file_block);
                file_blocks.push_back(text(""));
            }
            
            LOG_DEBUG("Views: files tab render, auto_scroll={}, scroll_offset={}", state.auto_scroll, *scroll_offset);
            body = vbox({
                text(" MODIFIED FILES ") | bold | color(accent2(theme)) | hcenter,
                text(""),
                vbox(std::move(file_blocks)) | vscroll_indicator | (state.auto_scroll ? focusPositionRelative(0.f, 1.f) : focusPosition(0, *scroll_offset)) | yframe | yflex,
            }) | flex;
        }
    }
    // ── Tab 2: Stats ──
    else {
        int hard_limit = 200000;
        int used = *state.total_tokens;
        double used_pct = (double)used / hard_limit * 100.0;
        if (used_pct > 100.0) used_pct = 100.0;

        int bar_width = 40;
        int filled = (int)(used_pct / 100.0 * bar_width);
        std::string filled_bar = "";
        for (int i = 0; i < filled; ++i) filled_bar += "█";
        std::string empty_bar = "";
        for (int i = 0; i < bar_width - filled; ++i) empty_bar += "░";

        double cost = 0.0;
        std::string prov_id = providers_list[selected_provider].id;
        if (prov_id == "openrouter") {
            cost = (*state.total_prompt_tokens * 2.50 + *state.total_completion_tokens * 10.00) / 1000000.0;
        } else {
            cost = (*state.total_prompt_tokens * 3.00 + *state.total_completion_tokens * 15.00) / 1000000.0;
        }

        body = vbox({
            text(""),
            hbox({
                text("  "),
                vbox({
                    text("⎔ CONTEXT WINDOW") | bold | color(accent2(theme)),
                    hbox({
                        text("Model/Provider: ") | dim,
                        text(providers_list[selected_provider].name + " / " + providers_list[selected_provider].models[selected_model].name) | bold
                    }),
                    hbox({
                        text("Usage: ") | dim,
                        text(std::to_string(used) + " / " + std::to_string(hard_limit) + " tokens (" + std::to_string((int)used_pct) + "%)")
                    }),
                    hbox({
                        text("[") | dim,
                        text(filled_bar) | color(used_pct >= 90 ? Color::Red : (used_pct >= 70 ? Color::Yellow : accent2(theme))),
                        text(empty_bar) | dim,
                        text("]") | dim
                    }),
                    separatorLight() | color(accent(theme)),
                    text("⎔ SESSION STATS") | bold | color(accent2(theme)),
                    hbox({ text("Prompt Tokens: ") | dim, text(std::to_string(*state.total_prompt_tokens)) }),
                    hbox({ text("Completion Tokens: ") | dim, text(std::to_string(*state.total_completion_tokens)) }),
                    hbox({ text("Total Tokens: ") | dim, text(std::to_string(*state.total_tokens)) }),
                    hbox({ text("Estimated Cost: ") | dim, text("$" + std::to_string(cost)) | color(Color::Green) | bold }),
                }) | flex,
                text("  ")
            }),
            text("")
        }) | border | color(accent(theme)) | size(WIDTH, LESS_THAN, 80) | hcenter | flex;
    }

    auto main_layout = vbox({
        header,
        body,
    }) | flex;

    // Overlay popups using dbox (stacked overlay layout) to match fluidity of opencode
    if (show_model_select) {
        return dbox({
            main_layout,
            clear_under(build_model_popup(model_entries, model_select_idx, selected_provider, selected_model, theme)) | center
        });
    }
    if (show_session_select) {
        return dbox({
            main_layout,
            clear_under(build_session_popup(session_entries, session_select_idx, *state.session_id, theme)) | center
        });
    }

    return main_layout;
}

// ── Model selector popup ──
ftxui::Element build_model_popup(
    const std::vector<ModelEntry>& entries,
    int select_idx,
    int selected_provider,
    int selected_model,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Select Model") | bold | color(accent2(theme)));
    lines.push_back(separatorLight());

    std::string last_cat;
    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
        auto& e = entries[i];
        if (e.category != last_cat) {
            if (!last_cat.empty()) lines.push_back(text(""));
            lines.push_back(text(" " + e.category) | bold | color(accent2(theme)));
            last_cat = e.category;
        }

        bool active = (e.provider_idx == selected_provider && e.model_idx == selected_model);
        std::string marker = (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");
        std::string line_text = marker + e.model_name;
        if (e.model_id != e.model_name)
            line_text += "  " + e.model_id;

        auto line = text(line_text);
        if (i == select_idx)
            line = line | bgcolor(bg_popup()) | bold;
        else if (active)
            line = line | color(accent2(theme));
        lines.push_back(line);
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 72) | hcenter;
}

// ── Session selector popup ──
ftxui::Element build_session_popup(
    const std::vector<std::pair<std::string, std::string>>& entries,
    int select_idx,
    const std::string& active_session_id,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Select Session") | bold | color(accent2(theme)));
    lines.push_back(separatorLight());

    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
        auto& e = entries[i];
        bool active = (e.first == active_session_id);
        std::string marker = (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");
        std::string line_text = marker + e.second;
        if (e.first != e.second)
            line_text += "  (" + e.first.substr(0, 8) + "...)";

        auto line = text(line_text);
        if (i == select_idx)
            line = line | bgcolor(bg_popup()) | bold;
        else if (active)
            line = line | color(accent2(theme));
        lines.push_back(line);
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 72) | hcenter;
}

} // namespace tui
} // namespace ai
