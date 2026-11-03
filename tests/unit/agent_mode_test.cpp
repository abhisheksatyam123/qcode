// Interface-level checks for the two agent modes: the /mode command, the
// mode setter the TUI uses, and the tool set a normal session exposes.
#include <qcode/core/in_process_bus.h>
#include <qcode/tools/tool_catalog.h>
#include <qcode/ui/chat_state.h>
#include <qcode/ui/commands.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace qcode {
namespace {

TEST(AgentModeTest, NormalToolSetHasNoTaskTool) {
  const auto tools = ToolCatalog::build_definitions(ToolConfig::normal());
  EXPECT_NE(tools.find("bash"), tools.end());
  EXPECT_EQ(tools.find("task"), tools.end());
}

TEST(AgentModeTest, ModeCommandSwitchesBetweenTheTwoModes) {
  bus::BusRuntime bus;
  ChatState state;
  state.session_id = std::make_shared<std::string>("");
  std::vector<ProviderInfo> providers;
  std::string prompt_input;
  int selected_provider = 0;
  int selected_model = 0;
  bool enable_tools = true;
  std::string system_prompt;
  auto compaction_thread = std::make_shared<compat::jthread>();

  handle_slash_command("/mode orchestrator", prompt_input, providers, selected_provider,
                       selected_model, enable_tools, system_prompt, state,
                       compaction_thread, bus);
  EXPECT_EQ(*state.agent_mode, "orchestrator");

  handle_slash_command("/mode normal", prompt_input, providers, selected_provider,
                       selected_model, enable_tools, system_prompt, state,
                       compaction_thread, bus);
  EXPECT_EQ(*state.agent_mode, "normal");
}

TEST(AgentModeTest, SubagentSessionKeepsItsRole) {
  bus::BusRuntime bus;
  ChatState state;
  state.session_id = std::make_shared<std::string>("");
  *state.agent_mode = "subagent";
  std::vector<ProviderInfo> providers;
  std::string system_prompt;
  EXPECT_FALSE(set_agent_mode(state, "orchestrator", providers, 0, 0, system_prompt, bus));
  EXPECT_EQ(*state.agent_mode, "subagent");
}

}  // namespace
}  // namespace qcode
