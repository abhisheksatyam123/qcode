#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include <qcode/config/provider_info.h>
#include <qcode/core/bus_port.h>
#include <qcode/core/jthread.h>
#include <qcode/ui/chat_state.h>

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

inline std::vector<SlashCommand> builtin_slash_commands() {
    return {
        {"model",       "Switch model",   "Select a model from any provider",          "Model"},
        {"theme",       "Switch theme",   "Select a UI theme color",                   "Theme"},
        {"new",         "New session",    "/new [name] [workspace]",                   "Session"},
        {"rename",      "Rename session", "/rename <new name>",                        "Session"},
        {"session",     "Select session", "Manage and load saved sessions",            "Session"},
        {"compact",     "Compact",        "Summarize conversation to save context",    "Session"},
        {"variant",     "Model variant",  "Select thinking / reasoning effort",        "Model"},
        {"agent",       "Agent mode",     "Switch between build and plan",             "Agent"},
        {"queue",       "Prompt queue",   "/queue [rm <n>] — list or drop queued prompts", "Session"},
        {"clear-queue", "Clear queue",    "Clear all queued prompts",                  "Session"},
        {"retry",       "Retry prompt",   "/retry - resend last prompt",               "Session"},
        {"help",        "Help",           "Show help and keyboard shortcuts",          "General"},
    };
}

// ── Palette command descriptor (Ctrl-P, OpenCode-style) ──

struct PaletteCommand {
    std::string id;
    std::string title;
    std::string description;
    std::string category;
    std::string shortcut;
};

inline std::vector<PaletteCommand> builtin_palette_commands() {
    return {
        {"session_new",        "New Session",              "Start a new blank chat session",      "Session",     "Ctrl+N"},
        {"session_list",       "Switch Session",           "Manage and switch between sessions",  "Session",     "/session"},
        {"session_rename",     "Rename Session",           "Rename current chat session",         "Session",     "/rename"},
        {"session_compact",    "Compact Context",          "Summarize history to free context",   "Session",     "/compact"},
        {"session_clear_queue","Clear Prompt Queue",       "Remove all queued prompts",           "Session",     "/clear-queue"},
        {"session_retry",      "Retry Last Prompt",        "Re-send the last user prompt",        "Session",     "r / /retry"},
        {"model_select",       "Select Model",             "Switch AI model and provider",        "Model",       "/model"},
        {"model_variant",      "Select Reasoning Variant", "Switch reasoning effort / thinking",  "Model",       "/variant"},
        {"agent_mode_toggle",  "Toggle Agent Mode",        "Switch between Orchestrator and Plan modes", "Agent",       "Tab"},
        {"thinking_toggle",    "Toggle Thinking Trace",    "Show or hide reasoning blocks",       "View",        "Ctrl+T / F2"},
        {"files_open",         "Changed Files",            "View git diff and changed files",     "View",        "Alt+2"},
        {"stats_open",         "Session Stats",            "Token usage, cost, and tool stats",   "View",        "Alt+3"},
        {"sessions_open",      "Sessions & Subagents",     "List sessions, continue, and subagents", "View", "Alt+4"},
        {"theme_select",       "Switch Theme",             "Change color theme",                  "Appearance",  "/theme"},
        {"copy_mode_toggle",   "Toggle Copy Mode",         "Enable terminal mouse text selection","View",        "F3"},
        {"help",               "Help & Shortcuts",         "Show help and keyboard shortcuts",    "General",     "/help"},
        {"exit",               "Exit q-code",              "Quit application",                    "General",     "Ctrl+C"},
    };
}

// Build flat list of all models across all providers.
struct VariantEntry {
    std::string id;
    std::string title;
    std::string description;
};

[[nodiscard]] std::vector<ModelEntry> build_model_entries(
    std::span<const ProviderInfo> providers);

// Model-specific thinking variants plus "off". Used by the /variant picker.
[[nodiscard]] std::vector<VariantEntry> build_variant_entries(
    const ModelInfo& model);

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
