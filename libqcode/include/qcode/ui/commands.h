#pragma once

#include <string>
#include <vector>
#include <memory>

#include <qcode/core/jthread.h>
#include <qcode/core/state.h>
#include <qcode/core/bus_port.h>

namespace qcode {

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
        {"new",      "New session",   "/new [name] [workspace]",           "Session"},
        {"rename",   "Rename session", "/rename <new name>",               "Session"},
        {"session",  "Select session", "Manage and load saved sessions",   "Session"},
        {"compact",  "Compact",       "Summarize conversation to save context", "Session"},
        {"variant",  "Model variant", "Select thinking / reasoning effort", "Model"},
        {"clear-queue", "Clear queue", "Clear all queued prompts",          "Session"},
        {"retry",       "Retry prompt", "/retry - resend last prompt",           "Session"},
    };
}

// Build flat list of all models across all providers.
struct VariantEntry {
    std::string id;
    std::string title;
    std::string description;
};

std::vector<ModelEntry> build_model_entries(
    const std::vector<ProviderInfo>& providers
);

// Model-specific thinking variants plus "off". Used by the /variant picker.
std::vector<VariantEntry> build_variant_entries(const ModelInfo& model);

// Dispatch a slash command.
bool handle_slash_command(
    const std::string& raw_cmd,
    std::string& prompt_input,
    std::vector<ProviderInfo>& providers_list,
    int& selected_provider,
    int& selected_model,
    bool& enable_tools,
    std::string& system_prompt,
    ChatState& state,
    std::shared_ptr<qcode::compat::jthread> compaction_thread,
    bus::BusPort& bus
);

// Run conversation compaction (used by /compact and auto-triggered pruning).
// Summarizes messages_history into a handoff packet and replaces it.
// keep = number of recent messages to preserve verbatim.
void run_compaction(
    ChatState& state,
    const std::vector<ProviderInfo>& providers_list,
    int selected_provider,
    int selected_model,
    int keep,
    std::shared_ptr<qcode::compat::jthread> compaction_thread,
    bus::BusPort& bus
);

} // namespace qcode
