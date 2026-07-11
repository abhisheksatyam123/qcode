#include <ai/tui/context_manager.h>
#include <sstream>

namespace ai {
namespace tui {

// ── Token estimation ─────────────────────────────────────────────
// Heuristic: ~4 chars per token (good for English + code).
// Per-message overhead: ~4 tokens for role + formatting.
static constexpr size_t kCharsPerToken = 4;
static constexpr size_t kMessageOverheadTokens = 4;

size_t estimate_tokens(const ai::Messages& messages) {
    size_t total = 0;
    for (const auto& msg : messages) {
        total += kMessageOverheadTokens;
        for (const auto& part : msg.content) {
            if (const auto* tp = std::get_if<ai::TextContentPart>(&part)) {
                total += (tp->text.size() + kCharsPerToken - 1) / kCharsPerToken;
            } else if (const auto* tcp = std::get_if<ai::ToolCallContentPart>(&part)) {
                // Tool name + arguments JSON
                total += (tcp->tool_name.size() + kCharsPerToken - 1) / kCharsPerToken;
                auto args_str = tcp->arguments.dump();
                total += (args_str.size() + kCharsPerToken - 1) / kCharsPerToken;
            } else if (const auto* trp = std::get_if<ai::ToolResultContentPart>(&part)) {
                // Result JSON — this is the big one
                auto result_str = trp->result.dump();
                total += (result_str.size() + kCharsPerToken - 1) / kCharsPerToken;
            } else if (const auto* rp = std::get_if<ai::ReasoningContentPart>(&part)) {
                total += (rp->text.size() + kCharsPerToken - 1) / kCharsPerToken;
            }
        }
    }
    return total;
}

size_t estimate_system_tokens(const std::string& system_prompt) {
    return (system_prompt.size() + kCharsPerToken - 1) / kCharsPerToken;
}

// ── Context pruning ──────────────────────────────────────────────
// Replace old tool results with compact placeholders to free context.
// The last `keep_recent` messages are never touched.
size_t prune_context(ai::Messages& messages,
                     size_t context_window,
                     size_t keep_recent) {
    if (context_window == 0 || messages.size() <= keep_recent) return 0;

    // Only prune if we're above 80% of the context window
    size_t tokens = estimate_tokens(messages);
    size_t budget = (context_window * 4) / 5;  // 80%
    if (tokens <= budget) return 0;

    size_t pruned = 0;
    // Walk messages from oldest, skip the last keep_recent
    size_t prune_limit = messages.size() - keep_recent;
    for (size_t i = 0; i < prune_limit; ++i) {
        auto& msg = messages[i];
        for (auto& part : msg.content) {
            if (auto* trp = std::get_if<ai::ToolResultContentPart>(&part)) {
                // Replace large results with a compact placeholder
                auto result_str = trp->result.dump();
                if (result_str.size() > 200) {
                    trp->result = "[Pruned: " + result_str.substr(0, 80) + "...]";
                    ++pruned;
                }
            }
        }
    }
    return pruned;
}

} // namespace tui
} // namespace ai
