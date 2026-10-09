#pragma once
#include <atomic>
#include <functional>
#include <memory>

#include <qcode/core/bus_port.h>
#include <qcode/config/provider_info.h>
#include <qcode/core/client.h>
#include <string>
#include <vector>

namespace qcode {

// ── Bus-aware generation entry points ────────────────────────────────
// Callers pass a BusPort& (not ChatState) and receive typed events.
// The UI subscribes to those events.

/**
 * Minimal context needed by the generation backend.
 * Replaces direct ChatState& dependency with only the fields
 * the backend actually needs: session_id and reasoning_mode.
 */
struct GenerationContext {
    std::string session_id;
    std::string reasoning_mode = "off";
    // Agent mode: "orchestrator" (default) or "subagent" (delegated worker).
    std::string agent_mode = "orchestrator";
    std::string workspace;
    std::shared_ptr<std::atomic<bool>> abort_flag = std::make_shared<std::atomic<bool>>(false);

    // Tool observability counters (updated by backend during generation)
    int tool_call_count = 0;
    double total_tool_time_ms = 0.0;

    // True when the user queued a prompt during this turn. Only streaming /
    // server-side agent providers use it, to end their turn early.
    std::function<bool()> has_queued_work = nullptr;

    // Drains prompts the user queued during this turn. The tool loop folds
    // them into its next model request (one user message) and publishes
    // contract::UserMessageInjected, so a queued prompt steers the running
    // turn instead of waiting for it to finish. Return {} when there is none.
    std::function<std::vector<std::string>()> take_queued_prompts = nullptr;

    // Caller's (calibrated) estimate of the first request's context size;
    // 0 = the tool loop estimates it. Drives auto-compaction at step 0.
    size_t context_tokens_hint = 0;
};

/**
 * GenerationService: high-level interface for LLM generation.
 * Wraps bus + providers and exposes a clean run_generation method.
 */
class GenerationService {
public:
    GenerationService(bus::BusPort& bus, const std::vector<ProviderInfo>& providers)
        : bus_(bus), providers_(providers) {}

    /**
     * Run LLM generation with tools, emitting bus events.
     */
    void run_generation(
        const std::string& provider_name,
        const std::string& model_id,
        const std::string& system_prompt,
        qcode::Messages messages,
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
    qcode::Messages messages,
    bool enable_tools,
    const std::vector<ProviderInfo>& providers,
    bus::BusPort& bus,
    GenerationContext& ctx);

} // namespace qcode
