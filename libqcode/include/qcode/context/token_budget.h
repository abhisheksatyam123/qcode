#pragma once
#include <qcode/types/message.h>
#include <string>

namespace qcode {

// Estimate token count from a messages vector.
// Uses chars÷4 heuristic + per-message overhead.
size_t estimate_tokens(const qcode::Messages& messages);

// Estimate tokens for the system prompt text.
size_t estimate_system_tokens(const std::string& system_prompt);

// Returns a pruned COPY that fits within the context budget.
// The original messages are never modified.
// Escalating passes: tool results → reasoning → assistant text → drop old messages.
qcode::Messages prune_context(const qcode::Messages& messages,
                           size_t context_window,
                           size_t keep_recent = 8);

// Calibrate a heuristic estimate against the API's actual token count.
// Uses the ratio actual/estimated from the previous generation and applies
// it to the current estimate, clamped to sane bounds. This corrects for
// JSON-heavy tool results and uncounted tool schemas.
// Returns `heuristic` unchanged if no calibration data is available yet.
size_t calibrate_estimate(size_t heuristic,
                          size_t last_actual,
                          size_t last_estimated);

} // namespace qcode
