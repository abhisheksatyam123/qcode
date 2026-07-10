#pragma once

#include <atomic>
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
    int context_window = 0;   // tokens; 0 => unknown (fallback)
    double input_cost = 0.0;   // USD per 1M input tokens
    double output_cost = 0.0;  // USD per 1M output tokens
    bool reasoning = false;
    bool tool_call = false;
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
    std::shared_ptr<int> scroll_line = std::make_shared<int>(INT_MAX);
    bool auto_scroll = true;
    std::shared_ptr<bool> show_thinking = std::make_shared<bool>(true);
    // Reasoning/thinking level: "off" | "low" | "medium" | "high" (opt-in)
    std::shared_ptr<std::string> reasoning_mode = std::make_shared<std::string>("off");
    std::shared_ptr<std::atomic<int>> generation_frame = std::make_shared<std::atomic<int>>(0);

    // Tab navigation
    int tab_selected = 0; // 0 = Chat, 1 = Files, 2 = Stats
    int selected_file = 0; // Selected index in modified_files

    int terminal_height = 40; // Approximate terminal height, updated during render

    // Color Theme Option (orange, green, blue, purple, monochrome)
    std::shared_ptr<std::string> theme = std::make_shared<std::string>("orange");

    // Active persistent session ID (UUID)
    std::shared_ptr<std::string> session_id = std::make_shared<std::string>();

    // Slash command autocomplete suggestions state
    bool slash_suggestion_mode = false;
    int slash_suggestion_idx = 0;

    // Tool observability stats
    std::shared_ptr<int> tool_call_count = std::make_shared<int>(0);
    std::shared_ptr<double> total_tool_time_ms = std::make_shared<double>(0.0);

    // Prompt queue + status mirrors (consumed by the view)
    std::shared_ptr<int> queued_prompts = std::make_shared<int>(0);
    std::shared_ptr<std::string> status = std::make_shared<std::string>("idle");
    std::shared_ptr<std::string> last_error = std::make_shared<std::string>();
};

// Helper to scan for modified files using git status
void update_modified_files(ChatState& state);

// Helper to copy text to system clipboard
void copy_to_clipboard(const std::string& text);

} // namespace tui
} // namespace ai
