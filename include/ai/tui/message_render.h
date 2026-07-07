#pragma once
#include <ftxui/dom/elements.hpp>
#include <ai/types/message.h>
#include <ai/tui/state.h>

namespace ai {
namespace tui {
ftxui::Element render_message(const ai::Message& msg, const ChatState& state, const std::vector<ProviderInfo>& providers_list, int selected_provider, int selected_model, const std::string& theme);
}
}
