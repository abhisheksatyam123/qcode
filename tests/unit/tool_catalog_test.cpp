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
    // Orchestrator without vision has exactly 2 tools: bash and task (subagent)
    auto orch_tools = ToolCatalog::build_definitions(ToolConfig::orchestrator(false));
    EXPECT_EQ(orch_tools.size(), 2u);
    EXPECT_TRUE(orch_tools.count("bash"));
    EXPECT_TRUE(orch_tools.count("task"));
    EXPECT_FALSE(orch_tools.count("image"));

    // Orchestrator with vision has 3 tools: bash, task, and image
    auto orch_vis_tools = ToolCatalog::build_definitions(ToolConfig::orchestrator(true));
    EXPECT_EQ(orch_vis_tools.size(), 3u);
    EXPECT_TRUE(orch_vis_tools.count("bash"));
    EXPECT_TRUE(orch_vis_tools.count("task"));
    EXPECT_TRUE(orch_vis_tools.count("image"));

    // Subagent without vision has exactly 1 tool: bash
    auto sub_tools = ToolCatalog::build_definitions(ToolConfig::subagent(false));
    EXPECT_EQ(sub_tools.size(), 1u);
    EXPECT_TRUE(sub_tools.count("bash"));
    EXPECT_FALSE(sub_tools.count("task"));
    EXPECT_FALSE(sub_tools.count("image"));

    // Subagent with vision has 2 tools: bash and image
    auto sub_vis_tools = ToolCatalog::build_definitions(ToolConfig::subagent(true));
    EXPECT_EQ(sub_vis_tools.size(), 2u);
    EXPECT_TRUE(sub_vis_tools.count("bash"));
    EXPECT_FALSE(sub_vis_tools.count("task"));
    EXPECT_TRUE(sub_vis_tools.count("image"));
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
    EXPECT_FALSE(ToolCatalog::format_tool_result("bash", true, "ok", 50).empty());
    EXPECT_FALSE(ToolCatalog::format_tool_call("image", "{\"path\":\"diagram.png\"}", 1, 2).empty());
    EXPECT_FALSE(ToolCatalog::format_tool_result("image", true, "{\"path\":\"diagram.png\",\"mime_type\":\"image/png\",\"size_bytes\":1024}", 50).empty());
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
