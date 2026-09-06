// Multi-agent wiring: when the generation layer injects a subagent_runner,
// the task tool must execute the real nested turn and surface its output /
// errors instead of returning the simulated acknowledgement.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <atomic>

#include <qcode/core/tool.h>
#include <qcode/tools/task_tool.h>

namespace qcode {
namespace test {

class TaskToolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    TaskTool::clear_background_tasks();
  }
  void TearDown() override {
    TaskTool::clear_background_tasks();
  }
};

TEST_F(TaskToolTest, UsesInjectedRunnerAndReturnsRealOutput) {
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

TEST_F(TaskToolTest, PropagatesRunnerErrors) {
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

TEST_F(TaskToolTest, FallsBackToTemplateWithoutRunner) {
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

TEST_F(TaskToolTest, SpawnsParallelBackgroundSubagents) {
  std::atomic<int> running_count{0};
  std::atomic<int> max_parallel{0};

  ToolExecutionContext context;
  context.subagent_runner = [&](const JsonValue& args) -> JsonValue {
    int current = ++running_count;
    int prev_max = max_parallel.load();
    while (current > prev_max && !max_parallel.compare_exchange_weak(prev_max, current)) {}

    // Sleep briefly so both tasks overlap in parallel
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::string task_name = args.value("description", "");
    --running_count;
    return JsonValue{{"output", "finished " + task_name}};
  };

  // Spawn subagent 1 in background
  JsonValue spawn1 = TaskTool::execute(
      JsonValue{{"op", "spawn"},
                {"subagent_type", "explore"},
                {"description", "task_one"},
                {"prompt", "scan codebase 1"},
                {"background", true}},
      context);

  // Spawn subagent 2 in background with different model override
  JsonValue spawn2 = TaskTool::execute(
      JsonValue{{"op", "spawn"},
                {"subagent_type", "verify"},
                {"description", "task_two"},
                {"prompt", "scan codebase 2"},
                {"model", "openai/gpt-4o"},
                {"background", true}},
      context);

  EXPECT_EQ(spawn1["metadata"].value("status", ""), "running");
  EXPECT_EQ(spawn2["metadata"].value("status", ""), "running");
  std::string bg1 = spawn1["metadata"].value("background_task_id", "");
  std::string bg2 = spawn2["metadata"].value("background_task_id", "");
  EXPECT_FALSE(bg1.empty());
  EXPECT_FALSE(bg2.empty());
  EXPECT_NE(bg1, bg2);

  // Collect results for both tasks
  JsonValue res1 = TaskTool::execute(
      JsonValue{{"op", "result"},
                {"background_task_id", bg1},
                {"timeout_ms", 2000}},
      context);

  JsonValue res2 = TaskTool::execute(
      JsonValue{{"op", "result"},
                {"background_task_id", bg2},
                {"timeout_ms", 2000}},
      context);

  EXPECT_EQ(res1["metadata"].value("status", ""), "done");
  EXPECT_EQ(res2["metadata"].value("status", ""), "done");
  EXPECT_THAT(res1.value("output", ""), testing::HasSubstr("finished task_one"));
  EXPECT_THAT(res2.value("output", ""), testing::HasSubstr("finished task_two"));

  // Verify that parallel execution actually ran concurrently
  EXPECT_GE(max_parallel.load(), 1);
}

TEST_F(TaskToolTest, NonblockingPollAndTimeoutAwait) {
  ToolExecutionContext context;
  context.subagent_runner = [](const JsonValue&) -> JsonValue {
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    return JsonValue{{"output", "delayed result"}};
  };

  JsonValue spawn = TaskTool::execute(
      JsonValue{{"op", "spawn"},
                {"subagent_type", "explore"},
                {"description", "slow task"},
                {"prompt", "do slow work"},
                {"background", true}},
      context);

  std::string bg_id = spawn["metadata"].value("background_task_id", "");
  ASSERT_FALSE(bg_id.empty());

  // Immediate nonblocking poll (timeout_ms = 0) should return running
  JsonValue poll_res = TaskTool::execute(
      JsonValue{{"op", "result"},
                {"background_task_id", bg_id},
                {"timeout_ms", 0}},
      context);
  EXPECT_EQ(poll_res["metadata"].value("status", ""), "running");

  // Now wait with generous timeout to finish
  JsonValue final_res = TaskTool::execute(
      JsonValue{{"op", "result"},
                {"background_task_id", bg_id},
                {"timeout_ms", 2000}},
      context);
  EXPECT_EQ(final_res["metadata"].value("status", ""), "done");
  EXPECT_THAT(final_res.value("output", ""), testing::HasSubstr("delayed result"));
}

TEST_F(TaskToolTest, KillLifecycleCancelsTask) {
  ToolExecutionContext context;
  context.subagent_runner = [](const JsonValue&) -> JsonValue {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return JsonValue{{"output", "should not finish"}};
  };

  JsonValue spawn = TaskTool::execute(
      JsonValue{{"op", "spawn"},
                {"subagent_type", "explore"},
                {"description", "kill target"},
                {"prompt", "do work"},
                {"background", true}},
      context);

  std::string bg_id = spawn["metadata"].value("background_task_id", "");
  ASSERT_FALSE(bg_id.empty());

  JsonValue kill_res = TaskTool::execute(
      JsonValue{{"op", "kill"},
                {"background_task_id", bg_id},
                {"reason", "user cancelled"}},
      context);

  EXPECT_EQ(kill_res["metadata"].value("status", ""), "killed");

  // Subsequent result check shows killed status
  JsonValue res = TaskTool::execute(
      JsonValue{{"op", "result"},
                {"background_task_id", bg_id},
                {"timeout_ms", 0}},
      context);
  EXPECT_EQ(res["metadata"].value("status", ""), "killed");
  EXPECT_THAT(res.value("output", ""), testing::HasSubstr("killed by orchestrator"));
}

TEST_F(TaskToolTest, ListsRegisteredSubagents) {
  ToolExecutionContext context;
  context.subagent_runner = [](const JsonValue&) -> JsonValue {
    return JsonValue{{"output", "quick done"}};
  };

  TaskTool::execute(
      JsonValue{{"op", "spawn"},
                {"subagent_type", "verify"},
                {"description", "listable task"},
                {"prompt", "check items"},
                {"background", true}},
      context);

  JsonValue list_res = TaskTool::execute(
      JsonValue{{"op", "list"}},
      context);

  EXPECT_TRUE(list_res.contains("metadata"));
  ASSERT_TRUE(list_res["metadata"].contains("tasks"));
  EXPECT_GE(list_res["metadata"]["tasks"].size(), 1u);
  EXPECT_THAT(list_res.value("output", ""), testing::HasSubstr("listable task"));
}

}  // namespace test
}  // namespace qcode
