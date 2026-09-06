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

// Pick provider+model from spawn args against the live opencode.json catalog.
// Unlisted model ids are still accepted when the provider is known (wire passthrough).
SubagentTarget resolve_subagent_target(
    const nlohmann::json& args,
    const std::vector<ProviderInfo>& providers,
    std::string_view default_provider_id,
    std::string_view default_model_id);

}  // namespace qcode
