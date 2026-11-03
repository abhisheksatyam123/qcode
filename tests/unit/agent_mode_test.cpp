#include <qcode/core/in_process_bus.h>
#include <qcode/generation/agent_mode.h>
#include <qcode/generation/turn_prefix.h>
#include <qcode/session/system_prompt.h>
#include <qcode/ui/chat_state.h>
#include <qcode/ui/commands.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace qcode {
namespace {

TEST(AgentModeTest, OnlyNormalAndOrchestratorSurvive) {
  EXPECT_EQ(normalize_agent_mode("normal"), "normal");
  EXPECT_EQ(normalize_agent_mode("orchestrator"), "orchestrator");
  EXPECT_EQ(normalize_agent_mode("subagent"), "subagent");
  // Retired modes keep their closest behaviour.
  EXPECT_EQ(normalize_agent_mode("autonomous"), "orchestrator");
  EXPECT_EQ(normalize_agent_mode("monitor"), "orchestrator");
  EXPECT_EQ(normalize_agent_mode("plan"), "normal");
  EXPECT_EQ(normalize_agent_mode("build"), "normal");
  EXPECT_EQ(normalize_agent_mode(""), "normal");
  EXPECT_EQ(std::string(kDefaultAgentMode), "normal");
}

TEST(AgentModeTest, ToggleSwitchesBetweenTheTwoModes) {
  EXPECT_EQ(next_agent_mode("normal"), "orchestrator");
  EXPECT_EQ(next_agent_mode("orchestrator"), "normal");
  EXPECT_EQ(next_agent_mode("autonomous"), "normal");
  EXPECT_EQ(next_agent_mode("subagent"), "subagent");
  EXPECT_EQ(agent_mode_label("normal"), "Normal");
  EXPECT_EQ(agent_mode_label("orchestrator"), "Orchestrator");
  EXPECT_EQ(agent_mode_label("subagent"), "Subagent");
}

TEST(AgentModeTest, NormalModeHasNoTaskToolOrCatalog) {
  const auto tools = build_turn_tools(false, "normal", false);
  EXPECT_NE(tools.find("bash"), tools.end());
  EXPECT_EQ(tools.find("task"), tools.end());

  const auto prompt = SystemPrompt::build_for_mode("normal");
  EXPECT_EQ(prompt.find("**Task tool**"), std::string::npos);
  EXPECT_EQ(prompt.find("Lead Orchestrator"), std::string::npos);

  std::vector<ProviderInfo> providers(1);
  providers[0].id = "anthropic";
  providers[0].models.resize(1);
  providers[0].models[0].id = "claude-haiku-5-5";
  const auto wire = build_turn_system_prompt(prompt, false, providers, "anthropic",
                                             "claude-haiku-5-5", "normal");
  EXPECT_EQ(wire, prompt);
}

TEST(AgentModeTest, OrchestratorModeDelegates) {
  const auto tools = build_turn_tools(false, "orchestrator", false);
  EXPECT_NE(tools.find("task"), tools.end());
  const auto prompt = SystemPrompt::build_for_mode("orchestrator");
  EXPECT_NE(prompt.find("**Task tool**"), std::string::npos);
  const auto wire = build_turn_system_prompt(prompt, false, {}, "", "", "orchestrator");
  EXPECT_NE(wire.find("Orchestrator Mode"), std::string::npos);
  // A subagent keeps the task tool for team messages.
  const auto sub_tools = build_turn_tools(true, "normal", false);
  EXPECT_NE(sub_tools.find("task"), sub_tools.end());
}

TEST(AgentModeTest, SetAgentModeRebuildsSystemPrompt) {
  bus::BusRuntime bus;
  ChatState state;
  state.session_id = std::make_shared<std::string>("");
  std::vector<ProviderInfo> providers;
  std::string system_prompt;
  ASSERT_TRUE(set_agent_mode(state, "orchestrator", providers, 0, 0, system_prompt, bus));
  EXPECT_EQ(*state.agent_mode, "orchestrator");
  EXPECT_EQ(system_prompt, SystemPrompt::build_for_mode("orchestrator"));
  ASSERT_TRUE(set_agent_mode(state, "normal", providers, 0, 0, system_prompt, bus));
  EXPECT_EQ(system_prompt, SystemPrompt::build_for_mode("normal"));

  *state.agent_mode = "subagent";
  EXPECT_FALSE(set_agent_mode(state, "orchestrator", providers, 0, 0, system_prompt, bus));
  EXPECT_EQ(*state.agent_mode, "subagent");
}

}  // namespace
}  // namespace qcode
