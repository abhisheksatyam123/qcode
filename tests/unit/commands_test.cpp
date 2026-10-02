#include <qcode/ui/commands.h>
#include <qcode/config/provider_info.h>
#include <picker_helpers.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/session/session_store.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <vector>

namespace qcode {
namespace {

TEST(CommandsTest, BuiltinSlashCommandsIncludeModel) {
    auto cmds = builtin_slash_commands();
    ASSERT_FALSE(cmds.empty());
    bool has_model = false;
    for (const auto& c : cmds) {
        if (c.name == "model") has_model = true;
    }
    EXPECT_TRUE(has_model);
}

TEST(CommandsTest, BuildModelEntriesFlattensProviders) {
    ProviderInfo p;
    p.id = "openai";
    p.name = "OpenAI";
    ModelInfo m;
    m.id = "gpt-4";
    m.name = "GPT-4";
    p.models.push_back(m);
    std::vector<ProviderInfo> providers{p};
    auto entries = build_model_entries(providers);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].model_id, "gpt-4");
    EXPECT_EQ(entries[0].provider_name, "OpenAI");
}

TEST(CommandsTest, ResolveProviderModelIndicesMatchesCleanly) {
    ProviderInfo p1;
    p1.id = "antigravity";
    p1.name = "Antigravity";
    ModelInfo m1_1;
    m1_1.id = "gemini-3.8-flash";
    m1_1.name = "Gemini 3.8 Flash";
    p1.models.push_back(m1_1);

    ProviderInfo p2;
    p2.id = "opencode";
    p2.name = "OpenCode Zen";
    ModelInfo m2_1;
    m2_1.id = "muse-spark-1.3-contributor-free";
    m2_1.name = "Muse Spark 1.3 Free";
    p2.models.push_back(m2_1);

    std::vector<ProviderInfo> providers{p1, p2};

    // 1. Exact name match
    auto res1 = tui::resolve_provider_model_indices(providers, "Antigravity", "Gemini 3.8 Flash");
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(res1->first, 0);
    EXPECT_EQ(res1->second, 0);

    // 2. Case-insensitive id match
    auto res2 = tui::resolve_provider_model_indices(providers, "OPENCODE", "muse-spark-1.3-contributor-free");
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(res2->first, 1);
    EXPECT_EQ(res2->second, 0);

    // 3. Empty provider with provider:model combo
    auto res3 = tui::resolve_provider_model_indices(providers, "", "opencode:muse-spark-1.3-contributor-free");
    ASSERT_TRUE(res3.has_value());
    EXPECT_EQ(res3->first, 1);
    EXPECT_EQ(res3->second, 0);

    // 4. Empty provider with bare model id search across all providers
    auto res4 = tui::resolve_provider_model_indices(providers, "", "gemini-3.8-flash");
    ASSERT_TRUE(res4.has_value());
    EXPECT_EQ(res4->first, 0);
    EXPECT_EQ(res4->second, 0);

    // 5. Unknown returns nullopt
    auto res_none = tui::resolve_provider_model_indices(providers, "non-existent", "non-existent");
    EXPECT_FALSE(res_none.has_value());
}

TEST(CommandsTest, ModelSlashCommandSwitchesModel) {
    ProviderInfo p1;
    p1.id = "antigravity";
    p1.name = "Antigravity";
    ModelInfo m1_1;
    m1_1.id = "gemini-3.8-flash";
    m1_1.name = "Gemini 3.8 Flash";
    p1.models.push_back(m1_1);

    ProviderInfo p2;
    p2.id = "opencode";
    p2.name = "OpenCode Zen";
    ModelInfo m2_1;
    m2_1.id = "muse-spark-1.3-free";
    m2_1.name = "Muse Spark 1.3 Free";
    p2.models.push_back(m2_1);

    std::vector<ProviderInfo> providers{p1, p2};
    int selected_provider = 0;
    int selected_model = 0;
    bool enable_tools = true;
    std::string system_prompt = "sys";
    ChatState state;
    bus::BusRuntime bus;

    std::string prompt_input = "";

    // Test /model without args lists models
    handle_slash_command("/model", prompt_input, providers, selected_provider,
                         selected_model, enable_tools, system_prompt, state,
                         nullptr, bus);
    ASSERT_FALSE(state.messages_history->empty());
    EXPECT_THAT(state.messages_history->back().get_text(),
                testing::HasSubstr("Available models"));

    // Test /model 2 switches to second model
    handle_slash_command("/model 2", prompt_input, providers, selected_provider,
                         selected_model, enable_tools, system_prompt, state,
                         nullptr, bus);
    EXPECT_EQ(selected_provider, 1);
    EXPECT_EQ(selected_model, 0);
    EXPECT_THAT(state.messages_history->back().get_text(),
                testing::HasSubstr("Model switched to: Muse Spark 1.3 Free"));

    // Test /model gemini switches back to first model
    handle_slash_command("/model gemini", prompt_input, providers, selected_provider,
                         selected_model, enable_tools, system_prompt, state,
                         nullptr, bus);
    EXPECT_EQ(selected_provider, 0);
    EXPECT_EQ(selected_model, 0);
    EXPECT_THAT(state.messages_history->back().get_text(),
                testing::HasSubstr("Model switched to: Gemini 3.8 Flash"));
}

}  // namespace
}  // namespace qcode
