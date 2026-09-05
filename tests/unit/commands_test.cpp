#include <qcode/ui/commands.h>
#include <qcode/config/provider_info.h>

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace qcode
