#pragma once

#include <qcode/config/provider_info.h>
#include <qcode/core/generate_options.h>
#include <qcode/core/message.h>
#include <qcode/core/model.h>
#include <qcode/core/tool.h>

#include <string>
#include <vector>

namespace qcode {

// Single source of truth for the cacheable shape of a chat turn: the
// effective system prompt, the dispatched tool set, the request parameters
// (sampling, output budget, thinking/variant) and the replayed history.
//
// Both generation_service (live turns) and compaction (the summarizer's
// auxiliary call) must produce byte-identical values here, or the provider's
// prompt cache misses from token 0. This mirrors the DeepSeek Harness
// compaction rule: replay the conversation's own system prompt, tools and
// message prefix byte-for-byte so the auxiliary call is a genuine prefix of
// the last routed request and reuses the warm prefix cache.

// Build the wire system prompt for a turn: `system_prompt` plus the mode
// reminder and provider catalog, exactly as ChatBus assembles it.
//   is_subagent  -> raw prompt only (focused worker, no catalog)
//   otherwise    -> orchestrator reminder + catalog
std::string build_turn_system_prompt(
    const std::string& system_prompt,
    bool is_subagent,
    const std::vector<ProviderInfo>& providers,
    std::string_view current_provider_id = "",
    std::string_view current_model_id = "");

// Tool definitions dispatched for a turn.
//   enable_task_tool -> orchestrator tool set, else subagent tool set.
ToolSet build_turn_tools(bool enable_task_tool, bool vision_supported);

// Sampling defaults and output budget of a turn: temperature/top_p by model
// family, max_tokens = opencode.json max_tokens capped by limit.output.
// Fields the caller already set are kept.
void apply_turn_sampling(GenerateOptions& opts, const ModelInfo* model,
                         const Model& transform_model);

// Applies a model's opencode.json reasoning config to `opts`: the thinking
// wire form ("thinking": {type, display}) and the resolved variant
// ("variants": {"<id>": {effort, max_tokens, budget_tokens, prompt}}; the
// prompt is appended to opts.system). `requested` is the session /variant
// ("" = model default; "off" disables thinking where the model allows it).
// Without catalog info the requested id passes through as the effort.
// Anthropic drops signed thinking blocks and invalidates the message cache
// when these change, so every call that replays a turn must apply them.
void apply_variant_options(GenerateOptions& opts, const ModelInfo* model,
                           const std::string& requested,
                           const std::string& model_id);

// The conversation a turn sends: the messages after the latest compaction
// summary. The TUI also drops its role=system notes (command output, errors)
// before routing; the server replays its rows as stored.
Messages prepare_turn_history(Messages history, bool drop_system_notes);

}  // namespace qcode
