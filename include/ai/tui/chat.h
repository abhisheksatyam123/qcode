#pragma once

#include <memory>
#include <string>
#include <utility>
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
    std::shared_ptr<bool> is_generating_flag,
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> chat_history_ptr,
    std::shared_ptr<ai::Messages> messages_history_ptr,
    std::shared_ptr<int> total_prompt_tokens_ptr,
    std::shared_ptr<int> total_completion_tokens_ptr,
    std::shared_ptr<int> total_tokens_ptr,
    std::shared_ptr<std::vector<std::string>> modified_files_ptr
);

} // namespace tui
} // namespace ai
