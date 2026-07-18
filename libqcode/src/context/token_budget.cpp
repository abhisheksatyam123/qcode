#include <qcode/context/token_budget.h>
#include <sstream>

namespace qcode {

// ── Token estimation ─────────────────────────────────────────────
// Heuristic: ~4 chars per token (good for English + code).
// Per-message overhead: ~4 tokens for role + formatting.
static constexpr size_t kCharsPerToken = 4;
static constexpr size_t kMessageOverheadTokens = 4;

size_t estimate_tokens(const qcode::Messages& messages) {
    size_t total = 0;
    for (const auto& msg : messages) {
        total += kMessageOverheadTokens;
        for (const auto& part : msg.content) {
            if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                total += (tp->text.size() + kCharsPerToken - 1) / kCharsPerToken;
            } else if (const auto* tcp = std::get_if<qcode::ToolCallContentPart>(&part)) {
                total += (tcp->tool_name.size() + kCharsPerToken - 1) / kCharsPerToken;
                auto args_str = tcp->arguments.dump();
                total += (args_str.size() + kCharsPerToken - 1) / kCharsPerToken;
            } else if (const auto* trp = std::get_if<qcode::ToolResultContentPart>(&part)) {
                auto result_str = trp->result.dump();
                total += (result_str.size() + kCharsPerToken - 1) / kCharsPerToken;
            } else if (const auto* rp = std::get_if<qcode::ReasoningContentPart>(&part)) {
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
qcode::Messages prune_context(const qcode::Messages& messages,
                           size_t context_window,
                           size_t keep_recent) {
    if (context_window == 0 || messages.size() <= keep_recent)
        return messages;

    qcode::Messages result = messages;
    size_t budget = (context_window * 4) / 5;  // 80%

    auto estimate = [&]() -> size_t {
        return estimate_tokens(result);
    };

    if (estimate() <= budget) return result;

    size_t prune_limit = result.size() - keep_recent;

    // ── Pass 1: Prune large tool results ──
    for (size_t i = 0; i < prune_limit; ++i) {
        for (auto& part : result[i].content) {
            if (auto* trp = std::get_if<qcode::ToolResultContentPart>(&part)) {
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
            if (auto* rp = std::get_if<qcode::ReasoningContentPart>(&part)) {
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
        if (result[i].role == qcode::kMessageRoleAssistant) {
            for (auto& part : result[i].content) {
                if (auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
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
        if (result[i].role == qcode::kMessageRoleAssistant) {
            result[i].content = {qcode::TextContentPart{"[Older messages pruned]"}};
        }
        if (estimate() <= budget) break;
    }

    return result;
}

} // namespace qcode

// ── Calibration ──────────────────────────────────────────────────
namespace qcode {

size_t calibrate_estimate(size_t heuristic,
                          size_t last_actual,
                          size_t last_estimated) {
    if (last_actual == 0 || last_estimated == 0) return heuristic;
    double ratio = (double)last_actual / (double)last_estimated;
    if (ratio < 0.5) ratio = 0.5;
    if (ratio > 2.0) ratio = 2.0;
    return (size_t)(heuristic * ratio);
}

} // namespace qcode
