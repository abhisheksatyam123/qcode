#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <string>
#include <vector>

#include <ai/tui/commands.h>
#include <ai/tui/state.h>

namespace ai {
namespace tui {

// Colour palette with theme options
inline ftxui::Color accent(const std::string& theme = "orange") {
    if (theme == "green") return ftxui::Color::RGB(0x22, 0xBB, 0x88);
    if (theme == "blue") return ftxui::Color::RGB(0x16, 0xB8, 0xF3);
    if (theme == "purple") return ftxui::Color::RGB(0xD8, 0x50, 0xE0);
    if (theme == "monochrome") return ftxui::Color::RGB(0xAA, 0xAA, 0xAA);
    return ftxui::Color::RGB(0xEC, 0x5B, 0x2B);
}
inline ftxui::Color accent2(const std::string& theme = "orange") {
    if (theme == "green") return ftxui::Color::RGB(0x44, 0xDD, 0xAA);
    if (theme == "blue") return ftxui::Color::RGB(0x48, 0x7C, 0xFF);
    if (theme == "purple") return ftxui::Color::RGB(0xEE, 0x80, 0xF8);
    if (theme == "monochrome") return ftxui::Color::RGB(0xDD, 0xDD, 0xDD);
    return ftxui::Color::RGB(0xEE, 0x79, 0x48);
}
inline ftxui::Color user_green() { return ftxui::Color::RGB(0x22, 0xBB, 0x88); }
inline ftxui::Color dim_gray()   { return ftxui::Color::GrayDark; }
inline ftxui::Color bg_popup()   { return ftxui::Color::RGB(0x22, 0x22, 0x22); }

ftxui::Element render_logo();

ftxui::Element render_view(
    const ChatState& state,
    const std::vector<ProviderInfo>& providers_list,
    int selected_provider,
    int selected_model,
    bool enable_tools,
    const std::string& prompt_input, // <-- Added!
    // Slash popup state
    bool show_slash,
    int slash_idx,
    const std::vector<SlashCommand>& slash_commands,
    // Model selector popup state
    bool show_model_select,
    int model_select_idx,
    const std::vector<ModelEntry>& model_entries,
    // Interactive components
    const ftxui::Component& tab_toggle,
    const ftxui::Component& files_menu,
    const ftxui::Component& input
);

} // namespace tui
} // namespace ai
