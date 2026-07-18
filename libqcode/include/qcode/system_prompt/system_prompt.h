#pragma once

#include <string>

#include <qcode/tools/tool_catalog.h>

namespace ai {
namespace tui {

// ── System prompt builder ──
// Mirrors opencode's _shared/base.md pattern: identity + values + tool contract.

class SystemPrompt {
 public:
  // Default identity prompt (opencode _shared/base.md style)
  static std::string default_identity();

  // Build the full system prompt with tool descriptions injected
  static std::string build(const std::string& identity,
                           const ToolConfig& tool_cfg = {});

  // Build with default identity
  static std::string build_default(const ToolConfig& tool_cfg = {});
};

} // namespace tui
} // namespace ai
