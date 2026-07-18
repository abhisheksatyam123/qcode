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
        Color bg;
        Color fg = Color::White;
        std::string icon;
        if (t.variant == "error") {
            bg = Color::RGB(0xCC, 0x33, 0x33);
            icon = " ✗ ";
        } else if (t.variant == "success") {
            bg = Color::RGB(0x22, 0xBB, 0x88);
            icon = " ✓ ";
        } else if (t.variant == "warning") {
            bg = Color::RGB(0xEE, 0x99, 0x22);
            icon = " ⚠ ";
        } else {
            bg = Color::RGB(0x33, 0x66, 0xCC);
            icon = " ℹ ";
        }
        Elements toast_row;
        toast_row.push_back(text(icon) | bold);
        toast_row.push_back(text(t.message));
        toast_elems.push_back(
            hbox(std::move(toast_row)) | bgcolor(bg) | color(fg)
        );
    }
    
    return vbox(std::move(toast_elems)) | size(WIDTH, LESS_THAN, 72) | hcenter;
}


}  // namespace tui
}  // namespace qcode
