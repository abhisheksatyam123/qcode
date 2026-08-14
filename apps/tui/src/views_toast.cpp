#include <views.h>

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
        std::string icon;
        if (t.variant == "error") {
            badge_bg = theme_error(theme);
            icon = " ✖ ";
        } else if (t.variant == "success") {
            badge_bg = theme_success(theme);
            icon = " ✔ ";
        } else if (t.variant == "warning") {
            badge_bg = Color::RGB(0xFA, 0xBC, 0x3F);
            icon = " ⚠ ";
        } else {
            badge_bg = accent(theme);
            badge_fg = Color::White;
            icon = " ℹ ";
        }
        Elements toast_row;
        toast_row.push_back(text(icon) | bold | bgcolor(badge_bg) | color(badge_fg));
        toast_row.push_back(text(" " + t.message + " ") | color(Color::White) | bold);
        toast_elems.push_back(
            hbox(std::move(toast_row)) | bgcolor(bg_popup()) | borderRounded | color(badge_bg)
        );
    }
    
    return vbox(std::move(toast_elems)) | size(WIDTH, LESS_THAN, 76) | hcenter;
}


}  // namespace tui
}  // namespace qcode
