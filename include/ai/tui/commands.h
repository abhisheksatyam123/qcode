#pragma once

#include <string>
#include <vector>

#include <ai/tui/state.h>

namespace ai {
namespace tui {

// ── Model selector entry (flat list across all providers) ──

struct ModelEntry {
    int provider_idx;
    int model_idx;
    std::string provider_name;
    std::string model_name;
    std::string model_id;
    std::string category;       // provider display name for grouping
};

// ── Slash command descriptor ──

struct SlashCommand {
    std::string name;
    std::string title;
    std::string description;
    std::string category;
};

// Only slash commands that exist in opencode and apply to qcode.
inline std::vector<SlashCommand> builtin_slash_commands() {
    return {
        {"model",    "Switch model",  "Select a model from any provider",  "Model"},
        {"theme",    "Switch theme",  "Select a UI theme color",           "Theme"},
        {"tools",    "Toggle tools",  "Toggle tool usage / streaming mode", "Settings"},
        {"new",      "New session",   "Start a new clean chat session",    "Session"},
        {"session",  "Select session", "Manage and load saved sessions",   "Session"},
        {"help",     "Help",          "Show available commands",           "Help"},
        {"compact",  "Compact",       "Summarize conversation to save context", "Session"},
        {"reasoning", "Reasoning", "Set reasoning level: off|low|medium|high", "Session"},
    };
}

// Build flat list of all models across all providers.
std::vector<ModelEntry> build_model_entries(
    const std::vector<ProviderInfo>& providers
);

// Dispatch a slash command.
bool handle_slash_command(
    const std::string& raw_cmd,
    std::string& prompt_input,
    std::vector<ProviderInfo>& providers_list,
    int& selected_provider,
    int& selected_model,
    bool& enable_tools,
    std::string& system_prompt,
    ChatState& state
);

} // namespace tui
} // namespace ai
