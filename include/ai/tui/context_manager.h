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

// Auto-prune old tool results when context is approaching the model's limit.
// Returns the number of messages pruned (0 if nothing was pruned).
// Strategy: replace old ToolResultContentParts with a compact placeholder,
// keeping the most recent `keep_recent` messages intact.
size_t prune_context(ai::Messages& messages,
                     size_t context_window,
                     size_t keep_recent = 8);

} // namespace tui
} // namespace ai
