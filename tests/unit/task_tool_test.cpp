// Multi-agent wiring: when the generation layer injects a subagent_runner,
// the task tool must execute the real nested turn and surface its output /
// errors instead of returning the simulated acknowledgement.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <qcode/core/tool.h>
#include <qcode/tools/task_tool.h>

namespace qcode {
namespace test {

TEST(TaskToolSubagentTest, UsesInjectedRunnerAndReturnsRealOutput) {
  bool called = false;
  Tool tool = TaskTool::definition();
  ASSERT_TRUE(tool.has_execute());

  ToolExecutionContext context;
  context.subagent_runner =
      [&called](const JsonValue& args) -> JsonValue {
    called = true;
    EXPECT_EQ(args.value("subagent_type", ""), "general");
    EXPECT_EQ(args.value("prompt", ""), "find all TODOs");
    return JsonValue{{"output", "found 3 TODOs in src/"}};
  };

  const JsonValue out = TaskTool::execute(
      JsonValue{{"op", "spawn"},
                {"subagent_type", "general"},
                {"description", "todo scan"},
                {"prompt", "find all TODOs"}},
      context);

  EXPECT_TRUE(called);
  ASSERT_TRUE(out.contains("output"));
  EXPECT_THAT(out.value("output", ""),
              testing::HasSubstr("found 3 TODOs"));
  EXPECT_EQ(out["metadata"].value("status", ""), "done");
  EXPECT_EQ(out["metadata"].value("real_subagent", false), true);
}

TEST(TaskToolSubagentTest, PropagatesRunnerErrors) {
  ToolExecutionContext context;
  context.subagent_runner = [](const JsonValue&) -> JsonValue {
    return JsonValue{{"error", "subagent aborted"}};
  };

  const JsonValue out = TaskTool::execute(
      JsonValue{{"op", "spawn"},
                {"subagent_type", "general"},
                {"description", "d"},
                {"prompt", "p"}},
      context);
  ASSERT_TRUE(out.contains("error"));
  EXPECT_EQ(out.value("error", ""), "subagent aborted");
}

TEST(TaskToolSubagentTest, FallsBackToTemplateWithoutRunner) {
  ToolExecutionContext context;  // no runner injected

  const JsonValue out = TaskTool::execute(
      JsonValue{{"op", "spawn"},
                {"subagent_type", "general"},
                {"description", "d"},
                {"prompt", "p"}},
      context);
  // Legacy behaviour preserved for direct invocations without a backend.
  EXPECT_EQ(out["metadata"].value("real_subagent", true), false);
  EXPECT_THAT(out.value("output", ""), testing::HasSubstr("@subagent general"));
}

}  // namespace test
}  // namespace qcode
