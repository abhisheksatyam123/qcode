#include <views.h>
#include <sstream>
#include <algorithm>

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
        Color badge_col;
        std::string badge_icon;

        if (t.variant == "error") {
            badge_col = theme_error(theme);
            badge_icon = " ✖ ";
        } else if (t.variant == "warning") {
            badge_col = Color::RGB(0xF5, 0x9E, 0x0B); // Amber
            badge_icon = " ⚠ ";
        } else if (t.variant == "success") {
            badge_col = theme_success(theme);
            badge_icon = " ✔ ";
        } else {
            badge_col = accent(theme);
            badge_icon = " ℹ ";
        }

        // Clean leading unicode warning prefix if redundant
        std::string raw_msg = t.message;
        if (raw_msg.rfind("\u26a0 ", 0) == 0) {
            raw_msg = raw_msg.substr(std::string("\u26a0 ").length());
        }

        // Parse message and optional hint split by " · " or newline
        std::string main_text = raw_msg;
        std::string hint_text = "";
        auto sep_pos = raw_msg.find("  ·  ");
        if (sep_pos == std::string::npos) {
            sep_pos = raw_msg.find(" · ");
        }
        if (sep_pos != std::string::npos) {
            main_text = raw_msg.substr(0, sep_pos);
            hint_text = raw_msg.substr(sep_pos + (raw_msg.find("  ·  ") != std::string::npos ? 5 : 3));
        }

        Elements row_parts;
        row_parts.push_back(text(badge_icon) | bold | color(badge_col));
        row_parts.push_back(text(main_text) | color(Color::White));

        if (!hint_text.empty()) {
            row_parts.push_back(text(" · " + hint_text) | dim | color(Color::GrayLight));
        }
        row_parts.push_back(text(" "));

        auto toast_card = hbox(std::move(row_parts))
            | bgcolor(bg_popup())
            | borderLight
            | color(badge_col);

        toast_elems.push_back(toast_card);
    }
    
    return vbox(std::move(toast_elems)) | size(WIDTH, LESS_THAN, 56) | hcenter;
}

}  // namespace tui
}  // namespace qcode
