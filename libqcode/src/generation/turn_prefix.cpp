#include <qcode/generation/turn_prefix.h>

#include <qcode/generation/generation_continue.h>
#include <qcode/tools/tool_catalog.h>

namespace qcode {

std::string build_turn_system_prompt(
    const std::string& system_prompt,
    bool is_subagent,
    const std::vector<ProviderInfo>& providers,
    std::string_view current_provider_id,
    std::string_view current_model_id) {
  std::string out = system_prompt;
  if (is_subagent) return out;
  out += std::string(kOrchestratorReminder);
  const std::string catalog_section = format_provider_catalog_for_prompt(
      providers, current_provider_id, current_model_id);
  if (!catalog_section.empty()) {
    out += "\n\n" + catalog_section;
  }
  return out;
}

ToolSet build_turn_tools(bool enable_task_tool, bool vision_supported) {
  const auto cfg = enable_task_tool ? ToolConfig::orchestrator(vision_supported)
                                    : ToolConfig::subagent(vision_supported);
  return ToolCatalog::build_definitions(cfg);
}

}  // namespace qcode
