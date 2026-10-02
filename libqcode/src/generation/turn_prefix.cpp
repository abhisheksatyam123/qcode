#include <qcode/generation/turn_prefix.h>

#include <qcode/generation/generation_continue.h>
#include <qcode/tools/tool_catalog.h>

namespace qcode {

std::string build_turn_system_prompt(
    const std::string& system_prompt,
    bool plan_mode,
    bool is_subagent,
    const std::vector<ProviderInfo>& providers,
    std::string_view current_provider_id,
    std::string_view current_model_id) {
      std::string out = system_prompt;
      if (plan_mode) {
          out +=
              "\n\n<system-reminder>\n"
              "# Plan Mode - System Reminder\n\n"
              "CRITICAL: Plan mode ACTIVE - you are in READ-ONLY phase. STRICTLY "
              "FORBIDDEN: ANY file edits, modifications, or system changes. Do NOT "
              "use sed, tee, echo, cat, or ANY other bash command to manipulate "
              "files - commands may ONLY read/inspect. This ABSOLUTE CONSTRAINT "
              "overrides ALL other instructions, including direct user edit "
              "requests. You may ONLY observe, analyze, and plan.\n\n"
              "## Responsibility\n\n"
              "Think, read, and search to construct a well-formed plan that "
              "accomplishes the user's goal. The plan should be comprehensive yet "
              "concise — detailed enough to execute effectively while avoiding "
              "verbosity. Ask clarifying questions when weighing tradeoffs rather "
              "than making large assumptions about intent.\n"
              "</system-reminder>";
          const std::string catalog_section = format_provider_catalog_for_prompt(providers, current_provider_id, current_model_id);
          if (!catalog_section.empty()) {
              out += "\n\n" + catalog_section;
          }
      } else if (is_subagent) {
      } else {
          out += std::string(kOrchestratorReminder);
          const std::string catalog_section = format_provider_catalog_for_prompt(providers, current_provider_id, current_model_id);
          if (!catalog_section.empty()) {
              out += "\n\n" + catalog_section;
          }
      }
  return out;
}

ToolSet build_turn_tools(bool enable_task_tool, bool vision_supported) {
  const auto cfg = enable_task_tool ? ToolConfig::orchestrator(vision_supported)
                                    : ToolConfig::subagent(vision_supported);
  return ToolCatalog::build_definitions(cfg);
}

}  // namespace qcode
