#include <ai/tui/views.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <cstdio>

namespace ai {
namespace tui {

using namespace ftxui;

// Helper to get git diff of the selected file
static std::string get_file_diff(const std::string& path) {
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

ftxui::Element render_logo() {
    auto cyan  = Color::RGB(22, 184, 243);
    auto blue  = Color::RGB(72, 124, 255);
    return vbox({
        hbox({ text("        ") | color(cyan), text("                         ") | color(blue) }),
        hbox({ text("\u2588\u2580\u2580\u2588    ") | color(cyan), text("  \u2580\u2580\u2580\u2580 \u2588\u2580\u2580\u2588 \u2588\u2580\u2580\u2584 \u2588\u2580\u2580 ") | color(blue) | bold }),
        hbox({ text("\u2588  \u2588 \u2580\u2580 ") | color(cyan), text("  \u2588    \u2588  \u2588 \u2588  \u2588 \u2580\u2580 ") | color(blue) | bold }),
        hbox({ text("\u2580\u2580\u2580\u2588\u2580   ") | color(cyan), text("  \u2580\u2580\u2580\u2580 \u2580\u2580\u2580\u2580 \u2580\u2580\u2580  \u2580\u2580\u2580 ") | color(blue) | bold }),
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
ftxui::Element build_slash_popup(
    const std::vector<SlashCommand>& commands,
    int slash_idx,
    const std::string& theme
);

ftxui::Element render_view(
    const ChatState& state,
    const std::vector<ProviderInfo>& providers_list,
    int selected_provider,
    int selected_model,
    bool enable_tools,
    bool show_slash,
    int slash_idx,
    const std::vector<SlashCommand>& slash_commands,
    bool show_model_select,
    int model_select_idx,
    const std::vector<ModelEntry>& model_entries,
    const ftxui::Component& tab_toggle,
    const ftxui::Component& files_menu,
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
            auto prompt_bar = hbox({
                input->Render() | flex,
                text(" " + providers_list[selected_provider].models[selected_model].name + " ") | bold | color(accent2(theme)),
                text(" " + providers_list[selected_provider].name + " ") | dim,
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
                    bubble = hbox({
                        text("  \u2139  ") | bold | color(c),
                        paragraph(entry.second) | flex | dim,
                    });
                } else if (entry.first == "User") {
                    // Exact opencode User Message formatting: left border
                    bubble = hbox({
                        separatorLight() | color(user_green()),
                        vbox({
                            text("  User") | bold | color(user_green()),
                            paragraph("  " + entry.second) | flex,
                        })
                    });
                } else {
                    // Exact opencode Assistant Message formatting: left border + status footer
                    bubble = hbox({
                        separatorLight() | color(accent2(theme)),
                        vbox({
                            text("  Assistant") | bold | color(accent2(theme)),
                            paragraph("  " + entry.second) | flex,
                            text(""),
                            text("  \u25a3 Assistant \u00b7 " + providers_list[selected_provider].models[selected_model].name) | dim | color(accent2(theme)),
                        })
                    });
                }

                // Selection highlight & focus scroll targeting
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

            auto status = *state.is_generating ? "\u25cf Generating..." : "";
            if (state.selection_mode) {
                status = "\u2191\u2193 navigate  y/Enter copy  Esc exit visual selection";
            }

            auto prompt_box = vbox({
                hbox({
                    input->Render() | flex,
                }) | size(HEIGHT, GREATER_THAN, 1),
                separatorLight() | color(accent(theme)),
                hbox({
                    text(" " + providers_list[selected_provider].models[selected_model].name + " ") | bold | color(accent2(theme)),
                    text(" " + providers_list[selected_provider].name + " ") | dim,
                    text(enable_tools ? " \u2699 (tools)" : "") | dim,
                    filler(),
                    text(status) | color(*state.is_generating ? Color::Green : dim_gray()),
                })
            }) | border | color(accent(theme));

            body = vbox({
                vbox(std::move(msgs)) | vscroll_indicator | frame | yflex,
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
                // Get diff content
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
            
            body = vbox({
                text(" MODIFIED FILES ") | bold | color(accent2(theme)) | hcenter,
                text(""),
                vbox(std::move(file_blocks)) | vscroll_indicator | frame | yflex,
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

    // Overlay popups if active
    if (show_model_select) {
        return vbox({
            build_model_popup(model_entries, model_select_idx, selected_provider, selected_model, theme),
            main_layout,
        });
    }
    if (show_slash) {
        return vbox({
            build_slash_popup(slash_commands, slash_idx, theme),
            main_layout,
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
        std::string marker = (i == select_idx) ? " \u25b6 " : (active ? " \u25cf " : "   ");
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
    lines.push_back(text(" \u2191\u2193 navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 72) | hcenter;
}

// ── Slash command popup ──
ftxui::Element build_slash_popup(
    const std::vector<SlashCommand>& commands,
    int slash_idx,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Commands") | bold | color(accent2(theme)));
    lines.push_back(separatorLight());

    std::string last_cat;
    for (int i = 0; i < static_cast<int>(commands.size()); i++) {
        auto& cmd = commands[i];
        if (cmd.category != last_cat) {
            if (!last_cat.empty()) lines.push_back(text(""));
            lines.push_back(text(" " + cmd.category) | bold | color(accent2(theme)));
            last_cat = cmd.category;
        }
        std::string line_text = std::string(i == slash_idx ? " \u25b6 " : "   ") + "/" + cmd.name;
        line_text += std::string(12, ' ').substr(0, std::max(0, 12 - (int)cmd.name.size()));
        line_text += cmd.description;

        auto line = text(line_text);
        line = (i == slash_idx) ? (line | bgcolor(bg_popup()) | bold) : (line | dim);
        lines.push_back(line);
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" \u2191\u2193 navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 60) | hcenter;
}

} // namespace tui
} // namespace ai
