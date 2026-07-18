#pragma once

#include <ftxui/screen/color.hpp>
#include <string>
#include <vector>

namespace qcode {
namespace tui {

struct ThemeEntry {
    std::string name;
    std::string description;
};

// Colour palette — classic + soft pastel accents (readable on dark terminals).
inline ftxui::Color accent(const std::string& theme = "orange") {
    if (theme == "green") return ftxui::Color::RGB(0x1B, 0x8A, 0x5A);     // Forest Green
    if (theme == "blue") return ftxui::Color::RGB(0x1A, 0x73, 0xE8);      // Electric Blue
    if (theme == "purple") return ftxui::Color::RGB(0x9B, 0x59, 0xB6);    // Cyberpunk Purple
    if (theme == "monochrome") return ftxui::Color::RGB(0x70, 0x80, 0x90);// Slate Gray
    // Pastels
    if (theme == "pastel") return ftxui::Color::RGB(0xF0, 0xA0, 0xC0);    // Pastel Rose
    if (theme == "mint") return ftxui::Color::RGB(0x7E, 0xD9, 0xB8);      // Mint Green
    if (theme == "lavender") return ftxui::Color::RGB(0xC4, 0xA8, 0xE8);  // Lavender
    if (theme == "peach") return ftxui::Color::RGB(0xF0, 0xB0, 0x90);     // Peach
    if (theme == "sky") return ftxui::Color::RGB(0x8E, 0xC8, 0xF0);       // Sky Blue
    if (theme == "rose") return ftxui::Color::RGB(0xE8, 0x90, 0xA8);      // Rose Pink
    if (theme == "butter") return ftxui::Color::RGB(0xE8, 0xD0, 0x80);     // Butter Yellow
    if (theme == "coral") return ftxui::Color::RGB(0xF0, 0x90, 0x80);      // Coral Orange
    if (theme == "lilac") return ftxui::Color::RGB(0xC9, 0xB1, 0xFF);      // Lilac
    if (theme == "sage") return ftxui::Color::RGB(0xA8, 0xC9, 0x98);      // Sage Green
    if (theme == "dracula") return ftxui::Color::RGB(0xBD, 0x93, 0xF9);   // Dracula Purple
    if (theme == "retro") return ftxui::Color::RGB(0x33, 0xFF, 0x33);     // Retro Phosphor Green
    return ftxui::Color::RGB(0xEC, 0x5B, 0x2B);  // Orange
}

inline ftxui::Color accent2(const std::string& theme = "orange") {
    if (theme == "green") return ftxui::Color::RGB(0xFA, 0xBC, 0x3F);     // Amber Gold (complements Forest Green)
    if (theme == "blue") return ftxui::Color::RGB(0xFF, 0x8A, 0x65);      // Soft Coral Peach (complements Blue)
    if (theme == "purple") return ftxui::Color::RGB(0x2E, 0xEC, 0xA8);    // Bright Mint Cyan (complements Purple)
    if (theme == "monochrome") return ftxui::Color::RGB(0xF5, 0xF5, 0xF5);// Warm Ivory
    // Pastels (complements)
    if (theme == "pastel") return ftxui::Color::RGB(0xC3, 0xE2, 0xC4);    // Pale Sage (complements Pastel Rose)
    if (theme == "mint") return ftxui::Color::RGB(0xE8, 0xA5, 0xC4);      // Soft Mauve (complements Mint)
    if (theme == "lavender") return ftxui::Color::RGB(0xF5, 0xD0, 0xA9);  // Soft Apricot (complements Lavender)
    if (theme == "peach") return ftxui::Color::RGB(0x90, 0xC0, 0xD0);     // Soft Teal/Sky (complements Peach)
    if (theme == "sky") return ftxui::Color::RGB(0xF0, 0xD8, 0xA0);       // Warm Butter (complements Sky Blue)
    if (theme == "rose") return ftxui::Color::RGB(0xB0, 0xD0, 0xA0);      // Soft Moss Green (complements Rose)
    if (theme == "butter") return ftxui::Color::RGB(0xA0, 0xB0, 0xE8);     // Soft Lilac/Blue (complements Butter)
    if (theme == "coral") return ftxui::Color::RGB(0x80, 0xE0, 0xC8);      // Cool Seafoam (complements Coral)
    if (theme == "lilac") return ftxui::Color::RGB(0xA8, 0xD0, 0xB0);      // Pale Sage (complements Lilac)
    if (theme == "sage") return ftxui::Color::RGB(0xE8, 0xA0, 0xB5);      // Dusty Pink (complements Sage)
    if (theme == "dracula") return ftxui::Color::RGB(0xFF, 0x79, 0xC6);   // Dracula Pink (complements Purple)
    if (theme == "retro") return ftxui::Color::RGB(0xFF, 0xB0, 0x00);     // Retro CRT Amber (complements Green)
    return ftxui::Color::RGB(0x00, 0xCE, 0xCB);  // Bright Teal (complements Classic Orange)
}

inline ftxui::Color user_green() { return ftxui::Color::RGB(0x22, 0xBB, 0x88); }
inline ftxui::Color dim_gray() { return ftxui::Color::GrayDark; }
inline ftxui::Color bg_popup() { return ftxui::Color::RGB(0x22, 0x22, 0x22); }

inline ftxui::Color theme_prompt(const std::string& theme) {
    return accent2(theme);
}
inline ftxui::Color theme_success(const std::string& theme) {
    if (theme == "sage" || theme == "mint") return ftxui::Color::RGB(0xA2, 0xE8, 0xC4);
    if (theme == "dracula") return ftxui::Color::RGB(0x50, 0xFA, 0x7B);   // Dracula Green
    if (theme == "retro") return ftxui::Color::RGB(0x33, 0xFF, 0x33);     // Phosphor Green
    return ftxui::Color::RGB(0x7F, 0xDB, 0x8C);
}
inline ftxui::Color theme_error(const std::string& theme) {
    if (theme == "coral" || theme == "peach") return ftxui::Color::RGB(0xFF, 0x76, 0x76);
    if (theme == "dracula") return ftxui::Color::RGB(0xFF, 0x55, 0x55);   // Dracula Red
    if (theme == "retro") return ftxui::Color::RGB(0xFF, 0x33, 0x33);     // Retro Amber Red
    return ftxui::Color::RGB(0xF4, 0x87, 0x71);
}
inline ftxui::Color theme_muted(const std::string& theme) {
    return ftxui::Color::RGB(0x8A, 0x8A, 0x8A);
}
inline ftxui::Color theme_panel_bg(const std::string& theme) {
    if (theme == "green") return ftxui::Color::RGB(0x13, 0x1E, 0x18);
    if (theme == "blue") return ftxui::Color::RGB(0x12, 0x18, 0x24);
    if (theme == "purple") return ftxui::Color::RGB(0x1A, 0x14, 0x22);
    if (theme == "monochrome") return ftxui::Color::RGB(0x1A, 0x1A, 0x1A);
    if (theme == "pastel") return ftxui::Color::RGB(0x22, 0x1A, 0x1E);
    if (theme == "mint") return ftxui::Color::RGB(0x14, 0x20, 0x1C);
    if (theme == "lavender") return ftxui::Color::RGB(0x18, 0x15, 0x20);
    if (theme == "peach") return ftxui::Color::RGB(0x20, 0x18, 0x14);
    if (theme == "sky") return ftxui::Color::RGB(0x14, 0x1A, 0x20);
    if (theme == "rose") return ftxui::Color::RGB(0x20, 0x14, 0x18);
    if (theme == "butter") return ftxui::Color::RGB(0x20, 0x1E, 0x14);
    if (theme == "coral") return ftxui::Color::RGB(0x22, 0x18, 0x14);
    if (theme == "lilac") return ftxui::Color::RGB(0x18, 0x14, 0x22);
    if (theme == "sage") return ftxui::Color::RGB(0x16, 0x1C, 0x14);
    if (theme == "dracula") return ftxui::Color::RGB(0x28, 0x2A, 0x36);   // Dracula Dark
    if (theme == "retro") return ftxui::Color::RGB(0x00, 0x0A, 0x00);     // Pitch Dark Green
    return ftxui::Color::RGB(0x20, 0x16, 0x12);
}
inline ftxui::Color theme_focus_bg(const std::string& theme) {
    if (theme == "green") return ftxui::Color::RGB(0x1A, 0x2E, 0x22);
    if (theme == "blue") return ftxui::Color::RGB(0x1D, 0x2A, 0x3D);
    if (theme == "purple") return ftxui::Color::RGB(0x2B, 0x1C, 0x37);
    if (theme == "monochrome") return ftxui::Color::RGB(0x2A, 0x2A, 0x2A);
    if (theme == "pastel") return ftxui::Color::RGB(0x35, 0x22, 0x2B);
    if (theme == "mint") return ftxui::Color::RGB(0x1D, 0x30, 0x28);
    if (theme == "lavender") return ftxui::Color::RGB(0x26, 0x1F, 0x30);
    if (theme == "peach") return ftxui::Color::RGB(0x32, 0x22, 0x1B);
    if (theme == "sky") return ftxui::Color::RGB(0x1D, 0x29, 0x35);
    if (theme == "rose") return ftxui::Color::RGB(0x32, 0x1F, 0x24);
    if (theme == "butter") return ftxui::Color::RGB(0x30, 0x2A, 0x1B);
    if (theme == "coral") return ftxui::Color::RGB(0x35, 0x22, 0x1B);
    if (theme == "lilac") return ftxui::Color::RGB(0x26, 0x1F, 0x35);
    if (theme == "sage") return ftxui::Color::RGB(0x22, 0x2C, 0x1E);
    if (theme == "dracula") return ftxui::Color::RGB(0x44, 0x47, 0x5A);   // Dracula Current Line
    if (theme == "retro") return ftxui::Color::RGB(0x00, 0x2A, 0x00);     // CRT Phosphor Highlight
    return ftxui::Color::RGB(0x32, 0x20, 0x18);
}


inline bool is_known_theme(const std::string& theme) {
    return theme == "orange" || theme == "green" || theme == "blue" ||
           theme == "purple" || theme == "monochrome" || theme == "pastel" ||
           theme == "mint" || theme == "lavender" || theme == "peach" ||
           theme == "sky" || theme == "rose" || theme == "butter" ||
           theme == "coral" || theme == "lilac" || theme == "sage" ||
           theme == "dracula" || theme == "retro";
}

inline std::vector<ThemeEntry> builtin_theme_entries() {
    return {
        {"orange", "Classic Orange"},
        {"green", "Forest Green"},
        {"blue", "Deep Blue"},
        {"purple", "Cyberpunk Purple"},
        {"monochrome", "Monochrome"},
        {"pastel", "Pastel Blush Pink"},
        {"mint", "Pastel Mint"},
        {"lavender", "Pastel Lavender"},
        {"peach", "Pastel Peach"},
        {"sky", "Pastel Sky"},
        {"rose", "Pastel Rose"},
        {"butter", "Pastel Butter"},
        {"coral", "Pastel Coral"},
        {"lilac", "Pastel Lilac"},
        {"sage", "Pastel Sage"},
        {"dracula", "Dracula Dark"},
        {"retro", "Retro Terminal"},
    };
}

}  // namespace tui
}  // namespace qcode
