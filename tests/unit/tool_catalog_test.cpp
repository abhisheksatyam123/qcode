#include <qcode/tools/tool_catalog.h>
#include <qcode/tools/tool_executor.h>
#include <qcode/core/tool.h>

#include <gtest/gtest.h>

namespace qcode {
namespace {

TEST(ToolCatalogTest, DescriptorsIncludeBash) {
    auto d = ToolCatalog::descriptors();
    ASSERT_FALSE(d.empty());
    bool has_bash = false;
    for (const auto& e : d) {
        if (e.name == "bash") has_bash = true;
    }
    EXPECT_TRUE(has_bash);
}

TEST(ToolCatalogTest, OrchestratorAndSubagentToolSets) {
    // Orchestrator has exactly 2 tools: bash and task (subagent)
    auto orch_tools = ToolCatalog::build_definitions(ToolConfig::orchestrator());
    EXPECT_EQ(orch_tools.size(), 2u);
    EXPECT_TRUE(orch_tools.count("bash"));
    EXPECT_TRUE(orch_tools.count("task"));

    // Subagent has exactly 1 tool: bash
    auto sub_tools = ToolCatalog::build_definitions(ToolConfig::subagent());
    EXPECT_EQ(sub_tools.size(), 1u);
    EXPECT_TRUE(sub_tools.count("bash"));
    EXPECT_FALSE(sub_tools.count("task"));
}

TEST(ToolCatalogTest, BuildDefinitionsHonorsConfig) {
    auto both = ToolCatalog::build_definitions(ToolConfig{true, true});
    EXPECT_TRUE(both.count("bash"));
    EXPECT_TRUE(both.count("task"));

    auto bash_only = ToolCatalog::build_definitions(ToolConfig{true, false});
    EXPECT_TRUE(bash_only.count("bash"));
    EXPECT_FALSE(bash_only.count("task"));

    auto none = ToolCatalog::build_definitions(ToolConfig{false, false});
    EXPECT_TRUE(none.empty());
}

TEST(ToolCatalogTest, FormatHelpersNonEmpty) {
    EXPECT_FALSE(ToolCatalog::format_tool_call("bash", "{}", 1, 2).empty());
    EXPECT_FALSE(ToolCatalog::format_tool_result("bash", true, "ok", 50).empty());
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
