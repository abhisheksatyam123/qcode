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
// (packages/tui/src/theme/assets/*.json, dark variants). Roles mirror
// opencode's Theme type: base chrome + markdown* + syntax* element roles.
// Regenerate with scratchpad/gen_palettes.py (verifies old-8 unchanged).
struct ThemePalette {
    const char* name;
    unsigned accent;
    unsigned accent2;
    unsigned success;
    unsigned error;
    unsigned muted;
    unsigned panel_bg;
    unsigned focus_bg;
    unsigned warning;
    unsigned bg;
    unsigned md_text;
    unsigned md_heading;
    unsigned md_link;
    unsigned md_link_text;
    unsigned md_code;
    unsigned md_quote;
    unsigned md_emph;
    unsigned md_strong;
    unsigned md_hr;
    unsigned md_list_item;
    unsigned md_list_enum;
    unsigned md_image;
    unsigned md_image_text;
    unsigned md_codeblock;
    unsigned syn_comment;
    unsigned syn_keyword;
    unsigned syn_function;
    unsigned syn_variable;
    unsigned syn_string;
    unsigned syn_number;
    unsigned syn_type;
    unsigned syn_operator;
    unsigned syn_punct;
};

inline const ThemePalette* find_theme_palette(const std::string& theme) {
    static const ThemePalette kPalettes[] = {
        // name, accent, accent2, success, error, muted, panel_bg, focus_bg,
        // warning, bg, md_text, md_heading, md_link, md_link_text, md_code,
        // md_quote, md_emph, md_strong, md_hr, md_list_item, md_list_enum,
        // md_image, md_image_text, md_codeblock, syn_comment, syn_keyword,
        // syn_function, syn_variable, syn_string, syn_number, syn_type,
        // syn_operator, syn_punct
        {"opencode", 0xFAB283u, 0x5C9CF5u, 0x7FD88Fu, 0xE06C75u, 0x808080u, 0x141414u, 0x1E1E1Eu, 0xF5A742u, 0x0A0A0Au, 0xEEEEEEu, 0x9D7CD8u, 0xFAB283u, 0x56B6C2u, 0x7FD88Fu, 0xE5C07Bu, 0xE5C07Bu, 0xF5A742u, 0x808080u, 0xFAB283u, 0x56B6C2u, 0xFAB283u, 0x56B6C2u, 0xEEEEEEu, 0x808080u, 0x9D7CD8u, 0xFAB283u, 0xE06C75u, 0x7FD88Fu, 0xF5A742u, 0xE5C07Bu, 0x56B6C2u, 0xEEEEEEu},
        {"tokyonight", 0x82AAFFu, 0xC099FFu, 0xC3E88Du, 0xFF757Fu, 0x828BB8u, 0x1E2030u, 0x222436u, 0xFF966Cu, 0x1A1B26u, 0xC8D3F5u, 0xC099FFu, 0x82AAFFu, 0x86E1FCu, 0xC3E88Du, 0xFFC777u, 0xFFC777u, 0xFF966Cu, 0x828BB8u, 0x82AAFFu, 0x86E1FCu, 0x82AAFFu, 0x86E1FCu, 0xC8D3F5u, 0x828BB8u, 0xC099FFu, 0x82AAFFu, 0xFF757Fu, 0xC3E88Du, 0xFF966Cu, 0xFFC777u, 0x86E1FCu, 0xC8D3F5u},
        {"nord", 0x88C0D0u, 0x81A1C1u, 0xA3BE8Cu, 0xBF616Au, 0x8B95A7u, 0x3B4252u, 0x434C5Eu, 0xD08770u, 0x2E3440u, 0xD8DEE9u, 0x88C0D0u, 0x81A1C1u, 0x8FBCBBu, 0xA3BE8Cu, 0x8B95A7u, 0xD08770u, 0xEBCB8Bu, 0x8B95A7u, 0x88C0D0u, 0x8FBCBBu, 0x81A1C1u, 0x8FBCBBu, 0xD8DEE9u, 0x8B95A7u, 0x81A1C1u, 0x88C0D0u, 0x8FBCBBu, 0xA3BE8Cu, 0xB48EADu, 0x8FBCBBu, 0x81A1C1u, 0xD8DEE9u},
        {"gruvbox", 0x83A598u, 0xD3869Bu, 0xB8BB26u, 0xFB4934u, 0x928374u, 0x3C3836u, 0x504945u, 0xFE8019u, 0x282828u, 0xEBDBB2u, 0x83A598u, 0x8EC07Cu, 0xB8BB26u, 0xFABD2Fu, 0x928374u, 0xD3869Bu, 0xFE8019u, 0x928374u, 0x83A598u, 0x8EC07Cu, 0x8EC07Cu, 0xB8BB26u, 0xEBDBB2u, 0x928374u, 0xFB4934u, 0xB8BB26u, 0x83A598u, 0xFABD2Fu, 0xD3869Bu, 0x8EC07Cu, 0xFE8019u, 0xEBDBB2u},
        {"catppuccin", 0x89B4FAu, 0xCBA6F7u, 0xA6E3A1u, 0xF38BA8u, 0x9399B2u, 0x181825u, 0x11111Bu, 0xF9E2AFu, 0x1E1E2Eu, 0xCDD6F4u, 0xCBA6F7u, 0x89B4FAu, 0x89DCEBu, 0xA6E3A1u, 0xF9E2AFu, 0xF9E2AFu, 0xFAB387u, 0xA6ADC8u, 0x89B4FAu, 0x89DCEBu, 0x89B4FAu, 0x89DCEBu, 0xCDD6F4u, 0x9399B2u, 0xCBA6F7u, 0x89B4FAu, 0xF38BA8u, 0xA6E3A1u, 0xFAB387u, 0xF9E2AFu, 0x89DCEBu, 0xCDD6F4u},
        {"rosepine", 0x9CCFD8u, 0xC4A7E7u, 0x31748Fu, 0xEB6F92u, 0x6E6A86u, 0x1F1D2Eu, 0x26233Au, 0xF6C177u, 0x191724u, 0xE0DEF4u, 0xC4A7E7u, 0x9CCFD8u, 0xEBBCBAu, 0x31748Fu, 0x6E6A86u, 0xF6C177u, 0xEB6F92u, 0x403D52u, 0x9CCFD8u, 0xEBBCBAu, 0x9CCFD8u, 0xEBBCBAu, 0xE0DEF4u, 0x6E6A86u, 0x31748Fu, 0xEBBCBAu, 0xE0DEF4u, 0xF6C177u, 0xC4A7E7u, 0x9CCFD8u, 0x908CAAu, 0x908CAAu},
        {"vesper", 0xFFC799u, 0x99FFE4u, 0x99FFE4u, 0xFF8080u, 0xA0A0A0u, 0x101010u, 0x101010u, 0xFFC799u, 0x101010u, 0xFFFFFFu, 0xFFC799u, 0xFFC799u, 0xA0A0A0u, 0xA0A0A0u, 0xFFFFFFu, 0xFFFFFFu, 0xFFFFFFu, 0x65737Eu, 0xFFFFFFu, 0xFFFFFFu, 0xFFC799u, 0xA0A0A0u, 0xFFFFFFu, 0x8B8B8Bu, 0xA0A0A0u, 0xFFC799u, 0xFFFFFFu, 0x99FFE4u, 0xFFC799u, 0xFFC799u, 0xA0A0A0u, 0xFFFFFFu},
        {"one-dark", 0x61AFEFu, 0xC678DDu, 0x98C379u, 0xE06C75u, 0x5C6370u, 0x21252Bu, 0x353B45u, 0xE5C07Bu, 0x282C34u, 0xABB2BFu, 0xC678DDu, 0x61AFEFu, 0x56B6C2u, 0x98C379u, 0x5C6370u, 0xE5C07Bu, 0xD19A66u, 0x5C6370u, 0x61AFEFu, 0x56B6C2u, 0x61AFEFu, 0x56B6C2u, 0xABB2BFu, 0x5C6370u, 0xC678DDu, 0x61AFEFu, 0xE06C75u, 0x98C379u, 0xD19A66u, 0xE5C07Bu, 0x56B6C2u, 0xABB2BFu},
        {"aura", 0xA277FFu, 0xF694FFu, 0x61FFCAu, 0xFF6767u, 0x6D6D6Du, 0x15141Bu, 0x15141Bu, 0xFFCA85u, 0x0F0F0Fu, 0xEDECEEu, 0xA277FFu, 0xF694FFu, 0xA277FFu, 0x61FFCAu, 0x6D6D6Du, 0xFFCA85u, 0xA277FFu, 0x6D6D6Du, 0xA277FFu, 0xA277FFu, 0xF694FFu, 0xA277FFu, 0xEDECEEu, 0x6D6D6Du, 0xF694FFu, 0xA277FFu, 0xA277FFu, 0x61FFCAu, 0x9DFF65u, 0xA277FFu, 0xF694FFu, 0xEDECEEu},
        {"zenburn", 0x8CD0D3u, 0xDC8CC3u, 0x7F9F7Fu, 0xCC9393u, 0x9F9F9Fu, 0x4F4F4Fu, 0x5F5F5Fu, 0xF0DFAFu, 0x3F3F3Fu, 0xDCDCCCu, 0xF0DFAFu, 0x8CD0D3u, 0x93E0E3u, 0x7F9F7Fu, 0x9F9F9Fu, 0xE0CF9Fu, 0xDFAF8Fu, 0x9F9F9Fu, 0x8CD0D3u, 0x93E0E3u, 0x8CD0D3u, 0x93E0E3u, 0xDCDCCCu, 0x7F9F7Fu, 0xF0DFAFu, 0x8CD0D3u, 0xDCDCCCu, 0xCC9393u, 0x8FB28Fu, 0x93E0E3u, 0xF0DFAFu, 0xDCDCCCu},
        {"cobalt2", 0x0088FFu, 0x9A5FEBu, 0x9EFF80u, 0xFF0088u, 0xADB7C9u, 0x122738u, 0x1F4662u, 0xFFC600u, 0x193549u, 0xFFFFFFu, 0xFFC600u, 0x0088FFu, 0x2AFFDFu, 0x9EFF80u, 0xADB7C9u, 0xFF9D00u, 0xFF628Cu, 0x2D5A7Bu, 0x0088FFu, 0x2AFFDFu, 0x0088FFu, 0x2AFFDFu, 0xFFFFFFu, 0x0088FFu, 0xFF9D00u, 0xFFC600u, 0xFFFFFFu, 0x9EFF80u, 0xFF628Cu, 0x2AFFDFu, 0xFF9D00u, 0xFFFFFFu},
        {"synthwave84", 0x36F9F6u, 0xFF7EDBu, 0x72F1B8u, 0xFE4450u, 0x848BBDu, 0x1E1A29u, 0x2A2139u, 0xFEDE5Du, 0x262335u, 0xFFFFFFu, 0xFF7EDBu, 0x36F9F6u, 0xB084EBu, 0x72F1B8u, 0x848BBDu, 0xFEDE5Du, 0xFF8B39u, 0x495495u, 0x36F9F6u, 0xB084EBu, 0x36F9F6u, 0xB084EBu, 0xFFFFFFu, 0x848BBDu, 0xFF7EDBu, 0xFF8B39u, 0xFFFFFFu, 0xFEDE5Du, 0xB084EBu, 0x36F9F6u, 0xFF7EDBu, 0xFFFFFFu},
        {"osaka-jade", 0x2DD5B7u, 0xD2689Cu, 0x549E6Au, 0xFF5345u, 0x53685Bu, 0x1A2520u, 0x23372Bu, 0xE5C736u, 0x111C18u, 0xC1C497u, 0x2DD5B7u, 0x8CD3CBu, 0x549E6Au, 0x63B07Au, 0x53685Bu, 0xD2689Cu, 0xC1C497u, 0x53685Bu, 0x2DD5B7u, 0x8CD3CBu, 0x8CD3CBu, 0x549E6Au, 0xC1C497u, 0x53685Bu, 0x2DD5B7u, 0x509475u, 0xC1C497u, 0x63B07Au, 0xD2689Cu, 0x549E6Au, 0x459451u, 0xC1C497u},
        {"matrix", 0x2EFF6Au, 0x00EFFFu, 0x62FF94u, 0xFF4B4Bu, 0x8CA391u, 0x0E130Du, 0x141C12u, 0xE6FF57u, 0x0A0E0Au, 0x62FF94u, 0x00EFFFu, 0x30B3FFu, 0x24F6D9u, 0x1CC24Bu, 0x8CA391u, 0xFFA83Du, 0xE6FF57u, 0x8CA391u, 0x30B3FFu, 0x24F6D9u, 0x30B3FFu, 0x24F6D9u, 0x62FF94u, 0x8CA391u, 0xC770FFu, 0x30B3FFu, 0x62FF94u, 0x1CC24Bu, 0xFFA83Du, 0xE6FF57u, 0x24F6D9u, 0x62FF94u},
        {"flexoki", 0xDA702Cu, 0x4385BEu, 0x879A39u, 0xD14D41u, 0x6F6E69u, 0x1C1B1Au, 0x282726u, 0xDA702Cu, 0x100F0Fu, 0xCECDC3u, 0x8B7EC8u, 0x4385BEu, 0x3AA99Fu, 0x3AA99Fu, 0xD0A215u, 0xD0A215u, 0xDA702Cu, 0x6F6E69u, 0xDA702Cu, 0x3AA99Fu, 0xCE5D97u, 0x3AA99Fu, 0xCECDC3u, 0x6F6E69u, 0x879A39u, 0xDA702Cu, 0x4385BEu, 0x3AA99Fu, 0x8B7EC8u, 0xD0A215u, 0xB7B5ACu, 0xB7B5ACu},
        {"material", 0x82AAFFu, 0xC792EAu, 0xC3E88Du, 0xF07178u, 0x546E7Au, 0x1E272Cu, 0x37474Fu, 0xFFCB6Bu, 0x263238u, 0xEEFFFFu, 0x82AAFFu, 0x89DDFFu, 0xC792EAu, 0xC3E88Du, 0x546E7Au, 0xFFCB6Bu, 0xFFCB6Bu, 0x37474Fu, 0x82AAFFu, 0x89DDFFu, 0x89DDFFu, 0xC792EAu, 0xEEFFFFu, 0x546E7Au, 0xC792EAu, 0x82AAFFu, 0xEEFFFFu, 0xC3E88Du, 0xFFCB6Bu, 0xFFCB6Bu, 0x89DDFFu, 0xEEFFFFu},
        {"ayu", 0x59C2FFu, 0xD2A6FFu, 0x7FD962u, 0xD95757u, 0x565B66u, 0x0F131Au, 0x0D1017u, 0xE6B673u, 0x0B0E14u, 0xBFBDB6u, 0xD2A6FFu, 0x59C2FFu, 0x39BAE6u, 0xAAD94Cu, 0xE6B673u, 0xE6B673u, 0xFFB454u, 0x565B66u, 0x59C2FFu, 0x39BAE6u, 0x59C2FFu, 0x39BAE6u, 0xBFBDB6u, 0xACB6BFu, 0xFF8F40u, 0xFFB454u, 0x59C2FFu, 0xAAD94Cu, 0xD2A6FFu, 0xE6B673u, 0xF29668u, 0xBFBDB6u},
        {"everforest", 0xA7C080u, 0x7FBBB3u, 0xA7C080u, 0xE67E80u, 0x7A8478u, 0x333C43u, 0x343F44u, 0xE69875u, 0x2D353Bu, 0xD3C6AAu, 0xD699B6u, 0xA7C080u, 0x83C092u, 0xA7C080u, 0xDBBC7Fu, 0xDBBC7Fu, 0xE69875u, 0x7A8478u, 0xA7C080u, 0x83C092u, 0xA7C080u, 0x83C092u, 0xD3C6AAu, 0x7A8478u, 0xD699B6u, 0xA7C080u, 0xE67E80u, 0xA7C080u, 0xE69875u, 0xDBBC7Fu, 0x83C092u, 0xD3C6AAu},
        {"kanagawa", 0x7E9CD8u, 0x957FB8u, 0x98BB6Cu, 0xE82424u, 0x727169u, 0x2A2A37u, 0x363646u, 0xD7A657u, 0x1F1F28u, 0xDCD7BAu, 0x957FB8u, 0x7E9CD8u, 0x76946Au, 0x98BB6Cu, 0x727169u, 0xC38D9Du, 0xD7A657u, 0x727169u, 0x7E9CD8u, 0x76946Au, 0x7E9CD8u, 0x76946Au, 0xDCD7BAu, 0x727169u, 0x957FB8u, 0x7E9CD8u, 0xDCD7BAu, 0x98BB6Cu, 0xD7A657u, 0xC38D9Du, 0xD27E99u, 0xDCD7BAu},
        {"monokai", 0x66D9EFu, 0xAE81FFu, 0xA6E22Eu, 0xF92672u, 0x75715Eu, 0x1E1F1Cu, 0x3E3D32u, 0xE6DB74u, 0x272822u, 0xF8F8F2u, 0xF92672u, 0x66D9EFu, 0xAE81FFu, 0xA6E22Eu, 0x75715Eu, 0xE6DB74u, 0xFD971Fu, 0x75715Eu, 0x66D9EFu, 0xAE81FFu, 0x66D9EFu, 0xAE81FFu, 0xF8F8F2u, 0x75715Eu, 0xF92672u, 0xA6E22Eu, 0xF8F8F2u, 0xE6DB74u, 0xAE81FFu, 0x66D9EFu, 0xF92672u, 0xF8F8F2u},
        {"github", 0x58A6FFu, 0xBC8CFFu, 0x3FB950u, 0xF85149u, 0x8B949Eu, 0x010409u, 0x161B22u, 0xE3B341u, 0x0D1117u, 0xC9D1D9u, 0x58A6FFu, 0x58A6FFu, 0x39C5CFu, 0xFF7B72u, 0x8B949Eu, 0xE3B341u, 0xD29922u, 0x30363Du, 0x58A6FFu, 0x39C5CFu, 0x58A6FFu, 0x39C5CFu, 0xC9D1D9u, 0x8B949Eu, 0xFF7B72u, 0xBC8CFFu, 0xD29922u, 0x39C5CFu, 0x58A6FFu, 0xD29922u, 0xFF7B72u, 0xC9D1D9u},
        {"solarized", 0x268BD2u, 0x6C71C4u, 0x859900u, 0xDC322Fu, 0x586E75u, 0x073642u, 0x073642u, 0xB58900u, 0x002B36u, 0x839496u, 0x268BD2u, 0x2AA198u, 0x6C71C4u, 0x859900u, 0x586E75u, 0xB58900u, 0xCB4B16u, 0x586E75u, 0x268BD2u, 0x2AA198u, 0x2AA198u, 0x6C71C4u, 0x839496u, 0x586E75u, 0x859900u, 0x268BD2u, 0x2AA198u, 0x2AA198u, 0xD33682u, 0xB58900u, 0x859900u, 0x839496u},
        {"dracula", 0xBD93F9u, 0xFF79C6u, 0x50FA7Bu, 0xFF5555u, 0x6272A4u, 0x21222Cu, 0x44475Au, 0xF1FA8Cu, 0x282A36u, 0xF8F8F2u, 0xBD93F9u, 0x8BE9FDu, 0xFF79C6u, 0x50FA7Bu, 0x6272A4u, 0xF1FA8Cu, 0xFFB86Cu, 0x6272A4u, 0xBD93F9u, 0x8BE9FDu, 0x8BE9FDu, 0xFF79C6u, 0xF8F8F2u, 0x6272A4u, 0xFF79C6u, 0x50FA7Bu, 0xF8F8F2u, 0xF1FA8Cu, 0xBD93F9u, 0x8BE9FDu, 0xFF79C6u, 0xF8F8F2u},
        {"carbonfox", 0x33B1FFu, 0x78A9FFu, 0x25BE6Au, 0xEE5396u, 0x7D848Fu, 0x1A1A1Au, 0x1E1E1Eu, 0xF1C21Bu, 0x161616u, 0xF2F4F8u, 0x8CB6FFu, 0x78A9FFu, 0x33B1FFu, 0x25BE6Au, 0x7D848Fu, 0xBE95FFu, 0xFFFFFFu, 0x303030u, 0x33B1FFu, 0x33B1FFu, 0x78A9FFu, 0x33B1FFu, 0xA9AFBCu, 0x7D848Fu, 0xBE95FFu, 0x8CB6FFu, 0xDFDFE0u, 0x25BE6Au, 0x3DDBD9u, 0x08BDBAu, 0xA9AFBCu, 0xA9AFBCu},
        {"catppuccin-frappe", 0x8DA4E2u, 0xCA9EE6u, 0xA6D189u, 0xE78284u, 0x949CB8u, 0x292C3Cu, 0x232634u, 0xE5C890u, 0x303446u, 0xC6D0F5u, 0xCA9EE6u, 0x8DA4E2u, 0x99D1DBu, 0xA6D189u, 0xE5C890u, 0xE5C890u, 0xEF9F76u, 0xA5ADCEu, 0x8DA4E2u, 0x99D1DBu, 0x8DA4E2u, 0x99D1DBu, 0xC6D0F5u, 0x949CB8u, 0xCA9EE6u, 0x8DA4E2u, 0xE78284u, 0xA6D189u, 0xEF9F76u, 0xE5C890u, 0x99D1DBu, 0xC6D0F5u},
        {"catppuccin-macchiato", 0x8AADF4u, 0xC6A0F6u, 0xA6DA95u, 0xED8796u, 0x939AB7u, 0x1E2030u, 0x181926u, 0xEED49Fu, 0x24273Au, 0xCAD3F5u, 0xC6A0F6u, 0x8AADF4u, 0x91D7E3u, 0xA6DA95u, 0xEED49Fu, 0xEED49Fu, 0xF5A97Fu, 0xA5ADCBu, 0x8AADF4u, 0x91D7E3u, 0x8AADF4u, 0x91D7E3u, 0xCAD3F5u, 0x939AB7u, 0xC6A0F6u, 0x8AADF4u, 0xED8796u, 0xA6DA95u, 0xF5A97Fu, 0xEED49Fu, 0x91D7E3u, 0xCAD3F5u},
        {"cursor", 0x88C0D0u, 0x81A1C1u, 0x3FA266u, 0xE34671u, 0xE4E4E4u, 0x141414u, 0x262626u, 0xF1B467u, 0x181818u, 0xE4E4E4u, 0xAAA0FAu, 0x82D2CEu, 0x81A1C1u, 0xE394DCu, 0xE4E4E4u, 0x82D2CEu, 0xF8C762u, 0xE4E4E4u, 0xE4E4E4u, 0x88C0D0u, 0x88C0D0u, 0x81A1C1u, 0xE4E4E4u, 0xE4E4E4u, 0x82D2CEu, 0xEFB080u, 0xE4E4E4u, 0xE394DCu, 0xF8C762u, 0xEFB080u, 0xE4E4E4u, 0xE4E4E4u},
        {"lucent-orng", 0xEC5B2Bu, 0xEE7948u, 0x6BA1E6u, 0xE06C75u, 0x808080u, 0x000000u, 0x000000u, 0xEC5B2Bu, 0x000000u, 0xEEEEEEu, 0xEC5B2Bu, 0xEC5B2Bu, 0x56B6C2u, 0x6BA1E6u, 0xFFF7F1u, 0xE5C07Bu, 0xEE7948u, 0x808080u, 0xEC5B2Bu, 0x56B6C2u, 0xEC5B2Bu, 0x56B6C2u, 0xEEEEEEu, 0x808080u, 0xEC5B2Bu, 0xEE7948u, 0xE06C75u, 0x6BA1E6u, 0xFFF7F1u, 0xE5C07Bu, 0x56B6C2u, 0xEEEEEEu},
        {"mercury", 0x8DA4F5u, 0xA7B6F8u, 0x77C599u, 0xFC92B4u, 0x9D9DA8u, 0x10101Au, 0x272735u, 0xFC9B6Fu, 0x171721u, 0xDDDDE5u, 0xFFFFFFu, 0x8DA4F5u, 0xA7B6F8u, 0x77C599u, 0x9D9DA8u, 0xFC9B6Fu, 0xF4F5F9u, 0xB4B7C8u, 0xFFFFFFu, 0x8DA4F5u, 0x8DA4F5u, 0xA7B6F8u, 0xDDDDE5u, 0x9D9DA8u, 0x8DA4F5u, 0x8DA4F5u, 0x77BECFu, 0x77C599u, 0xFC9B6Fu, 0x77BECFu, 0x8DA4F5u, 0xDDDDE5u},
        {"nightowl", 0x82AAFFu, 0x7FDBCAu, 0xC5E478u, 0xEF5350u, 0x5F7E97u, 0x0B253Au, 0x0B253Au, 0xECC48Du, 0x011627u, 0xD6DEEBu, 0x82AAFFu, 0x7FDBCAu, 0x82AAFFu, 0xC5E478u, 0x5F7E97u, 0xC792EAu, 0xECC48Du, 0x5F7E97u, 0x82AAFFu, 0x7FDBCAu, 0x7FDBCAu, 0x82AAFFu, 0xD6DEEBu, 0x637777u, 0xC792EAu, 0x82AAFFu, 0xD6DEEBu, 0xECC48Du, 0xF78C6Cu, 0xC5E478u, 0x7FDBCAu, 0xD6DEEBu},
        {"orng", 0xEC5B2Bu, 0xEE7948u, 0x6BA1E6u, 0xE06C75u, 0x808080u, 0x141414u, 0x1E1E1Eu, 0xEC5B2Bu, 0x0A0A0Au, 0xEEEEEEu, 0xEC5B2Bu, 0xEC5B2Bu, 0x56B6C2u, 0x6BA1E6u, 0xFFF7F1u, 0xE5C07Bu, 0xEE7948u, 0x808080u, 0xEC5B2Bu, 0x56B6C2u, 0xEC5B2Bu, 0x56B6C2u, 0xEEEEEEu, 0x808080u, 0xEC5B2Bu, 0xEE7948u, 0xE06C75u, 0x6BA1E6u, 0xFFF7F1u, 0xE5C07Bu, 0x56B6C2u, 0xEEEEEEu},
        {"palenight", 0x82AAFFu, 0xC792EAu, 0xC3E88Du, 0xF07178u, 0x676E95u, 0x1E2132u, 0x32364Au, 0xFFCB6Bu, 0x292D3Eu, 0xA6ACCDu, 0xC792EAu, 0x82AAFFu, 0x89DDFFu, 0xC3E88Du, 0x676E95u, 0xFFCB6Bu, 0xF78C6Cu, 0x676E95u, 0x82AAFFu, 0x89DDFFu, 0x82AAFFu, 0x89DDFFu, 0xA6ACCDu, 0x676E95u, 0xC792EAu, 0x82AAFFu, 0xA6ACCDu, 0xC3E88Du, 0xF78C6Cu, 0xFFCB6Bu, 0x89DDFFu, 0xA6ACCDu},
        {"vercel", 0x0070F3u, 0x52A8FFu, 0x46A758u, 0xE5484Du, 0x878787u, 0x1A1A1Au, 0x292929u, 0xFFB224u, 0x000000u, 0xEDEDEDu, 0xBF7AF0u, 0x52A8FFu, 0x0AC7ACu, 0x63C46Du, 0x878787u, 0xF2A700u, 0xF75590u, 0x454545u, 0xEDEDEDu, 0x52A8FFu, 0x0AC7ACu, 0x50E3C2u, 0xEDEDEDu, 0x878787u, 0xF75590u, 0xBF7AF0u, 0x52A8FFu, 0x63C46Du, 0xF2A700u, 0x0AC7ACu, 0xF75590u, 0xEDEDEDu},
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


// ── Extended roles (markdown + syntax + background) ─────────────────────────
// Ported from opencode's Theme type (packages/tui/src/theme/index.ts). For
// legacy hand-tuned themes without a palette row, derive coherent fallbacks
// from the base roles so custom themes keep working.
inline ftxui::Color theme_bg(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->bg);
    return theme_panel_bg(theme);
}
inline ftxui::Color theme_text_muted(const std::string& theme = "") {
    return theme_muted(theme);
}
inline ftxui::Color theme_text(const std::string& theme = "") {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_text);
    if (theme == "retro") return ftxui::Color::RGB(0x33, 0xFF, 0x33);
    return ftxui::Color::RGB(0xEE, 0xEE, 0xEE);
}
inline ftxui::Color theme_border(const std::string& theme = "") {
    return theme_muted(theme);
}
inline ftxui::Color theme_md_text(const std::string& theme) {
    return theme_text(theme);
}
inline ftxui::Color theme_md_heading(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_heading);
    return accent(theme);
}
inline ftxui::Color theme_md_link(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_link);
    return accent(theme);
}
inline ftxui::Color theme_md_link_text(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_link_text);
    return accent2(theme);
}
inline ftxui::Color theme_md_code(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_code);
    return theme_success(theme);
}
inline ftxui::Color theme_md_quote(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_quote);
    return theme_warning(theme);
}
inline ftxui::Color theme_md_emph(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_emph);
    return theme_warning(theme);
}
inline ftxui::Color theme_md_strong(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_strong);
    return theme_warning(theme);
}
inline ftxui::Color theme_md_hr(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_hr);
    return theme_muted(theme);
}
inline ftxui::Color theme_md_list_item(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_list_item);
    return accent(theme);
}
inline ftxui::Color theme_md_list_enum(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_list_enum);
    return accent2(theme);
}
inline ftxui::Color theme_md_image(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_image);
    return accent(theme);
}
inline ftxui::Color theme_md_image_text(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_image_text);
    return accent2(theme);
}
inline ftxui::Color theme_md_codeblock(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->md_codeblock);
    return theme_text(theme);
}
inline ftxui::Color theme_syn_comment(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->syn_comment);
    return theme_muted(theme);
}
inline ftxui::Color theme_syn_keyword(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->syn_keyword);
    return accent2(theme);
}
inline ftxui::Color theme_syn_function(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->syn_function);
    return accent(theme);
}
inline ftxui::Color theme_syn_variable(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->syn_variable);
    return theme_error(theme);
}
inline ftxui::Color theme_syn_string(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->syn_string);
    return theme_success(theme);
}
inline ftxui::Color theme_syn_number(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->syn_number);
    return theme_warning(theme);
}
inline ftxui::Color theme_syn_type(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->syn_type);
    return theme_warning(theme);
}
inline ftxui::Color theme_syn_operator(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->syn_operator);
    return accent2(theme);
}
inline ftxui::Color theme_syn_punct(const std::string& theme) {
    if (const auto* p = find_theme_palette(theme)) return from_hex(p->syn_punct);
    return theme_text(theme);
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
        {"carbonfox", "Carbonfox"},
        {"catppuccin-frappe", "Catppuccin Frapp\u00e9"},
        {"catppuccin-macchiato", "Catppuccin Macchiato"},
        {"cursor", "Cursor Dark"},
        {"lucent-orng", "Lucent Orange"},
        {"mercury", "Mercury"},
        {"nightowl", "Night Owl"},
        {"orng", "Orange"},
        {"palenight", "Palenight"},
        {"vercel", "Vercel"},
    };
}

}  // namespace qcode
