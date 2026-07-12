#pragma once

#include <ftxui/screen/color.hpp>
#include <string>
#include <vector>

namespace ai {
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
    return ftxui::Color::RGB(0x00, 0xCE, 0xCB);  // Bright Teal (complements Classic Orange)
}

inline ftxui::Color user_green() { return ftxui::Color::RGB(0x22, 0xBB, 0x88); }
inline ftxui::Color dim_gray() { return ftxui::Color::GrayDark; }
inline ftxui::Color bg_popup() { return ftxui::Color::RGB(0x22, 0x22, 0x22); }

inline bool is_known_theme(const std::string& theme) {
    return theme == "orange" || theme == "green" || theme == "blue" ||
           theme == "purple" || theme == "monochrome" || theme == "pastel" ||
           theme == "mint" || theme == "lavender" || theme == "peach" ||
           theme == "sky" || theme == "rose" || theme == "butter" ||
           theme == "coral" || theme == "lilac" || theme == "sage";
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
    };
}

}  // namespace tui
}  // namespace ai
