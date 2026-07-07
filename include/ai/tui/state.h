#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ai/types/message.h>

namespace ai {
namespace tui {

// ── Provider/model types ──

struct ModelInfo {
    std::string name;
    std::string id;
};

struct ProviderInfo {
    std::string name;
    std::string id;
    std::string api_url;
    std::vector<ModelInfo> models;
};

// ── Shared chat state ──

struct ChatState {
    std::shared_ptr<bool> is_generating = std::make_shared<bool>(false);
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> chat_history =
        std::make_shared<std::vector<std::pair<std::string, std::string>>>();
    std::shared_ptr<ai::Messages> messages_history = std::make_shared<ai::Messages>();
    std::shared_ptr<int> total_prompt_tokens = std::make_shared<int>(0);
    std::shared_ptr<int> total_completion_tokens = std::make_shared<int>(0);
    std::shared_ptr<int> total_tokens = std::make_shared<int>(0);
    std::shared_ptr<std::vector<std::string>> modified_files = std::make_shared<std::vector<std::string>>();

    // Tab navigation
    int tab_selected = 0; // 0 = Chat, 1 = Files, 2 = Stats
    int selected_file = 0; // Selected index in modified_files

    // Selection & Clipboard
    int selected_message = -1; // Index in chat_history, or -1 if none
    bool selection_mode = false; // When true, arrow keys navigate messages instead of input

    // Color Theme Option (orange, green, blue, purple, monochrome)
    std::shared_ptr<std::string> theme = std::make_shared<std::string>("orange");

    // Active persistent session ID (UUID)
    std::shared_ptr<std::string> session_id = std::make_shared<std::string>();
};

// Helper to scan for modified files using git status
void update_modified_files(ChatState& state);

// Helper to copy text to system clipboard
void copy_to_clipboard(const std::string& text);

} // namespace tui
} // namespace ai
