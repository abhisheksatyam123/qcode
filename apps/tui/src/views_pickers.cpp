#include "views_pickers.h"

namespace qcode {
namespace tui {

using namespace ftxui;

// ── Theme selector popup ──
ftxui::Element build_theme_popup(
    const std::vector<ThemeEntry>& entries,
    int select_idx,
    const std::string& active_theme,
    const std::string& query,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Select Theme") | bold | color(accent2(theme)));
    lines.push_back(separatorLight());

    // Search bar
    lines.push_back(hbox({
        text(" Find: ") | bold | color(accent(theme)),
        text(query) | color(Color::White)
    }));
    lines.push_back(separatorLight());

    if (entries.empty()) {
        lines.push_back(text("  (no matches)") | dim);
    } else {
        for (int i = 0; i < static_cast<int>(entries.size()); i++) {
            const auto& e = entries[i];
            bool active = (e.name == active_theme);
            std::string marker = (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");
            std::string line_text = marker + e.name + " (" + e.description + ")";

            auto line = text(line_text);
            if (i == select_idx)
                line = line | bgcolor(bg_popup()) | bold;
            else if (active)
                line = line | color(accent2(theme));
            lines.push_back(line);
        }
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 72) | hcenter;
}

// ── Model selector popup ──
ftxui::Element build_model_popup(
    const std::vector<ModelEntry>& entries,
    int select_idx,
    int selected_provider,
    int selected_model,
    const std::string& query,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Select Model") | bold | color(accent2(theme)));
    lines.push_back(separatorLight());

    // Search bar
    lines.push_back(hbox({
        text(" Find: ") | bold | color(accent(theme)),
        text(query) | color(Color::White)
    }));
    lines.push_back(separatorLight());

    if (entries.empty()) {
        lines.push_back(text("  (no matches)") | dim);
    } else {
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
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 72) | hcenter;
}

// ── Session selector popup ──
ftxui::Element build_session_popup(
    const std::vector<session::SessionInfo>& entries,
    int select_idx,
    const std::string& active_session_id,
    const std::string& query,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Select Session") | bold | color(accent2(theme)));
    lines.push_back(separatorLight());

    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
        const auto& e = entries[i];
        bool active = (e.id == active_session_id);
        std::string marker = (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");
        std::string line_text = marker + e.title;
        if (e.id != e.title)
            line_text += "  (" + e.id.substr(0, 8) + "...)";
        if (!e.workspace.empty())
            line_text += "  [" + e.workspace + "]";

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


}  // namespace tui
}  // namespace qcode
