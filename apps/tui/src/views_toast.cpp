#include <views.h>
#include <sstream>

namespace qcode {
namespace tui {

// ── Toast overlay ─────────────────────────────────────────────────────────────
ftxui::Element render_toast_overlay(
    const std::vector<Toast>& toasts,
    const std::string& theme)
{
    using namespace ftxui;
    if (toasts.empty()) return emptyElement();
    
    Elements toast_elems;
    for (const auto& t : toasts) {
        Color badge_bg;
        Color badge_fg = Color::Black;
        std::string badge_label;
        if (t.variant == "error") {
            badge_bg = theme_error(theme);
            badge_fg = Color::White;
            badge_label = " ✖ ERROR ";
        } else if (t.variant == "warning") {
            badge_bg = Color::RGB(0xF5, 0x9E, 0x0B);
            badge_fg = Color::Black;
            badge_label = " ⚠ WARNING ";
        } else if (t.variant == "success") {
            badge_bg = theme_success(theme);
            badge_fg = Color::Black;
            badge_label = " ✔ SUCCESS ";
        } else {
            badge_bg = accent(theme);
            badge_fg = Color::White;
            badge_label = " ℹ INFO ";
        }

        Elements msg_lines;
        std::istringstream iss(t.message);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            msg_lines.push_back(text(line) | color(Color::White) | bold);
        }
        if (msg_lines.empty()) {
            msg_lines.push_back(text("") | color(Color::White));
        }

        Element msg_content = (msg_lines.size() == 1)
                                  ? msg_lines[0]
                                  : vbox(std::move(msg_lines));

        auto toast_card = hbox({
            text(badge_label) | bold | bgcolor(badge_bg) | color(badge_fg),
            text(" "),
            msg_content | flex,
            text(" "),
        }) | bgcolor(bg_popup()) | borderRounded | color(badge_bg);

        toast_elems.push_back(toast_card);
    }
    
    return vbox(std::move(toast_elems)) | size(WIDTH, LESS_THAN, 80) | hcenter;
}

}  // namespace tui
}  // namespace qcode
