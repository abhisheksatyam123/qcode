#pragma once

#include <qcode/config/provider_info.h>

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace qcode {

// Resolved subagent destination for TaskTool spawn. Generic across providers:
//   { "provider": "openrouter", "model": "deepseek/foo" }
//   { "model": "openrouter:deepseek/foo" }
//   { "model": "openrouter/deepseek/foo" }  // only if prefix is a catalog provider
struct SubagentTarget {
  const ProviderInfo* provider = nullptr;
  const ModelInfo* model_info = nullptr;
  std::string provider_id;
  std::string model_id;
  std::string error;
};

bool is_inherit_model_id(std::string_view id);

// True if prov and mod match the orchestrator provider and model (case-insensitive).
bool matches_orchestrator(
    std::string_view prov, std::string_view mod,
    std::string_view orch_prov, std::string_view orch_mod);

// Select the best alternate working model from opencode.json that differs from the orchestrator.
SubagentTarget pick_alternate_working_target(
    const std::vector<ProviderInfo>& providers,
    std::string_view orchestrator_provider_id,
    std::string_view orchestrator_model_id,
    bool allow_cursor = false);

// Pick provider+model from spawn args against the live opencode.json catalog.
// Enforces the rule that subagents must use a different model than the orchestrator.
SubagentTarget resolve_subagent_target(
    const nlohmann::json& args,
    const std::vector<ProviderInfo>& providers,
    std::string_view default_provider_id,
    std::string_view default_model_id);

}  // namespace qcode
