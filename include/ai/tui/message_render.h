#pragma once
#include <ftxui/dom/elements.hpp>
#include <ai/types/message.h>
#include <ai/tui/state.h>

namespace ai {
namespace tui {

// Styled component wrapper for tools
ftxui::Element BlockTool(const std::string& title, ftxui::Element content, bool is_running = false, const std::string& status = "", ftxui::Color border_color = ftxui::Color::GrayDark);

ftxui::Element render_message(const ai::Message& msg, const ChatState& state, const std::vector<ProviderInfo>& providers_list, int selected_provider, int selected_model, const std::string& theme);

}
}
