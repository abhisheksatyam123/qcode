#pragma once

#include <string>
#include <string_view>

#include <qcode/tools/tool_catalog.h>

namespace qcode {

// ── System prompt builder ──
// Mirrors opencode's _shared/base.md pattern: identity + values + tool contract.

class SystemPrompt {
 public:
  // Default identity prompt (opencode _shared/base.md style): the
  // orchestrator, which delegates to subagents.
  static std::string default_identity();

  // Normal mode identity: one agent that does the work itself.
  static std::string normal_identity();

  // Build the full system prompt with tool descriptions injected
  static std::string build(const std::string& identity,
                           const ToolConfig& tool_cfg = {});

  // Build with default identity
  static std::string build_default(const ToolConfig& tool_cfg = {});

  // The system prompt of an agent mode (see generation/agent_mode.h):
  // normal -> normal identity, bash (+ image) only; orchestrator and
  // anything else -> default identity with the task tool.
  static std::string build_for_mode(std::string_view agent_mode, bool vision = false);
};

} // namespace qcode
