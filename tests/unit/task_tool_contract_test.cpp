// Contract for the subagent team as the orchestrator sees it: a background
// spawn reports "running", the status moves to "done" once the runner
// returns, and the report arrives in the lead's inbox with the same task id.
#include <qcode/session/session_store.h>
#include <qcode/tools/task_tool.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace qcode {
namespace {

TEST(TaskToolContractTest, BackgroundSubagentGoesFromRunningToDone) {
  const std::string lead = "contract_lead";
  session::ensure_session_row(lead, "Lead", "p", "m", "/ws");
  auto release = std::make_shared<std::atomic<bool>>(false);
  ToolExecutionContext context;
  context.session_id = lead;
  context.subagent_runner = [release](const JsonValue&, std::shared_ptr<std::atomic<bool>>) {
    while (!release->load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return JsonValue{{"output", "contract report"}};
  };

  // Spawn: the call returns at once with a running task.
  const JsonValue spawned = TaskTool::execute(
      JsonValue{{"prompt", "check the contract"}, {"description", "contract"},
                {"background", true}},
      context);
  ASSERT_FALSE(spawned.contains("error")) << spawned.dump();
  const std::string id = spawned["metadata"].value("task_id", "");
  ASSERT_FALSE(id.empty());
  EXPECT_EQ(spawned["metadata"].value("status", ""), "running");
  EXPECT_EQ(spawned["metadata"].value("sessionId", ""), id);
  EXPECT_TRUE(TaskTool::is_session_running(id));

  // While it runs, status says so.
  const JsonValue running = TaskTool::execute(
      JsonValue{{"action", "status"}, {"task_id", id}}, context);
  EXPECT_THAT(running.value("output", ""), testing::HasSubstr("is running"));

  // Finish: the team no longer lists it as running.
  release->store(true);
  for (int i = 0; i < 2000 && TaskTool::is_session_running(id); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_FALSE(TaskTool::is_session_running(id));

  // The report reaches the lead once, headed by the same task id.
  const auto notices = TaskTool::take_notices(lead);
  ASSERT_EQ(notices.size(), 1u);
  EXPECT_THAT(notices[0], testing::StartsWith("[Background task " + id + " (contract) finished]"));
  EXPECT_THAT(notices[0], testing::HasSubstr("contract report"));

  // Afterwards the status is the stored "done" with the same output.
  const JsonValue done = TaskTool::execute(
      JsonValue{{"action", "status"}, {"task_id", id}}, context);
  EXPECT_THAT(done.value("output", ""), testing::StartsWith("Task " + id + ": done"));
  EXPECT_THAT(done.value("output", ""), testing::HasSubstr("contract report"));
}

}  // namespace
}  // namespace qcode
