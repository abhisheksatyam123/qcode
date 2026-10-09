#pragma once

#include <qcode/config/provider_info.h>

#include <string>
#include <string_view>
#include <vector>

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

// "inherit", "parent" or "default": use the caller's own model.
bool is_inherit_model_id(std::string_view id);

// The provider and model for a subagent: `spec` is "provider:model" (or
// "provider/model", or a bare model id found in the catalog); empty or an
// inherit id means the caller's own model (default_*). A model id unknown to
// a known provider is passed through as is.
SubagentTarget resolve_subagent_target(
    std::string_view spec,
    const std::vector<ProviderInfo>& providers,
    std::string_view default_provider_id,
    std::string_view default_model_id);

}  // namespace qcode
