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
    if (theme == "green") return ftxui::Color::RGB(0x22, 0xBB, 0x88);
    if (theme == "blue") return ftxui::Color::RGB(0x16, 0xB8, 0xF3);
    if (theme == "purple") return ftxui::Color::RGB(0xD8, 0x50, 0xE0);
    if (theme == "monochrome") return ftxui::Color::RGB(0xAA, 0xAA, 0xAA);
    // Pastels
    if (theme == "pastel") return ftxui::Color::RGB(0xF0, 0xA0, 0xC0);
    if (theme == "mint") return ftxui::Color::RGB(0x7E, 0xD9, 0xB8);
    if (theme == "lavender") return ftxui::Color::RGB(0xC4, 0xA8, 0xE8);
    if (theme == "peach") return ftxui::Color::RGB(0xF0, 0xB0, 0x90);
    if (theme == "sky") return ftxui::Color::RGB(0x8E, 0xC8, 0xF0);
    if (theme == "rose") return ftxui::Color::RGB(0xE8, 0x90, 0xA8);
    if (theme == "butter") return ftxui::Color::RGB(0xE8, 0xD0, 0x80);
    if (theme == "coral") return ftxui::Color::RGB(0xF0, 0x90, 0x80);
    if (theme == "lilac") return ftxui::Color::RGB(0xC9, 0xB1, 0xFF);
    if (theme == "sage") return ftxui::Color::RGB(0xA8, 0xC9, 0x98);
    return ftxui::Color::RGB(0xEC, 0x5B, 0x2B);  // orange (default)
}

inline ftxui::Color accent2(const std::string& theme = "orange") {
    if (theme == "green") return ftxui::Color::RGB(0x44, 0xDD, 0xAA);
    if (theme == "blue") return ftxui::Color::RGB(0x48, 0x7C, 0xFF);
    if (theme == "purple") return ftxui::Color::RGB(0xEE, 0x80, 0xF8);
    if (theme == "monochrome") return ftxui::Color::RGB(0xDD, 0xDD, 0xDD);
    // Pastels (lighter companion)
    if (theme == "pastel") return ftxui::Color::RGB(0xF8, 0xC0, 0xD8);
    if (theme == "mint") return ftxui::Color::RGB(0xA8, 0xE8, 0xD0);
    if (theme == "lavender") return ftxui::Color::RGB(0xDC, 0xC8, 0xF5);
    if (theme == "peach") return ftxui::Color::RGB(0xFF, 0xD0, 0xB0);
    if (theme == "sky") return ftxui::Color::RGB(0xB0, 0xDC, 0xF8);
    if (theme == "rose") return ftxui::Color::RGB(0xF5, 0xB8, 0xC8);
    if (theme == "butter") return ftxui::Color::RGB(0xF5, 0xE8, 0xA8);
    if (theme == "coral") return ftxui::Color::RGB(0xFF, 0xB8, 0xA8);
    if (theme == "lilac") return ftxui::Color::RGB(0xE0, 0xD0, 0xFF);
    if (theme == "sage") return ftxui::Color::RGB(0xC8, 0xE0, 0xB8);
    return ftxui::Color::RGB(0xEE, 0x79, 0x48);
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
