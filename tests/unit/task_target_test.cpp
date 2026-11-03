#include <qcode/tools/task_target.h>
#include <qcode/tools/task_tool.h>

#include <qcode/config/config.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

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

  // Delegation targets are tool-calling models (is_model_working).
  for (auto* provider : {&cursor, &openrouter, &zen, &antigravity}) {
    for (auto& model : provider->models) model.tool_call = true;
  }
  return {cursor, openrouter, zen, antigravity};
}

TEST(TaskTargetTest, ColonFormSelectsProviderEvenWhenModelIdHasSlashes) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      "openrouter:deepseek/deepseek-v4-flash-0731",
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  ASSERT_NE(t.provider, nullptr);
  EXPECT_EQ(t.provider_id, "openrouter");
  EXPECT_EQ(t.model_id, "deepseek/deepseek-v4-flash-0731");
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
        model, catalog, "cursor", "cursor-grok-4.6");
    EXPECT_TRUE(t.error.empty()) << model << ": " << t.error;
    EXPECT_EQ(t.provider_id, provider) << model;
  }
}

TEST(TaskTargetTest, SlashSplitsOnlyWhenPrefixIsAProvider) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      "deepseek/deepseek-v4-flash-0731",
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "openrouter");
  EXPECT_EQ(t.model_id, "deepseek/deepseek-v4-flash-0731");
}

TEST(TaskTargetTest, ColonInsideModelIdIsNotAProvider) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      "nvidia/nemotron-3.5-lightning:free",
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "openrouter");
  EXPECT_EQ(t.model_id, "nvidia/nemotron-3.5-lightning:free");
}

TEST(TaskTargetTest, ProviderSlashModelStillWorks) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      "openrouter/nvidia/nemotron-3.5-lightning:free",
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "openrouter");
  EXPECT_EQ(t.model_id, "nvidia/nemotron-3.5-lightning:free");
}

TEST(TaskTargetTest, EmptyOrInheritMeansTheCallersOwnModel) {
  const auto catalog = sample_catalog();
  for (const char* spec : {"", "inherit"}) {
    const auto t = resolve_subagent_target(spec, catalog, "opencode", "big-pickle");
    EXPECT_TRUE(t.error.empty()) << t.error;
    EXPECT_EQ(t.provider_id, "opencode");
    EXPECT_EQ(t.model_id, "big-pickle");
  }
}

TEST(TaskTargetTest, TheCallersOwnModelCanBeNamed) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target("opencode:big-pickle", catalog, "opencode", "big-pickle");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id + ":" + t.model_id, "opencode:big-pickle");
}

TEST(TaskTargetTest, UnknownProviderIsAnError) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      "openai:gpt-4.1",
      catalog, "cursor", "cursor-grok-4.6");
  EXPECT_EQ(t.provider, nullptr);
  EXPECT_THAT(t.error, testing::HasSubstr("Unknown model"));
  EXPECT_THAT(t.error, testing::HasSubstr("opencode.json"));
  EXPECT_THAT(t.error, testing::HasSubstr("`cursor:cursor-grok-4.6`"));
  EXPECT_THAT(t.error, testing::HasSubstr("`openrouter:deepseek/deepseek-v4-flash-0731`"));
}

TEST(TaskTargetTest, CursorEffortSlugResolvesToCatalogPickerId) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      "cursor-grok-4.6-medium",
      catalog, "opencode", "big-pickle");
  EXPECT_TRUE(t.error.empty()) << t.error;
  ASSERT_NE(t.provider, nullptr);
  EXPECT_EQ(t.provider_id, "cursor");
  EXPECT_EQ(t.model_id, "cursor-grok-4.6");
}

TEST(TaskTargetTest, OlderCursorGrokSlugFallsBackToCatalogGrok) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      "cursor-grok-4.5-high",
      catalog, "opencode", "big-pickle");
  EXPECT_TRUE(t.error.empty()) << t.error;
  ASSERT_NE(t.provider, nullptr);
  EXPECT_EQ(t.provider_id, "cursor");
  EXPECT_EQ(t.model_id, "cursor-grok-4.6");
}

TEST(TaskTargetTest, UnknownModelListsCatalogFromOpencodeJson) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      "totally-unknown-model-xyz",
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

TEST(TaskTargetTest, ExplicitCursorStillBinds) {
  const auto catalog = sample_catalog();
  const auto t = resolve_subagent_target(
      "cursor:cursor-grok-4.6",
      catalog, "opencode", "big-pickle");
  EXPECT_TRUE(t.error.empty()) << t.error;
  EXPECT_EQ(t.provider_id, "cursor");
}

std::vector<ProviderInfo> haiku_catalog() {
  ProviderInfo go;
  go.id = "mockgo";
  for (const char* id : {"claude-3-5-haiku-20241022", "claude-haiku-5-5",
                         "claude-haiku-4-5", "claude-opus-5-5"}) {
    ModelInfo m;
    m.id = id;
    m.tool_call = true;
    go.models.push_back(m);
  }
  ProviderInfo other;
  other.id = "openrouter";
  ModelInfo h;
  h.id = "anthropic/claude-haiku-6";
  h.tool_call = true;
  other.models.push_back(h);
  ModelInfo no_tools;
  no_tools.id = "anthropic/claude-haiku-9";  // cannot run a tool loop
  other.models.push_back(no_tools);
  return {go, other};
}

TEST(SubagentDefaultModelTest, PicksNewestHaikuOnLeadProviderFirst) {
  const auto providers = haiku_catalog();
  EXPECT_EQ(newest_haiku_model(providers, "mockgo"), "mockgo:claude-haiku-5-5");
  // No preference: newest anywhere, tool-calling only.
  EXPECT_EQ(newest_haiku_model(providers), "openrouter:anthropic/claude-haiku-6");
  EXPECT_EQ(default_subagent_model(providers, "mockgo", "claude-opus-5-5",
                                   std::string()),
            "mockgo:claude-haiku-5-5");
}

TEST(SubagentDefaultModelTest, ConfiguredSubagentModelWins) {
  const auto providers = haiku_catalog();
  EXPECT_EQ(default_subagent_model(providers, "mockgo", "claude-opus-5-5",
                                   std::string("mockgo/claude-haiku-4-5")),
            "mockgo:claude-haiku-4-5");
  // "inherit" keeps the lead's model.
  EXPECT_EQ(default_subagent_model(providers, "mockgo", "claude-opus-5-5",
                                   std::string("inherit")),
            "");
  // Unknown configured model falls back to the Haiku default.
  EXPECT_EQ(default_subagent_model(providers, "mockgo", "claude-opus-5-5",
                                   std::string("nope:missing")),
            "mockgo:claude-haiku-5-5");
}

TEST(SubagentDefaultModelTest, NoHaikuMeansLeadModel) {
  EXPECT_EQ(default_subagent_model(sample_catalog(), "cursor", "cursor-grok-4.6",
                                   std::string()),
            "");
}

TEST(SubagentDefaultModelTest, ReadsSubagentModelFromConfigAndCatalogNamesIt) {
  const auto path = std::filesystem::temp_directory_path() / "qcode_subagent_model_test.json";
  {
    std::ofstream out(path);
    out << R"({"subagent_model": "mockgo/claude-haiku-4-5"})";
  }
  const char* old = std::getenv("OPENCODE_CONFIG");
  const std::string saved = old ? old : "";
  setenv("OPENCODE_CONFIG", path.c_str(), 1);
  EXPECT_EQ(config_subagent_model(), "mockgo/claude-haiku-4-5");
  const auto providers = haiku_catalog();
  EXPECT_EQ(default_subagent_model(providers, "mockgo", "claude-opus-5-5"),
            "mockgo:claude-haiku-4-5");
  const auto catalog =
      format_provider_catalog_for_prompt(providers, "mockgo", "claude-opus-5-5");
  EXPECT_NE(catalog.find("delegation default `mockgo:claude-haiku-4-5`"),
            std::string::npos);
  if (old) {
    setenv("OPENCODE_CONFIG", saved.c_str(), 1);
  } else {
    unsetenv("OPENCODE_CONFIG");
  }
  std::filesystem::remove(path);
}

}  // namespace
}  // namespace qcode
