#include <qcode/tools/tool_catalog.h>
#include <qcode/tools/tool_executor.h>
#include <qcode/core/tool.h>

#include <gtest/gtest.h>

namespace qcode {
namespace {

TEST(ToolCatalogTest, DescriptorsIncludeBashAndImage) {
    auto d = ToolCatalog::descriptors();
    ASSERT_FALSE(d.empty());
    bool has_bash = false;
    bool has_image = false;
    for (const auto& e : d) {
        if (e.name == "bash") has_bash = true;
        if (e.name == "image") has_image = true;
    }
    EXPECT_TRUE(has_bash);
    EXPECT_TRUE(has_image);
}

TEST(ToolCatalogTest, OrchestratorAndSubagentToolSets) {
    // Both agents get bash and task; vision adds image.
    for (const auto& cfg : {ToolConfig::orchestrator(false), ToolConfig::subagent(false)}) {
        auto tools = ToolCatalog::build_definitions(cfg);
        EXPECT_EQ(tools.size(), 2u);
        EXPECT_TRUE(tools.count("bash"));
        EXPECT_TRUE(tools.count("task"));
        EXPECT_FALSE(tools.count("image"));
    }
    for (const auto& cfg : {ToolConfig::orchestrator(true), ToolConfig::subagent(true)}) {
        auto tools = ToolCatalog::build_definitions(cfg);
        EXPECT_EQ(tools.size(), 3u);
        EXPECT_TRUE(tools.count("image"));
    }
}

TEST(ToolCatalogTest, BuildDefinitionsHonorsConfig) {
    auto all_three = ToolCatalog::build_definitions(ToolConfig{true, true, true});
    EXPECT_TRUE(all_three.count("bash"));
    EXPECT_TRUE(all_three.count("task"));
    EXPECT_TRUE(all_three.count("image"));

    auto bash_task = ToolCatalog::build_definitions(ToolConfig{true, true, false});
    EXPECT_TRUE(bash_task.count("bash"));
    EXPECT_TRUE(bash_task.count("task"));
    EXPECT_FALSE(bash_task.count("image"));

    auto bash_only = ToolCatalog::build_definitions(ToolConfig{true, false, false});
    EXPECT_TRUE(bash_only.count("bash"));
    EXPECT_FALSE(bash_only.count("task"));
    EXPECT_FALSE(bash_only.count("image"));

    auto none = ToolCatalog::build_definitions(ToolConfig{false, false, false});
    EXPECT_TRUE(none.empty());
}

TEST(ToolCatalogTest, FormatHelpersNonEmpty) {
    EXPECT_FALSE(ToolCatalog::format_tool_call("bash", "{}", 1, 2).empty());
    EXPECT_FALSE(ToolCatalog::format_tool_call("image", "{\"path\":\"diagram.png\"}", 1, 2).empty());
}

TEST(ToolExecutorTest, ToolExistsAndMissingCall) {
    ToolSet tools;
    tools["echo"] = Tool("echo", nlohmann::json::object(),
                         [](const nlohmann::json&, const ToolExecutionContext&) {
                             return nlohmann::json("ok");
                         });
    EXPECT_TRUE(ToolExecutor::tool_exists("echo", tools));
    EXPECT_FALSE(ToolExecutor::tool_exists("missing", tools));

    ToolCall missing("1", "missing", nlohmann::json::object());
    auto result = ToolExecutor::execute_tool(missing, tools);
    EXPECT_TRUE(result.error.has_value());
}

}  // namespace
}  // namespace qcode
