#include <unistd.h>
#include <filesystem>
#include <qcode/session/session_store.h>
// Multi-agent wiring: when the generation layer injects a subagent_runner,
// the task tool must execute the real nested turn and surface its output /
// errors instead of returning the simulated acknowledgement.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <qcode/core/tool.h>
#include <qcode/tools/multi_step_coordinator.h>
#include <qcode/tools/task_tool.h>

namespace qcode {
namespace test {

class TaskToolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char tmpl[] = "/tmp/qcode_task_test_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd >= 0) {
      close(fd);
      unlink(tmpl);
      db_path_ = std::string(tmpl) + ".db";
      setenv("QCODE_DB_PATH", db_path_.c_str(), 1);
    }
    qcode::session::init_database();
    TaskTool::clear_background_tasks();
  }
  void TearDown() override {
    TaskTool::clear_background_tasks();
    if (!db_path_.empty()) {
      std::error_code ec;
      std::filesystem::remove(db_path_, ec);
      std::filesystem::remove(db_path_ + "-wal", ec);
      std::filesystem::remove(db_path_ + "-shm", ec);
      unsetenv("QCODE_DB_PATH");
    }
  }
  std::string db_path_;
};

TEST_F(TaskToolTest, UsesInjectedRunnerAndReturnsRealOutput) {
  bool called = false;
  Tool tool = TaskTool::definition();
  ASSERT_TRUE(tool.has_execute());

  ToolExecutionContext context;
  context.subagent_runner =
      [&called](const JsonValue& args,
                std::shared_ptr<std::atomic<bool>>) -> JsonValue {
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
  context.subagent_runner = [](const JsonValue&,
                               std::shared_ptr<std::atomic<bool>>) -> JsonValue {
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
  context.subagent_runner = [&](const JsonValue& args,
                               std::shared_ptr<std::atomic<bool>>) -> JsonValue {
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
  context.subagent_runner = [](const JsonValue&,
                               std::shared_ptr<std::atomic<bool>>) -> JsonValue {
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
  context.subagent_runner =
      [](const JsonValue&,
         std::shared_ptr<std::atomic<bool>> abort) -> JsonValue {
    for (int i = 0; i < 50; ++i) {
      if (abort && abort->load()) {
        return JsonValue{{"error", "cancelled"}};
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
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
  context.subagent_runner = [](const JsonValue&,
                               std::shared_ptr<std::atomic<bool>>) -> JsonValue {
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

TEST_F(TaskToolTest, DefaultsTypeAndAcceptsPromptAliases) {
  JsonValue seen;
  ToolExecutionContext context;
  context.subagent_runner = [&](const JsonValue& args,
                                std::shared_ptr<std::atomic<bool>>) -> JsonValue {
    seen = args;
    return JsonValue{{"output", "ok"}};
  };

  const JsonValue out = TaskTool::execute(
      JsonValue{{"task", "scan the repo"}, {"description", "repo scan"}},
      context);

  EXPECT_EQ(seen.value("subagent_type", ""), "general");
  EXPECT_EQ(seen.value("prompt", ""), "scan the repo");
  EXPECT_EQ(seen.value("mode", ""), "explore");
  EXPECT_EQ(out["metadata"].value("real_subagent", false), true);
  EXPECT_THAT(out.value("output", ""), testing::HasSubstr("ok"));
}

TEST_F(TaskToolTest, UsesModeAsSubagentTypeWhenOmitted) {
  JsonValue seen;
  ToolExecutionContext context;
  context.subagent_runner = [&](const JsonValue& args,
                                std::shared_ptr<std::atomic<bool>>) -> JsonValue {
    seen = args;
    return JsonValue{{"output", "explored"}};
  };

  TaskTool::execute(
      JsonValue{{"prompt", "look around"}, {"mode", "explore"}},
      context);
  EXPECT_EQ(seen.value("subagent_type", ""), "explore");
}

TEST_F(TaskToolTest, UnsupportedLifecycleOpsReturnError) {
  for (const char* op : {"pause", "resume", "resurrect", "model"}) {
    const JsonValue out = TaskTool::execute(JsonValue{{"op", op}, {"task_id", "x"}}, {});
    ASSERT_TRUE(out.contains("error")) << op << " " << out.dump();
    const std::string err = out.value("error", "");
    EXPECT_TRUE(err.find("Unsupported") != std::string::npos ||
                err.find("not supported") != std::string::npos)
        << op << ": " << err;
  }
}

TEST_F(TaskToolTest, DefinitionAdvertisesCallableSchema) {
  Tool tool = TaskTool::definition();
  EXPECT_EQ(tool.name, "task");
  EXPECT_THAT(tool.description, testing::HasSubstr("subagent"));
  ASSERT_TRUE(tool.parameters_schema.contains("properties"));
  const auto& props = tool.parameters_schema["properties"];
  EXPECT_TRUE(props.contains("task"));
  EXPECT_TRUE(props.contains("prompt"));
  EXPECT_TRUE(props.contains("provider"));
  EXPECT_TRUE(props.contains("model"));
  EXPECT_TRUE(props.contains("scope"));
  EXPECT_TRUE(props.contains("background_task_id"));
  const auto& op_enum = props["op"]["enum"];
  EXPECT_THAT(op_enum.dump(), testing::HasSubstr("spawn"));
  EXPECT_THAT(op_enum.dump(), testing::HasSubstr("kill"));
  EXPECT_THAT(op_enum.dump(), testing::Not(testing::HasSubstr("pause")));
}

TEST_F(TaskToolTest, SwappedCursorTaskFieldsAreRepaired) {
  JsonValue seen;
  ToolExecutionContext context;
  context.subagent_runner = [&](const JsonValue& args,
                                std::shared_ptr<std::atomic<bool>>) {
    seen = args;
    return JsonValue{{"output", "ok"}};
  };
  JsonValue out = TaskTool::execute(
      JsonValue{
          {"prompt", "explore"},
          {"subagent_type", "cursor-grok-4.6"},
          {"model", "Read-only. Workspace: /home/abhi/project/qcode\n\nRead README.md"},
          {"description", "readme"},
          {"run_in_background", false},
      },
      context);
  EXPECT_FALSE(out.contains("error")) << out.dump();
  EXPECT_THAT(seen.value("prompt", ""), testing::HasSubstr("README.md"));
  EXPECT_EQ(seen.value("mode", ""), "explore");
  EXPECT_EQ(seen.value("subagent_type", ""), "explore");
  EXPECT_EQ(seen.value("model", ""), "cursor-grok-4.6");
}

TEST_F(TaskToolTest, PromptLikeModelIsNotSplitAsProvider) {
  JsonValue norm = TaskTool::normalize_spawn_args(JsonValue{
      {"prompt", "explore"},
      {"model", "Read-only. Workspace: /tmp"},
      {"description", "x"},
  });
  EXPECT_FALSE(norm.contains("provider"));
  EXPECT_THAT(norm.value("prompt", ""), testing::HasSubstr("Workspace:"));
}

TEST_F(TaskToolTest, ReassemblesShreddedProtoPromptFields) {
  JsonValue seen;
  ToolExecutionContext context;
  context.subagent_runner = [&](const JsonValue& args,
                                std::shared_ptr<std::atomic<bool>>) {
    seen = args;
    return JsonValue{{"output", "ok"}};
  };
  const std::string f10 =
      "AD-ONLY large nested session. Do not edit any files.\n\nInspect /home/a";
  const std::string f12 =
      "i/project/qcode with bash. Produce a LONG factual report covering TaskTool.";
  JsonValue out = TaskTool::execute(
      JsonValue{
          {"prompt", "generalPurpose"},
          {"subagent_type", "composer-2.5-fast"},
          {"description", "call-abc\nfc_def"},
          {"10", f10},
          {"12", f12},
          {"4", 68},
          {"9", "c5a0606b85fc1aec05deb798dbce1f50"},
          {"run_in_background", false},
      },
      context);
  EXPECT_FALSE(out.contains("error")) << out.dump();
  EXPECT_THAT(seen.value("prompt", ""), testing::HasSubstr("/home/ai/project/qcode"));
  EXPECT_THAT(seen.value("prompt", ""), testing::HasSubstr("TaskTool"));
  EXPECT_GT(seen.value("prompt", "").size(), 69u);
  EXPECT_FALSE(seen.value("run_in_background", false));
}

TEST_F(TaskToolTest, Field10AloneStillBecomesPromptWhenNamedIsModeToken) {
  JsonValue norm = TaskTool::normalize_spawn_args(JsonValue{
      {"prompt", "explore"},
      {"10", "AD-ONLY large session test. Do not edit files. Workspace is /home/abh"},
  });
  EXPECT_THAT(norm.value("prompt", ""), testing::HasSubstr("Workspace is /home/abh"));
  EXPECT_EQ(norm.value("mode", ""), "explore");
}

TEST_F(TaskToolTest, SessionIdParsedFromSpawnOutput) {
  JsonValue result;
  result["output"] = "background_task_id: bg_ses_1\ntask_id: ses_123abc\nstatus: running";
  result["metadata"] = {{"sessionId", "ses_123abc"}, {"task_id", "ses_123abc"}};
  EXPECT_EQ(TaskTool::session_id_from_result(result), "ses_123abc");
}

TEST_F(TaskToolTest, SubagentsCannotPerformNestedDelegation) {
  ToolExecutionContext sub_context;
  sub_context.session_id = "ses_child_subagent_123";

  const JsonValue out = TaskTool::execute(
      JsonValue{{"op", "spawn"},
                {"subagent_type", "general"},
                {"description", "nested delegation attempt"},
                {"prompt", "do something"}},
      sub_context);

  ASSERT_TRUE(out.contains("error"));
  EXPECT_THAT(out.value("error", ""),
              testing::HasSubstr("Nested delegation is disabled for subagents"));
}

TEST_F(TaskToolTest, ListsDurableChildSessionsFromDatabase) {
  const std::string parent_sid = "parent_sess_abc123";
  qcode::session::ensure_session_row(parent_sid, "Parent Chat", "openrouter", "model1", "/ws");
  qcode::session::ensure_session_row("ses_durable_child_1", "Audit simulation parity", "cursor", "composer-2.5", "/ws", parent_sid);
  qcode::session::save_message("ses_durable_child_1", "Assistant", "Audit complete: found 5 gaps.");

  auto list_json = TaskTool::list_tasks(parent_sid);
  ASSERT_TRUE(list_json.contains("metadata"));
  ASSERT_TRUE(list_json["metadata"].contains("tasks"));
  const auto& tasks = list_json["metadata"]["tasks"];
  ASSERT_GE(tasks.size(), 1u);

  bool found = false;
  for (const auto& t : tasks) {
    if (t.value("task_id", "") == "ses_durable_child_1") {
      found = true;
      EXPECT_EQ(t.value("status", ""), "done");
      EXPECT_EQ(t.value("description", ""), "Audit simulation parity");
      EXPECT_EQ(t.value("parent_session_id", ""), parent_sid);
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(TaskToolTest, ResultFallsBackToDurableSession) {
  const std::string child_sid = "ses_durable_child_res_999";
  qcode::session::ensure_session_row(child_sid, "Network test suite", "antigravity", "gemini-3.8-flash", "/ws");
  qcode::session::save_message(child_sid, "Assistant", "Network test suite passed: 10/10 ok.");

  // Clear in-memory registry to simulate process restart or lost registry
  TaskTool::clear_background_tasks();

  JsonValue res = TaskTool::execute(
      JsonValue{{"op", "result"},
                {"background_task_id", "bg_" + child_sid}},
      {});

  ASSERT_TRUE(res.contains("metadata"));
  EXPECT_EQ(res["metadata"].value("status", ""), "done");
  EXPECT_THAT(res.value("output", ""), testing::HasSubstr("Network test suite passed"));
}



TEST_F(TaskToolTest, SubagentsExecuteConcurrentlyOnSeparateWorkerThreadsWithoutBlocking) {
  constexpr int kNumWorkers = 3;
  std::atomic<int> concurrent_active{0};
  std::atomic<int> max_concurrent{0};
  std::mutex thread_ids_mutex;
  std::set<std::thread::id> thread_ids;

  ToolExecutionContext context;
  context.subagent_runner = [&](const JsonValue& args,
                                std::shared_ptr<std::atomic<bool>>) -> JsonValue {
    {
      std::lock_guard<std::mutex> lock(thread_ids_mutex);
      thread_ids.insert(std::this_thread::get_id());
    }

    int cur = ++concurrent_active;
    int prev_max = max_concurrent.load();
    while (cur > prev_max && !max_concurrent.compare_exchange_weak(prev_max, cur)) {}

    // Sleep long enough so all workers overlap concurrently
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    --concurrent_active;
    return JsonValue{{"output", "worker done: " + args.value("description", "")}};
  };

  std::vector<std::string> bg_ids;
  for (int i = 0; i < kNumWorkers; ++i) {
    JsonValue spawn = TaskTool::execute(
        JsonValue{{"op", "spawn"},
                  {"subagent_type", "explore"},
                  {"description", "worker_" + std::to_string(i)},
                  {"prompt", "work"},
                  {"background", true}},
        context);
    EXPECT_EQ(spawn["metadata"].value("status", ""), "running");
    bg_ids.push_back(spawn["metadata"].value("background_task_id", ""));
  }

  // All workers should have distinct background task IDs
  EXPECT_EQ(bg_ids.size(), static_cast<size_t>(kNumWorkers));
  for (size_t i = 0; i < bg_ids.size(); ++i) {
    for (size_t j = i + 1; j < bg_ids.size(); ++j) {
      EXPECT_NE(bg_ids[i], bg_ids[j]);
    }
  }

  // Collect results
  for (int i = 0; i < kNumWorkers; ++i) {
    JsonValue res = TaskTool::execute(
        JsonValue{{"op", "result"},
                  {"background_task_id", bg_ids[i]},
                  {"timeout_ms", 3000}},
        context);
    EXPECT_EQ(res["metadata"].value("status", ""), "done");
    EXPECT_THAT(res.value("output", ""),
                testing::HasSubstr("worker done: worker_" + std::to_string(i)));
  }

  // Check that multiple workers ran concurrently on distinct worker threads
  EXPECT_GE(max_concurrent.load(), 2);
  EXPECT_GE(thread_ids.size(), 2u);
}

TEST(MultiStepCoordinatorTest, ProcessesToolCallsEvenWhenFinishReasonIsStop) {
  ToolSet tools;
  Tool mock_tool;
  mock_tool.name = "calc_tool";
  mock_tool.description = "A calculation tool";
  mock_tool.execute = [](const JsonValue&, const ToolExecutionContext&) -> JsonValue {
    return JsonValue{{"result", 42}};
  };
  tools["calc_tool"] = mock_tool;

  GenerateOptions opts("mock-model", "system prompt", "initial user prompt");
  opts.tools = tools;
  opts.max_steps = 3;

  int step_count = 0;
  auto generate_func = [&](const GenerateOptions& step_opts) -> GenerateResult {
    step_count++;
    if (step_count == 1) {
      // Step 1: Model outputs tool calls but sets finish_reason = Stop
      GenerateResult res;
      res.finish_reason = kFinishReasonStop;
      res.tool_calls.push_back(ToolCall("call_calc", "calc_tool", JsonValue::object()));
      return res;
    } else {
      // Step 2: Model receives tool result
      bool found_tool_result = false;
      for (const auto& msg : step_opts.messages) {
        if (msg.has_tool_results()) {
          found_tool_result = true;
          for (const auto& part : msg.content) {
            if (std::holds_alternative<ToolResultContentPart>(part)) {
              const auto& tr = std::get<ToolResultContentPart>(part);
              EXPECT_FALSE(tr.is_error);
              EXPECT_EQ(tr.result.value("result", 0), 42);
            }
          }
        }
      }
      EXPECT_TRUE(found_tool_result);

      GenerateResult res;
      res.finish_reason = kFinishReasonStop;
      res.text = "Result is 42.";
      return res;
    }
  };

  GenerateResult final_res = MultiStepCoordinator::execute_multi_step(opts, generate_func);

  EXPECT_EQ(step_count, 2);
  EXPECT_TRUE(final_res.is_success());
  EXPECT_EQ(final_res.text, "Result is 42.");
}

TEST(MultiStepCoordinatorTest, DoesNotAbortWhenToolExecutionFails) {
  // Setup options with tools
  ToolSet tools;
  Tool mock_tool;
  mock_tool.name = "failing_tool";
  mock_tool.description = "A tool that returns an error";
  mock_tool.execute = [](const JsonValue&, const ToolExecutionContext&) -> JsonValue {
    throw std::runtime_error("Command failed with exit code 1");
  };
  tools["failing_tool"] = mock_tool;

  GenerateOptions opts("mock-model", "system prompt", "initial user prompt");
  opts.tools = tools;
  opts.max_steps = 3;

  int step_count = 0;
  auto generate_func = [&](const GenerateOptions& step_opts) -> GenerateResult {
    step_count++;
    if (step_count == 1) {
      // Step 1: Model calls failing_tool
      GenerateResult res;
      res.finish_reason = kFinishReasonToolCalls;
      res.tool_calls.push_back(ToolCall("call_1", "failing_tool", JsonValue::object()));
      return res;
    } else {
      // Step 2: Model receives error tool result and recovers/completes
      bool found_tool_result = false;
      for (const auto& msg : step_opts.messages) {
        if (msg.has_tool_results()) {
          found_tool_result = true;
          for (const auto& part : msg.content) {
            if (std::holds_alternative<ToolResultContentPart>(part)) {
              const auto& tr = std::get<ToolResultContentPart>(part);
              EXPECT_TRUE(tr.is_error);
              EXPECT_TRUE(tr.result.contains("error"));
              EXPECT_THAT(tr.result["error"].get<std::string>(),
                          testing::HasSubstr("Command failed with exit code 1"));
            }
          }
        }
      }
      EXPECT_TRUE(found_tool_result);

      GenerateResult res;
      res.finish_reason = kFinishReasonStop;
      res.text = "I recovered from the error.";
      return res;
    }
  };

  GenerateResult final_res = MultiStepCoordinator::execute_multi_step(opts, generate_func);

  EXPECT_EQ(step_count, 2);
  EXPECT_TRUE(final_res.is_success());
  EXPECT_EQ(final_res.finish_reason, kFinishReasonStop);
  EXPECT_EQ(final_res.text, "I recovered from the error.");
}

}  // namespace test
}  // namespace qcode
