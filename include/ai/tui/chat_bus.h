#pragma once

#include <ai/tui/bus/port.h>
#include <ai/tui/state.h>
#include <ai/types/client.h>
#include <string>
#include <vector>

namespace ai {
namespace tui {

// ── Bus-aware generation entry points ────────────────────────────────────────
// These replace the old run_llm_generation / run_stream_generation from chat.h.
// Instead of taking a ScreenInteractive* and ChatState, they take a BusPort&
// and emit typed events. The UI subscribes to those events.

struct ProviderInfo;

/**
 * Run LLM generation with tools, emitting bus events for:
 *   - MessageDelta    (streaming text)
 *   - ToolCallStarted / ToolCallCompleted
 *   - SessionStatusChanged
 *   - TokenUsageUpdated
 *   - ErrorOccurred
 */
void run_generation_with_bus(
    const std::string& provider_name,
    const std::string& model_id,
    const std::string& system_prompt,
    const ai::Messages& messages,
    bool enable_tools,
    const std::vector<ProviderInfo>& providers,
    bus::BusPort& bus,
    ChatState& state);

} // namespace tui
} // namespace ai
