#pragma once
#include <atomic>
#include <memory>

#include <ai/tui/bus/port.h>
#include <ai/tui/state.h>
#include <ai/types/client.h>
#include <string>
#include <vector>

namespace ai {
namespace tui {

// ── Bus-aware generation entry points ────────────────────────────────
// These replace the old run_llm_generation / run_stream_generation from chat.h.
// Instead of taking a ScreenInteractive* and ChatState, they take a BusPort&
// and emit typed events. The UI subscribes to those events.

struct ProviderInfo;

/**
 * Minimal context needed by the generation backend.
 * Replaces direct ChatState& dependency with only the fields
 * the backend actually needs: session_id and reasoning_mode.
 */
struct GenerationContext {
    std::string session_id;
    std::string reasoning_mode = "off";
    std::string workspace;
    std::shared_ptr<std::atomic<bool>> abort_flag = std::make_shared<std::atomic<bool>>(false);

    // Tool observability counters (updated by backend during generation)
    int tool_call_count = 0;
    double total_tool_time_ms = 0.0;
};


/**
 * BackendService: high-level interface for LLM generation.
 * Wraps bus + providers and exposes a clean run_generation method.
 */
class BackendService {
public:
    BackendService(bus::BusPort& bus, const std::vector<ProviderInfo>& providers)
        : bus_(bus), providers_(providers) {}

    /**
     * Run LLM generation with tools, emitting bus events.
     */
    void run_generation(
        const std::string& provider_name,
        const std::string& model_id,
        const std::string& system_prompt,
        const ai::Messages& messages,
        bool enable_tools,
        GenerationContext& ctx);

private:
    bus::BusPort& bus_;
    const std::vector<ProviderInfo>& providers_;
};

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
    GenerationContext& ctx);

} // namespace tui
} // namespace ai
