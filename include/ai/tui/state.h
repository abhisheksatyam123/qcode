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
};

} // namespace tui
} // namespace ai
