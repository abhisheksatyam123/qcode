#include <ai/tui/views.h>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>

namespace ai {
namespace tui {

// Forward declarations
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

// ── Old Q-Code logo ──
ftxui::Element render_logo() {
    using namespace ftxui;
    auto cyan  = Color::RGB(22, 184, 243);
    auto blue  = Color::RGB(72, 124, 255);
    return vbox({
        hbox({ text("        ") | color(cyan), text("                         ") | color(blue) }),
        hbox({ text("\u2588\u2580\u2580\u2588    ") | color(cyan), text("  \u2580\u2580\u2580\u2580 \u2588\u2580\u2580\u2588 \u2588\u2580\u2580\u2584 \u2588\u2580\u2580 ") | color(blue) | bold }),
        hbox({ text("\u2588  \u2588 \u2580\u2580 ") | color(cyan), text("  \u2588    \u2588  \u2588 \u2588  \u2588 \u2588\u2580\u2580 ") | color(blue) | bold }),
        hbox({ text("\u2580\u2580\u2580\u2588\u2580   ") | color(cyan), text("  \u2580\u2580\u2580\u2580 \u2580\u2580\u2580\u2580 \u2580\u2580\u2580  \u2580\u2580\u2580 ") | color(blue) | bold }),
    }) | hcenter;
}

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
    const ftxui::Component& input
) {
    using namespace ftxui;
    bool empty = state.chat_history->empty();

    if (empty) {
        auto prompt_bar = hbox({
            input->Render() | flex,
            text(" " + providers_list[selected_provider].models[selected_model].name + " ") | bold | color(accent2()),
            text(" " + providers_list[selected_provider].name + " ") | dim,
        }) | border | color(accent());

        auto content = vbox({
            filler() | flex,
            render_logo(),
            text("") | size(HEIGHT, EQUAL, 1),
            text("What can I help you build today?") | dim | hcenter,
            prompt_bar | size(WIDTH, EQUAL, 80) | hcenter,
            filler() | flex,
        });

        if (show_model_select)
            return build_model_popup(model_entries, model_select_idx, selected_provider, selected_model);
        if (show_slash)
            return build_slash_popup(slash_commands, slash_idx);
        return content;
    }

    Elements msgs;
    for (auto& entry : *state.chat_history) {
        Color c = entry.first == "User" ? user_green()
                : entry.first == "System" ? dim_gray() : accent2();
        msgs.push_back(hbox({
            text(" " + entry.first + " ") | bold | color(c),
            text(" " + entry.second),
        }));
        msgs.push_back(text(""));
    }

    auto status = *state.is_generating ? "\u25cf Generating..." : "";
    auto bottom = hbox({
        text(" " + providers_list[selected_provider].models[selected_model].name + " ") | bold | color(accent2()),
        text(" " + providers_list[selected_provider].name + " ") | dim,
        separatorLight() | color(accent()),
        text(status) | color(*state.is_generating ? Color::Green : dim_gray()),
        text("") | flex,
        input->Render() | flex,
        text(enable_tools ? "\u2699" : "") | dim,
    }) | border | color(accent());

    auto chat = vbox({
        vbox(std::move(msgs)) | vscroll_indicator | yflex | focus,
        bottom,
    });

    if (show_model_select)
        return vbox({ build_model_popup(model_entries, model_select_idx, selected_provider, selected_model), chat });
    if (show_slash)
        return vbox({ build_slash_popup(slash_commands, slash_idx), chat });
    return chat;
}

ftxui::Element build_model_popup(
    const std::vector<ModelEntry>& entries,
    int select_idx,
    int selected_provider,
    int selected_model
) {
    using namespace ftxui;
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
        if (e.model_id != e.model_name) line_text += "  " + e.model_id;
        auto line = text(line_text);
        if (i == select_idx) line = line | bgcolor(bg_popup()) | bold;
        else if (active) line = line | color(accent2());
        lines.push_back(line);
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" \u2191\u2193 navigate  Enter select  Esc cancel") | dim);
    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) | size(WIDTH, EQUAL, 72) | hcenter;
}

ftxui::Element build_slash_popup(
    const std::vector<SlashCommand>& commands,
    int slash_idx
) {
    using namespace ftxui;
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
        int pad = std::max(0, 12 - (int)cmd.name.size());
        line_text += std::string(pad, ' ');
        line_text += cmd.description;
        auto line = text(line_text);
        line = (i == slash_idx) ? (line | bgcolor(bg_popup()) | bold) : (line | dim);
        lines.push_back(line);
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" \u2191\u2193 navigate  Enter select  Esc cancel") | dim);
    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) | size(WIDTH, EQUAL, 60) | hcenter;
}

} // namespace tui
} // namespace ai
