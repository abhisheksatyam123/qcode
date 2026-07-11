#pragma once
#include <ai/types/message.h>
#include <string>

namespace ai {
namespace tui {

// Estimate token count from a messages vector.
// Uses chars÷4 heuristic + per-message overhead.
size_t estimate_tokens(const ai::Messages& messages);

// Estimate tokens for the system prompt text.
size_t estimate_system_tokens(const std::string& system_prompt);

// Returns a pruned COPY that fits within the context budget.
// The original messages are never modified.
// Escalating passes: tool results → reasoning → assistant text → drop old messages.
ai::Messages prune_context(const ai::Messages& messages,
                           size_t context_window,
                           size_t keep_recent = 8);

} // namespace tui
} // namespace ai
