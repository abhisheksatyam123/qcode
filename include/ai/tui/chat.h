#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include <ai/tui/state.h>
#include <ai/types/message.h>

namespace ai {
namespace tui {

// Run LLM generation in a background thread.
// screen is used to Post UI updates back to the main thread.
void run_llm_generation(
    std::string provider,
    std::string model,
    std::string system_prompt,
    ai::Messages messages,
    bool enable_tools,
    std::vector<ProviderInfo> providers_list,
    ftxui::ScreenInteractive* screen,
    ChatState state
);

} // namespace tui
} // namespace ai
