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
                total += (tcp->tool_name.size() + kCharsPerToken - 1) / kCharsPerToken;
                auto args_str = tcp->arguments.dump();
                total += (args_str.size() + kCharsPerToken - 1) / kCharsPerToken;
            } else if (const auto* trp = std::get_if<ai::ToolResultContentPart>(&part)) {
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
// Prune a COPY of the messages to fit within the context budget.
// Returns a pruned copy; the original is never modified.
//
// Strategy (applied in escalating passes until under budget):
//   Pass 1: Replace old ToolResult JSON >200 chars with compact placeholder
//   Pass 2: Strip old ReasoningContentPart text entirely
//   Pass 3: Replace old assistant text with "[Earlier response pruned]"
// Messages in the last `keep_recent` window are always preserved.
ai::Messages prune_context(const ai::Messages& messages,
                           size_t context_window,
                           size_t keep_recent) {
    if (context_window == 0 || messages.size() <= keep_recent)
        return messages;

    ai::Messages result = messages;
    size_t budget = (context_window * 4) / 5;  // 80%

    auto estimate = [&]() -> size_t {
        return estimate_tokens(result);
    };

    if (estimate() <= budget) return result;

    size_t prune_limit = result.size() - keep_recent;

    // ── Pass 1: Prune large tool results ──
    for (size_t i = 0; i < prune_limit; ++i) {
        for (auto& part : result[i].content) {
            if (auto* trp = std::get_if<ai::ToolResultContentPart>(&part)) {
                auto result_str = trp->result.dump();
                if (result_str.size() > 200) {
                    trp->result = "[Pruned: " + result_str.substr(0, 80) + "...]";
                }
            }
        }
    }
    if (estimate() <= budget) return result;

    // ── Pass 2: Strip old reasoning content ──
    for (size_t i = 0; i < prune_limit; ++i) {
        for (auto& part : result[i].content) {
            if (auto* rp = std::get_if<ai::ReasoningContentPart>(&part)) {
                if (!rp->text.empty()) {
                    rp->text = "[Reasoning pruned]";
                    rp->signature.clear();
                }
            }
        }
    }
    if (estimate() <= budget) return result;

    // ── Pass 3: Replace old assistant text with summary placeholder ──
    for (size_t i = 0; i < prune_limit; ++i) {
        if (result[i].role == ai::kMessageRoleAssistant) {
            for (auto& part : result[i].content) {
                if (auto* tp = std::get_if<ai::TextContentPart>(&part)) {
                    if (tp->text.size() > 100) {
                        tp->text = "[Earlier response pruned]";
                    }
                }
            }
        }
    }
    if (estimate() <= budget) return result;

    // ── Pass 4: Drop old assistant messages entirely (keep role marker) ──
    for (size_t i = 0; i < prune_limit; ++i) {
        if (result[i].role == ai::kMessageRoleAssistant) {
            result[i].content = {ai::TextContentPart{"[Older messages pruned]"}};
        }
        if (estimate() <= budget) break;
    }

    return result;
}

} // namespace tui
} // namespace ai
