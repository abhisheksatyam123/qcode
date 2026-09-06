#pragma once

#include <qcode/providers/registry.h>
#include <qcode/providers/zen_route.h>

#include <string>
#include <string_view>

namespace qcode {

/// Supported backends the TUI and engine can resolve:
/// cursor, opencode, openrouter, antigravity.
enum class ProviderKind {
  kCursor,
  kOpenCodeZen,
  kOpenRouter,
  kAntigravity,
  kCompatible,
};

ProviderKind provider_kind(std::string_view provider_id,
                           std::string_view base_url);

struct ProviderCall {
  ProviderKind kind = ProviderKind::kCompatible;
  std::string wire_model_id;
};

/// One entry point for generation and compaction: remap the catalog id,
/// stamp protocol/path, and add Zen session headers. Callers then resolve
/// the client through ProviderRegistry.
ProviderCall prepare_provider_call(providers::ProviderOptions& options,
                                   const std::string& provider_id,
                                   std::string model_id,
                                   const std::string& session_id = {});

}  // namespace qcode
