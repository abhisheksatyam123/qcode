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

// Colour palette
inline ftxui::Color accent()     { return ftxui::Color::RGB(0xEC, 0x5B, 0x2B); }
inline ftxui::Color accent2()    { return ftxui::Color::RGB(0xEE, 0x79, 0x48); }
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
