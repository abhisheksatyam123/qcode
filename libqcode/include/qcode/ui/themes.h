#pragma once

#include <ftxui/screen/color.hpp>
#include <string>
#include <vector>

namespace qcode {

struct ThemeEntry {
    std::string name;
    std::string description;
};

// Data-driven palette ported from opencode's TUI theme system
// (packages/tui/src/theme/assets/*.json). Each entry carries the roles qcode
// renders with; legacy built-in names keep their hand-tuned values below.
struct ThemePalette {
    const char* name;
    unsigned accent;      // primary
    unsigned accent2;     // secondary
    unsigned success;
    unsigned error;
    unsigned muted;
    unsigned panel_bg;    // backgroundPanel
    unsigned focus_bg;    // backgroundElement
    unsigned warning;
};

inline const ThemePalette* find_theme_palette(const std::string& theme) {
    static const ThemePalette kPalettes[] = {
        // ── Upstream opencode themes (dark variants) ──
        {"opencode",   0xFAB283u, 0x5C9CF5u, 0x7FD88Fu, 0xE06C75u, 0x808080u, 0x141414u, 0x1E1E1Eu, 0xF5A742u},
        {"tokyonight", 0x82AAFFu, 0xC099FFu, 0xC3E88Du, 0xFF757Fu, 0x828BB8u, 0x1E2030u, 0x222436u, 0xFF966Cu},
        {"nord",       0x88C0D0u, 0x81A1C1u, 0xA3BE8Cu, 0xBF616Au, 0x8B95A7u, 0x3B4252u, 0x434C5Eu, 0xD08770u},
        {"gruvbox",    0x83A598u, 0xD3869Bu, 0xB8BB26u, 0xFB4934u, 0x928374u, 0x3C3836u, 0x504945u, 0xFE8019u},
        {"catppuccin", 0x89B4FAu, 0xCBA6F7u, 0xA6E3A1u, 0xF38BA8u, 0x9399B2u, 0x181825u, 0x11111Bu, 0xF9E2AFu},
        {"rosepine",   0x9CCFD8u, 0xC4A7E7u, 0x31748Fu, 0xEB6F92u, 0x6E6A86u, 0x1F1D2Eu, 0x26233Au, 0xF6C177u},
        {"vesper",     0xFFC799u, 0x99FFE4u, 0x99FFE4u, 0xFF8080u, 0xA0A0A0u, 0x101010u, 0x101010u, 0xFFC799u},
        {"one-dark",   0x61AFEFu, 0xC678DDu, 0x98C379u, 0xE06C75u, 0x5C6370u, 0x21252Bu, 0x353B45u, 0xE5C07Bu},
        {"aura",       0xA277FFu, 0xF694FFu, 0x61FFCAu, 0xFF6767u, 0x6D6D6Du, 0x15141Bu, 0x15141Bu, 0xFFCA85u},
        {"zenburn",    0x8CD0D3u, 0xDC8CC3u, 0x7F9F7Fu, 0xCC9393u, 0x9F9F9Fu, 0x4F4F4Fu, 0x5F5F5Fu, 0xF0DFAFu},
        {"cobalt2",    0x0088FFu, 0x9A5FEBu, 0x9EFF80u, 0xFF0088u, 0xADB7C9u, 0x122738u, 0x1F4662u, 0xFFC600u},
        {"synthwave84",0x36F9F6u, 0xFF7EDBu, 0x72F1B8u, 0xFE4450u, 0x848BBDu, 0x1E1A29u, 0x2A2139u, 0xFEDE5Du},
        {"osaka-jade", 0x2DD5B7u, 0xD2689Cu, 0x549E6Au, 0xFF5345u, 0x53685Bu, 0x1A2520u, 0x23372Bu, 0xE5C736u},
        {"matrix",     0x2EFF6Au, 0x00EFFFu, 0x62FF94u, 0xFF4B4Bu, 0x8CA391u, 0x0E130Du, 0x141C12u, 0xE6FF57u},
        {"flexoki",    0xDA702Cu, 0x4385BEu, 0x879A39u, 0xD14D41u, 0x6F6E69u, 0x1C1B1Au, 0x282726u, 0xDA702Cu},
        {"material",   0x82AAFFu, 0xC792EAu, 0xC3E88Du, 0xF07178u, 0x546E7Au, 0x1E272Cu, 0x37474Fu, 0xFFCB6Bu},
        {"ayu",        0x59C2FFu, 0xD2A6FFu, 0x7FD962u, 0xD95757u, 0x565B66u, 0x0F131Au, 0x0D1017u, 0xE6B673u},
        {"everforest", 0xA7C080u, 0x7FBBB3u, 0xA7C080u, 0xE67E80u, 0x7A8478u, 0x333C43u, 0x343F44u, 0xE69875u},
        {"kanagawa",   0x7E9CD8u, 0x957FB8u, 0x98BB6Cu, 0xE82424u, 0x727169u, 0x2A2A37u, 0x363646u, 0xD7A657u},
        {"monokai",    0x66D9EFu, 0xAE81FFu, 0xA6E22Eu, 0xF92672u, 0x75715Eu, 0x1E1F1Cu, 0x3E3D32u, 0xE6DB74u},
        {"github",     0x58A6FFu, 0xBC8CFFu, 0x3FB950u, 0xF85149u, 0x8B949Eu, 0x010409u, 0x161B22u, 0xE3B341u},
        {"solarized",  0x268BD2u, 0x6C71C4u, 0x859900u, 0xDC322Fu, 0x586E75u, 0x073642u, 0x073642u, 0xB58900u},
        {"dracula",    0xBD93F9u, 0xFF79C6u, 0x50FA7Bu, 0xFF5555u, 0x6272A4u, 0x21222Cu, 0x44475Au, 0xF1FA8Cu},
    };
    for (const auto& p : kPalettes) {
        if (theme == p.name) return &p;
    }
    return nullptr;
}

inline ftxui::Color from_hex(unsigned rgb) {
    return ftxui::Color::RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Colour palette — classic + soft pastel accents (readable on dark terminals).
inline ftxui::Color accent(const std::string& theme = "orange") {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->accent);
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
    if (theme == "retro") return ftxui::Color::RGB(0x33, 0xFF, 0x33);     // Retro Phosphor Green
    return ftxui::Color::RGB(0xEC, 0x5B, 0x2B);  // Orange
}

inline ftxui::Color accent2(const std::string& theme = "orange") {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->accent2);
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
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->success);
    if (theme == "sage" || theme == "mint") return ftxui::Color::RGB(0xA2, 0xE8, 0xC4);
    if (theme == "retro") return ftxui::Color::RGB(0x33, 0xFF, 0x33);     // Phosphor Green
    return ftxui::Color::RGB(0x7F, 0xDB, 0x8C);
}
inline ftxui::Color theme_error(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->error);
    if (theme == "coral" || theme == "peach") return ftxui::Color::RGB(0xFF, 0x76, 0x76);
    if (theme == "retro") return ftxui::Color::RGB(0xFF, 0x33, 0x33);     // Retro Amber Red
    return ftxui::Color::RGB(0xF4, 0x87, 0x71);
}
inline ftxui::Color theme_muted(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->muted);
    if (theme == "green") return ftxui::Color::RGB(0x6B, 0x8E, 0x7B);
    if (theme == "blue") return ftxui::Color::RGB(0x6A, 0x82, 0xA5);
    if (theme == "purple") return ftxui::Color::RGB(0x8A, 0x76, 0x9B);
    if (theme == "retro") return ftxui::Color::RGB(0x22, 0x88, 0x22);
    return ftxui::Color::RGB(0x8A, 0x8A, 0x8A);
}
inline ftxui::Color theme_warning(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->warning);
    return ftxui::Color::RGB(0xE8, 0xB8, 0x4A);
}
inline ftxui::Color theme_panel_bg(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->panel_bg);
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
    if (theme == "retro") return ftxui::Color::RGB(0x00, 0x0A, 0x00);     // Pitch Dark Green
    return ftxui::Color::RGB(0x20, 0x16, 0x12);
}
inline ftxui::Color theme_focus_bg(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->focus_bg);
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
    if (theme == "retro") return ftxui::Color::RGB(0x00, 0x2A, 0x00);     // CRT Phosphor Highlight
    return ftxui::Color::RGB(0x32, 0x20, 0x18);
}

inline bool is_known_theme(const std::string& theme) {
    if (find_theme_palette(theme) != nullptr) return true;
    return theme == "orange" || theme == "green" || theme == "blue" ||
           theme == "purple" || theme == "monochrome" || theme == "pastel" ||
           theme == "mint" || theme == "lavender" || theme == "peach" ||
           theme == "sky" || theme == "rose" || theme == "butter" ||
           theme == "coral" || theme == "lilac" || theme == "sage" ||
           theme == "dracula" || theme == "retro";
}

inline std::vector<ThemeEntry> builtin_theme_entries() {
    return {
        // Legacy qcode classics
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
        {"retro", "Retro Terminal"},
        // Ported from opencode packages/tui/src/theme/assets
        {"opencode", "OpenCode"},
        {"tokyonight", "Tokyo Night"},
        {"nord", "Nord"},
        {"gruvbox", "Gruvbox"},
        {"catppuccin", "Catppuccin Mocha"},
        {"rosepine", "Rosé Pine"},
        {"vesper", "Vesper"},
        {"one-dark", "One Dark"},
        {"aura", "Aura"},
        {"zenburn", "Zenburn"},
        {"cobalt2", "Cobalt2"},
        {"synthwave84", "SynthWave '84"},
        {"osaka-jade", "Osaka Jade"},
        {"matrix", "Matrix"},
        {"flexoki", "Flexoki Dark"},
        {"material", "Material Ocean"},
        {"ayu", "Ayu Dark"},
        {"everforest", "Everforest"},
        {"kanagawa", "Kanagawa"},
        {"monokai", "Monokai"},
        {"github", "GitHub Dark"},
        {"solarized", "Solarized Dark"},
        {"dracula", "Dracula"},
    };
}

}  // namespace qcode
