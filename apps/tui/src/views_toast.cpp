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
        Color badge_bg;
        Color badge_fg = Color::White;
        Color border_col;
        std::string badge_icon;
        std::string badge_title;

        if (t.variant == "error") {
            badge_bg = theme_error(theme);
            badge_fg = Color::White;
            border_col = theme_error(theme);
            badge_icon = " ✖ ";
            badge_title = "ERROR";
        } else if (t.variant == "warning") {
            badge_bg = Color::RGB(0xF5, 0x9E, 0x0B); // Amber
            badge_fg = Color::Black;
            border_col = Color::RGB(0xF5, 0x9E, 0x0B);
            badge_icon = " ⚠ ";
            badge_title = "WARNING";
        } else if (t.variant == "success") {
            badge_bg = theme_success(theme);
            badge_fg = Color::Black;
            border_col = theme_success(theme);
            badge_icon = " ✔ ";
            badge_title = "SUCCESS";
        } else {
            badge_bg = accent(theme);
            badge_fg = Color::White;
            border_col = accent(theme);
            badge_icon = " ℹ ";
            badge_title = "INFO";
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

        Elements msg_rows;
        std::istringstream iss(main_text);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            msg_rows.push_back(text(line) | color(Color::White) | bold);
        }
        if (msg_rows.empty()) {
            msg_rows.push_back(text("") | color(Color::White));
        }

        Element body_element = (msg_rows.size() == 1) ? msg_rows[0] : vbox(std::move(msg_rows));

        Element header_row = hbox({
            text(badge_icon + badge_title + " ") | bold | bgcolor(badge_bg) | color(badge_fg),
            text("  "),
            body_element | flex,
        });

        Element content_box;
        if (!hint_text.empty()) {
            content_box = vbox({
                header_row,
                hbox({
                    text("           "),
                    text("↳ " + hint_text) | dim | color(Color::GrayLight),
                }),
            });
        } else {
            content_box = header_row;
        }

        // Floating popup card with left rail accent, dark background and rounded border
        auto toast_card = hbox({
            text("▌") | color(border_col) | bold,
            text(" "),
            content_box | flex,
            text(" "),
        }) | bgcolor(bg_popup()) | borderRounded | color(border_col);

        toast_elems.push_back(toast_card);
    }
    
    return vbox(std::move(toast_elems)) | size(WIDTH, LESS_THAN, 82) | hcenter;
}

}  // namespace tui
}  // namespace qcode
