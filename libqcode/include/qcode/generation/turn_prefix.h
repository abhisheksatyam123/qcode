#pragma once

#include <qcode/config/provider_info.h>
#include <qcode/core/tool.h>

#include <string>
#include <vector>

namespace qcode {

// Single source of truth for the cacheable prefix segments of a chat turn:
// the effective system prompt and the dispatched tool set.
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

}  // namespace qcode
