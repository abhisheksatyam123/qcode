#include <ai/tui/views.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ai {
namespace tui {

using namespace ftxui;

// Helper to get file preview (ported from main.cpp)
static std::string get_file_preview(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        return "File does not exist: " + path;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        return "Could not open file: " + path;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

ftxui::Element render_logo() {
    auto cyan  = Color::RGB(22, 184, 243);
    auto blue  = Color::RGB(72, 124, 255);
    return vbox({
        hbox({ text("        ") | color(cyan), text("                         ") | color(blue) }),
        hbox({ text("\u2588\u2580\u2580\u2588    ") | color(cyan), text("  \u2580\u2580\u2580\u2580 \u2588\u2580\u2580\u2588 \u2588\u2580\u2580\u2584 \u2588\u2580\u2580 ") | color(blue) | bold }),
        hbox({ text("\u2588  \u2588 \u2580\u2580 ") | color(cyan), text("  \u2588    \u2588  \u2588 \u2588  \u2588 \u2588\u2580\u2580 ") | color(blue) | bold }),
        hbox({ text("\u2580\u2580\u2580\u2588\u2580   ") | color(cyan), text("  \u2580\u2580\u2580\u2580 \u2580\u2580\u2580\u2580 \u2580\u2580\u2580  \u2580\u2580\u2580 ") | color(blue) | bold }),
    }) | hcenter;
}

// Forward declarations of popups
ftxui::Element build_model_popup(
    const std::vector<ModelEntry>& entries,
    int select_idx,
    int selected_provider,
    int selected_model
);
ftxui::Element build_slash_popup(
    const std::vector<SlashCommand>& commands,
    int slash_idx
);
ftxui::Element build_confirm_popup(const std::string& message);

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
    // ── Outer Layout — header (tab selection) + body + footer ──
    auto header = hbox({
        text(" QCODE ") | bold | bgcolor(accent()) | color(Color::White),
        text("  "),
        tab_toggle->Render() | flex,
    }) | borderLight | color(accent());

    Element body;

    // ── Tab 0: Chat ──
    if (state.tab_selected == 0) {
        bool empty = state.chat_history->empty();

        if (empty) {
            // Home screen
            auto prompt_bar = hbox({
                input->Render() | flex,
                text(" " + providers_list[selected_provider].models[selected_model].name + " ") | bold | color(accent2()),
                text(" " + providers_list[selected_provider].name + " ") | dim,
            }) | border | color(accent());

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
                        : entry.first == "System" ? dim_gray() : accent2();
                
                auto bubble = hbox({
                    text(" " + entry.first + " ") | bold | color(c),
                    paragraph(" " + entry.second) | flex,
                });

                // Selection highlight
                if (state.selection_mode && state.selected_message == i) {
                    bubble = bubble | bgcolor(Color::RGB(0x33, 0x33, 0x33)) | bold;
                }

                msgs.push_back(bubble);
                msgs.push_back(text(""));
            }

            auto status = *state.is_generating ? "\u25cf Generating..." : "";
            if (state.selection_mode) {
                status = "\u2191\u2193 navigate  y/Enter copy  Esc exit visual selection";
            }

            auto bottom = hbox({
                text(" " + providers_list[selected_provider].models[selected_model].name + " ") | bold | color(accent2()),
                text(" " + providers_list[selected_provider].name + " ") | dim,
                separatorLight() | color(accent()),
                text(status) | color(*state.is_generating ? Color::Green : dim_gray()),
                text("") | flex,
                input->Render() | flex,
                text(enable_tools ? "\u2699" : "") | dim,
            }) | border | color(accent());

            body = vbox({
                vbox(std::move(msgs)) | vscroll_indicator | yflex | focus,
                bottom,
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
            std::string selected_path = "";
            if (state.selected_file >= 0 && state.selected_file < static_cast<int>(state.modified_files->size())) {
                selected_path = (*state.modified_files)[state.selected_file];
            }

            std::string content = get_file_preview(selected_path);

            auto file_list_panel = vbox({
                text(" MODIFIED FILES ") | bold | color(accent2()),
                separatorLight() | color(accent()),
                files_menu->Render() | frame
            }) | size(WIDTH, EQUAL, 30);

            auto file_preview_panel = vbox({
                text(" PREVIEW: " + selected_path) | bold | color(accent2()),
                separatorLight() | color(accent()),
                text(""),
                hbox({
                    text("  "),
                    paragraph(content) | vscroll_indicator | frame | flex,
                    text("  ")
                }),
                text("")
            }) | flex;

            body = hbox({
                file_list_panel,
                separator() | color(accent()),
                file_preview_panel
            }) | border | color(accent()) | flex;
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
                    text("⎔ CONTEXT WINDOW") | bold | color(accent2()),
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
                        text(filled_bar) | color(used_pct >= 90 ? Color::Red : (used_pct >= 70 ? Color::Yellow : accent2())),
                        text(empty_bar) | dim,
                        text("]") | dim
                    }),
                    separatorLight() | color(accent()),
                    text("⎔ SESSION STATS") | bold | color(accent2()),
                    hbox({ text("Prompt Tokens: ") | dim, text(std::to_string(*state.total_prompt_tokens)) }),
                    hbox({ text("Completion Tokens: ") | dim, text(std::to_string(*state.total_completion_tokens)) }),
                    hbox({ text("Total Tokens: ") | dim, text(std::to_string(*state.total_tokens)) }),
                    hbox({ text("Estimated Cost: ") | dim, text("$" + std::to_string(cost)) | color(Color::Green) | bold }),
                }) | flex,
                text("  ")
            }),
            text("")
        }) | border | color(accent()) | size(WIDTH, LESS_THAN, 80) | hcenter | flex;
    }

    auto main_layout = vbox({
        header,
        body,
    }) | flex;

    // Overlay popups if active
    if (*state.show_confirm_dialog) {
        return vbox({
            build_confirm_popup(*state.confirm_dialog_message),
            main_layout,
        });
    }
    if (show_model_select) {
        return vbox({
            build_model_popup(model_entries, model_select_idx, selected_provider, selected_model),
            main_layout,
        });
    }
    if (show_slash) {
        return vbox({
            build_slash_popup(slash_commands, slash_idx),
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
    int selected_model
) {
    Elements lines;
    lines.push_back(text(" Select Model") | bold | color(accent2()));
    lines.push_back(separatorLight());

    std::string last_cat;
    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
        auto& e = entries[i];
        if (e.category != last_cat) {
            if (!last_cat.empty()) lines.push_back(text(""));
            lines.push_back(text(" " + e.category) | bold | color(accent2()));
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
            line = line | color(accent2());
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
    int slash_idx
) {
    Elements lines;
    lines.push_back(text(" Commands") | bold | color(accent2()));
    lines.push_back(separatorLight());

    std::string last_cat;
    for (int i = 0; i < static_cast<int>(commands.size()); i++) {
        auto& cmd = commands[i];
        if (cmd.category != last_cat) {
            if (!last_cat.empty()) lines.push_back(text(""));
            lines.push_back(text(" " + cmd.category) | bold | color(accent2()));
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

namespace ai {
namespace tui {

ftxui::Element build_confirm_popup(const std::string& message) {
    using namespace ftxui;
    Elements lines;
    lines.push_back(text(" Permission Requested") | bold | color(Color::Yellow));
    lines.push_back(separatorLight() | color(Color::Yellow));
    
    // Support multi-line message wrapping
    std::stringstream ss(message);
    std::string line_str;
    while (std::getline(ss, line_str, '\n')) {
        lines.push_back(paragraph(line_str));
    }
    
    lines.push_back(separatorLight() | color(Color::Yellow));
    lines.push_back(hbox({
        text("  [Y] Approve  ") | bold | bgcolor(Color::Green) | color(Color::Black),
        text("    "),
        text("  [N] Deny  ") | bold | bgcolor(Color::Red) | color(Color::Black),
    }) | hcenter);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 60) | hcenter;
}

} // namespace tui
} // namespace ai
