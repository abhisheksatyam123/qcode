#include <qcode/tools/task_target.h>
#include <qcode/tools/task_tool.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace qcode {
namespace {

std::vector<ProviderInfo> sample_catalog() {
  ProviderInfo cursor;
  cursor.id = "cursor";
  cursor.name = "Cursor";
  ModelInfo grok;
  grok.id = "cursor-grok-4.6";
  grok.name = "Grok 4.6";
  cursor.models.push_back(grok);

  ProviderInfo openrouter;
  openrouter.id = "openrouter";
  openrouter.name = "OpenRouter";
  ModelInfo ds;
  ds.id = "deepseek/deepseek-v4-flash-0731";
  ds.name = "DeepSeek V4 Flash";
  openrouter.models.push_back(ds);
  ModelInfo nemo;
  nemo.id = "nvidia/nemotron-3.5-lightning:free";
  openrouter.models.push_back(nemo);

  ProviderInfo zen;
  zen.id = "opencode";
  zen.name = "OpenCode Zen";
  ModelInfo pickle;
  pickle.id = "big-pickle";
  zen.models.push_back(pickle);

  ProviderInfo antigravity;
  antigravity.id = "antigravity";
  antigravity.name = "Antigravity";
  ModelInfo gemini;
  gemini.id = "gemini-3.1-pro";
  antigravity.models.push_back(gemini);

  return {cursor, openrouter, zen, antigravity};
}

TEST(TaskTargetTest, ColonFormSelectsProviderEvenWhenModelIdHasSlashes) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "openrouter:deepseek/deepseek-v4-flash-0731"}},
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  ASSERT_NE(t.provider, nullptr);
  EXPECT_EQ(t.provider_id, "openrouter");
  EXPECT_EQ(t.model_id, "deepseek/deepseek-v4-flash-0731");
}

TEST(TaskTargetTest, ExplicitProviderAndModelFields) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"provider", "opencode"}, {"model", "big-pickle"}},
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "opencode");
  EXPECT_EQ(t.model_id, "big-pickle");
}

TEST(TaskTargetTest, OrchestratorCanSelectEveryConfiguredProvider) {
  const auto catalog = sample_catalog();
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"opencode:big-pickle", "opencode"},
      {"openrouter:deepseek/deepseek-v4-flash-0731", "openrouter"},
      {"antigravity:gemini-3.1-pro", "antigravity"},
  };
  for (const auto& [model, provider] : cases) {
    const auto t = resolve_subagent_target(
        nlohmann::json{{"model", model}}, catalog, "cursor", "cursor-grok-4.6");
    EXPECT_TRUE(t.error.empty()) << model << ": " << t.error;
    EXPECT_EQ(t.provider_id, provider) << model;
  }
}

TEST(TaskTargetTest, SlashSplitsOnlyWhenPrefixIsAProvider) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "deepseek/deepseek-v4-flash-0731"}},
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "openrouter");
  EXPECT_EQ(t.model_id, "deepseek/deepseek-v4-flash-0731");
}

TEST(TaskTargetTest, ColonInsideModelIdIsNotAProvider) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "nvidia/nemotron-3.5-lightning:free"}},
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "openrouter");
  EXPECT_EQ(t.model_id, "nvidia/nemotron-3.5-lightning:free");
}

TEST(TaskTargetTest, ProviderSlashModelStillWorks) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "openrouter/nvidia/nemotron-3.5-lightning:free"}},
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "openrouter");
  EXPECT_EQ(t.model_id, "nvidia/nemotron-3.5-lightning:free");
}

TEST(TaskTargetTest, InheritOrSameModelSteersToAlternateWorkingModel) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "inherit"}}, catalog, "opencode", "big-pickle");
  EXPECT_TRUE(t.error.empty()) << t.error;
  // Subagents must NOT use the orchestrator's provider:model
  EXPECT_NE(t.provider_id + ":" + t.model_id, "opencode:big-pickle");
  EXPECT_EQ(t.provider_id, "antigravity");
  EXPECT_EQ(t.model_id, "gemini-3.1-pro");
}

TEST(TaskTargetTest, OrchestratorModelIsAvoidedForSubagent) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "opencode:big-pickle"}}, catalog, "opencode", "big-pickle");
  EXPECT_TRUE(t.error.empty()) << t.error;
  // Steers away from orchestrator's own model
  EXPECT_NE(t.provider_id + ":" + t.model_id, "opencode:big-pickle");
}

TEST(TaskTargetTest, UnknownProviderIsAnError) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"provider", "openai"}, {"model", "gpt-4.1"}},
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_EQ(t.provider, nullptr);
  EXPECT_THAT(t.error, testing::HasSubstr("Unknown provider"));
  EXPECT_THAT(t.error, testing::HasSubstr("opencode.json"));
  EXPECT_THAT(t.error, testing::HasSubstr("`cursor:cursor-grok-4.6`"));
  EXPECT_THAT(t.error, testing::HasSubstr("`openrouter:deepseek/deepseek-v4-flash-0731`"));
}

TEST(TaskTargetTest, CursorEffortSlugResolvesToCatalogPickerId) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "cursor-grok-4.6-medium"}},
      catalog, "opencode", "big-pickle");
  EXPECT_TRUE(t.error.empty()) << t.error;
  ASSERT_NE(t.provider, nullptr);
  EXPECT_EQ(t.provider_id, "cursor");
  EXPECT_EQ(t.model_id, "cursor-grok-4.6");
}

TEST(TaskTargetTest, OlderCursorGrokSlugFallsBackToCatalogGrok) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "cursor-grok-4.5-high"}},
      catalog, "opencode", "big-pickle");
  EXPECT_TRUE(t.error.empty()) << t.error;
  ASSERT_NE(t.provider, nullptr);
  EXPECT_EQ(t.provider_id, "cursor");
  EXPECT_EQ(t.model_id, "cursor-grok-4.6");
}

TEST(TaskTargetTest, UnknownModelListsCatalogFromOpencodeJson) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "totally-unknown-model-xyz"}},
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_EQ(t.provider, nullptr);
  EXPECT_THAT(t.error, testing::HasSubstr("Unknown model"));
  EXPECT_THAT(t.error, testing::HasSubstr("totally-unknown-model-xyz"));
  EXPECT_THAT(t.error, testing::HasSubstr("e.g. cursor:cursor-grok-4.6"));
  EXPECT_THAT(t.error, testing::HasSubstr("Available models from opencode.json"));
  EXPECT_THAT(t.error, testing::HasSubstr("`cursor:cursor-grok-4.6`"));
  EXPECT_THAT(t.error, testing::HasSubstr("`opencode:big-pickle`"));
  EXPECT_THAT(t.error, testing::Not(testing::HasSubstr("openrouter:deepseek/foo")));
}

TEST(TaskTargetTest, ModelsFallbackList) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"models", nlohmann::json::array({"missing/x", "opencode:big-pickle"})}},
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "opencode");
  EXPECT_EQ(t.model_id, "big-pickle");
}

TEST(TaskTargetTest, SpawnNormalizesColonModelIntoProvider) {
  ToolExecutionContext context;
  context.subagent_runner = [](const nlohmann::json& args,
                               std::shared_ptr<std::atomic<bool>>) {
    EXPECT_EQ(args.value("provider", ""), "openrouter");
    EXPECT_EQ(args.value("model", ""), "deepseek/foo");
    return nlohmann::json{{"output", "ok"}};
  };
  const auto out = TaskTool::execute(
      nlohmann::json{{"prompt", "scan"},
                     {"model", "openrouter:deepseek/foo"}},
      context);
  EXPECT_EQ(out["metadata"].value("provider", ""), "openrouter");
  EXPECT_EQ(out["metadata"].value("model", ""), "deepseek/foo");
  EXPECT_THAT(out.value("output", ""), testing::HasSubstr("ok"));
}


TEST(TaskTargetTest, ExplicitCursorStillBinds) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      nlohmann::json{{"model", "cursor:cursor-grok-4.6"}},
      catalog, "opencode", "big-pickle");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "cursor");
}

}  // namespace
}  // namespace qcode
