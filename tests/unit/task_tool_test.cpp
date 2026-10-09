// The task tool runs one blocking subagent turn through the injected runner,
// records it as a child session, and reports the subagent's final text.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include <qcode/compaction/compaction_request.h>
#include <qcode/core/tool.h>
#include <qcode/session/session_store.h>
#include <qcode/tools/multi_step_coordinator.h>
#include <qcode/tools/task_tool.h>
#include <qcode/tools/tool_executor.h>

namespace qcode {
namespace test {

namespace {

SubagentRunner echo_runner(std::string output) {
  return [output](const JsonValue&, std::shared_ptr<std::atomic<bool>>) {
    return JsonValue{{"output", output}};
  };
}

}  // namespace

TEST(TaskToolTest, RunsSubagentAndReturnsItsReport) {
  qcode::session::ensure_session_row("parent_run_1", "Parent", "p", "m", "/ws");
  JsonValue seen;
  ToolExecutionContext context;
  context.session_id = "parent_run_1";
  context.subagent_runner = [&seen](const JsonValue& args,
                                    std::shared_ptr<std::atomic<bool>>) {
    seen = args;
    return JsonValue{{"output", "found 3 TODOs in src/"}};
  };

  const JsonValue out = TaskTool::execute(
      JsonValue{{"description", "todo scan"}, {"prompt", "find all TODOs"}}, context);

  ASSERT_FALSE(out.contains("error")) << out.dump();
  EXPECT_THAT(out.value("output", ""), testing::StartsWith("found 3 TODOs in src/\n\n["));
  EXPECT_EQ(out["metadata"].value("status", ""), "done");
  EXPECT_EQ(seen.value("prompt", ""), "find all TODOs");
  EXPECT_FALSE(seen.contains("model"));  // omitted: the runner uses the caller's model

  // The run is a durable child session of the caller.
  const std::string child = out["metadata"].value("sessionId", "");
  ASSERT_FALSE(child.empty());
  EXPECT_EQ(seen.value("session_id", ""), child);
  // The footer names the task to rate; the child session keeps the bare report.
  EXPECT_THAT(out.value("output", ""), testing::HasSubstr("[task_id: " + child));
  EXPECT_EQ(TaskTool::session_id_from_result(JsonValue(out.value("output", ""))), child);
  EXPECT_TRUE(qcode::session::is_child_session(child));
  auto last = qcode::session::load_last_session_message(child);
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(last->first, "Assistant");
  EXPECT_EQ(last->second, "found 3 TODOs in src/");
  EXPECT_FALSE(TaskTool::is_session_running(child));
}

TEST(TaskToolTest, RunnerErrorIsReportedAndPersisted) {
  qcode::session::ensure_session_row("parent_err_1", "Parent", "p", "m", "/ws");
  ToolExecutionContext context;
  context.session_id = "parent_err_1";
  context.subagent_runner = [](const JsonValue&, std::shared_ptr<std::atomic<bool>>) {
    return JsonValue{{"error", "provider exploded"}};
  };

  const JsonValue out = TaskTool::execute(JsonValue{{"prompt", "p"}}, context);
  EXPECT_THAT(out.value("error", ""), testing::StartsWith("provider exploded"));
  EXPECT_EQ(out["metadata"].value("status", ""), "error");

  const auto tasks = TaskTool::list_tasks("parent_err_1")["metadata"]["tasks"];
  ASSERT_EQ(tasks.size(), 1u);
  EXPECT_EQ(tasks[0].value("status", ""), "error");
}

TEST(TaskToolTest, RejectsBadArguments) {
  ToolExecutionContext context;
  context.subagent_runner = echo_runner("ok");
  EXPECT_TRUE(TaskTool::execute(JsonValue{{"description", "x"}}, context).contains("error"));
  EXPECT_THAT(TaskTool::execute(JsonValue{{"action", "rate"}}, context).value("error", ""),
              testing::HasSubstr("Unknown action"));

  ToolExecutionContext no_runner;
  EXPECT_TRUE(TaskTool::execute(JsonValue{{"prompt", "p"}}, no_runner).contains("error"));
}

// A subagent that starts a task starts a sister: a child of the lead.
TEST(TaskToolTest, SubagentStartsASisterUnderTheLead) {
  qcode::session::ensure_session_row("parent_nest_1", "Parent", "p", "m", "/ws");
  qcode::session::ensure_session_row("ses_nest_child_1", "Child", "p", "m", "/ws",
                                     "parent_nest_1");
  JsonValue seen;
  ToolExecutionContext context;
  context.session_id = "ses_nest_child_1";
  context.subagent_runner = [&seen](const JsonValue& args, std::shared_ptr<std::atomic<bool>>) {
    seen = args;
    return JsonValue{{"output", "sister done"}};
  };

  const JsonValue started = TaskTool::execute(JsonValue{{"prompt", "do something"}}, context);
  ASSERT_FALSE(started.contains("error")) << started.dump();
  const std::string sister = started["metadata"].value("task_id", "");
  EXPECT_EQ(seen.value("parent_session_id", ""), "parent_nest_1");
  EXPECT_EQ(qcode::session::get_parent_session_id(sister), "parent_nest_1");

  // Its status shows the whole team and who is asking.
  const std::string team =
      TaskTool::execute(JsonValue{{"action", "status"}}, context).value("output", "");
  EXPECT_THAT(team, testing::HasSubstr("Lead (orchestrator): parent_nest_1"));
  EXPECT_THAT(team, testing::HasSubstr("You: ses_nest_child_1"));
  EXPECT_THAT(team, testing::HasSubstr(sister));
}

TEST(TaskToolTest, ReportsTheModelTheRunnerActuallyUsed) {
  ToolExecutionContext context;
  context.subagent_runner = [](const JsonValue& args, std::shared_ptr<std::atomic<bool>>) {
    EXPECT_EQ(args.value("model", ""), "openrouter:deepseek/foo");
    return JsonValue{{"output", "ok"}, {"provider", "opencode"}, {"model", "big-pickle"}};
  };
  const JsonValue out = TaskTool::execute(
      JsonValue{{"prompt", "scan"}, {"model", "openrouter:deepseek/foo"}}, context);
  EXPECT_EQ(out["metadata"].value("model", ""), "opencode:big-pickle");
}

TEST(TaskToolTest, DefinitionHasMinimalSchema) {
  const Tool tool = TaskTool::definition();
  EXPECT_EQ(tool.name, "task");
  ASSERT_TRUE(tool.has_execute());
  const auto& props = tool.parameters_schema["properties"];
  std::set<std::string> keys;
  for (auto it = props.begin(); it != props.end(); ++it) keys.insert(it.key());
  EXPECT_EQ(keys, (std::set<std::string>{"description", "prompt", "model", "background",
                                         "task_id", "action", "timeout_s"}));
  // prompt is checked by execute(): status / wait / kill take none.
  EXPECT_FALSE(tool.parameters_schema.contains("required"));
}

// ── Background and resumed subagents ──

namespace {
// Waits until `parent` has a finished background report (or 5 s pass).
std::vector<std::string> wait_for_notices(const std::string& parent) {
  for (int i = 0; i < 500 && !TaskTool::has_notices(parent); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return TaskTool::take_notices(parent);
}
}  // namespace

TEST(TaskToolTest, BackgroundTaskReturnsAtOnceAndReportsLater) {
  const std::string parent = "parent_bg_1";
  qcode::session::ensure_session_row(parent, "Parent", "p", "m", "/ws");
  auto release = std::make_shared<std::atomic<bool>>(false);
  auto lead_abort = std::make_shared<std::atomic<bool>>(false);
  auto seen = std::make_shared<JsonValue>();
  auto seen_mutex = std::make_shared<std::mutex>();
  ToolExecutionContext context;
  context.session_id = parent;
  context.abort_flag = lead_abort;
  context.subagent_runner = [seen, seen_mutex, release](const JsonValue& args,
                                                        std::shared_ptr<std::atomic<bool>> flag) {
    {
      std::lock_guard<std::mutex> lock(*seen_mutex);
      *seen = args;
    }
    while (!release->load() && !flag->load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return JsonValue{{"output", "scan done"}, {"model", "m1"}, {"provider", "opencode"}};
  };

  const JsonValue out = TaskTool::execute(
      JsonValue{{"prompt", "scan"}, {"description", "bg scan"}, {"background", true}}, context);
  ASSERT_FALSE(out.contains("error"));
  const std::string id = out["metadata"].value("task_id", "");
  ASSERT_FALSE(id.empty());
  EXPECT_EQ(out["metadata"].value("status", ""), "running");
  EXPECT_TRUE(TaskTool::is_session_running(id));
  EXPECT_FALSE(TaskTool::has_notices(parent));
  // The lead's turn ending (its abort flag) does not stop a background task.
  lead_abort->store(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  EXPECT_TRUE(TaskTool::is_session_running(id));
  lead_abort->store(false);
  EXPECT_THAT(TaskTool::execute(JsonValue{{"action", "status"}, {"task_id", id}}, context)
                  .value("output", ""),
              testing::HasSubstr("is running"));

  release->store(true);
  const auto notices = wait_for_notices(parent);
  ASSERT_EQ(notices.size(), 1u);
  EXPECT_THAT(notices[0], testing::StartsWith("[Background task " + id + " (bg scan) finished]"));
  EXPECT_THAT(notices[0], testing::HasSubstr("scan done"));
  EXPECT_THAT(notices[0], testing::HasSubstr("opencode:m1"));
  EXPECT_FALSE(TaskTool::has_notices(parent));  // delivered once
  for (int i = 0; i < 100 && TaskTool::is_session_running(id); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_FALSE(TaskTool::is_session_running(id));
  {
    std::lock_guard<std::mutex> lock(*seen_mutex);
    EXPECT_EQ(seen->value("parent_session_id", ""), parent);
  }
  auto last = qcode::session::load_last_session_message(id);
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(last->second, "scan done");
}

TEST(TaskToolTest, WaitBlocksForBackgroundTasksAndKillStopsThem) {
  const std::string parent = "parent_bg_2";
  qcode::session::ensure_session_row(parent, "Parent", "p", "m", "/ws");
  ToolExecutionContext context;
  context.session_id = parent;
  context.subagent_runner = [](const JsonValue& args, std::shared_ptr<std::atomic<bool>> flag) {
    if (args.value("prompt", "") == "quick") {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      return JsonValue{{"output", "quick report"}};
    }
    while (!flag->load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return JsonValue{{"error", "Subagent cancelled due to abort flag"}};
  };
  const std::string quick =
      TaskTool::execute(JsonValue{{"prompt", "quick"}, {"background", true}}, context)
          ["metadata"].value("task_id", "");
  const std::string slow =
      TaskTool::execute(JsonValue{{"prompt", "slow"}, {"background", true}}, context)
          ["metadata"].value("task_id", "");

  const JsonValue waited = TaskTool::execute(
      JsonValue{{"action", "wait"}, {"task_id", quick}, {"timeout_s", 5}}, context);
  EXPECT_THAT(waited.value("output", ""), testing::HasSubstr("quick report"));

  const JsonValue killed =
      TaskTool::execute(JsonValue{{"action", "kill"}, {"task_id", slow}}, context);
  EXPECT_FALSE(killed.contains("error"));
  const JsonValue after = TaskTool::execute(
      JsonValue{{"action", "wait"}, {"task_id", slow}, {"timeout_s", 5}}, context);
  EXPECT_THAT(after.value("output", ""), testing::HasSubstr("Error: killed"));
  EXPECT_TRUE(TaskTool::take_notices(parent).empty());  // wait delivered both
}

TEST(TaskToolTest, ResumeContinuesTheSameSubagentSession) {
  const std::string parent = "parent_resume_1";
  qcode::session::ensure_session_row(parent, "Parent", "p", "m", "/ws");
  std::vector<JsonValue> seen;
  ToolExecutionContext context;
  context.session_id = parent;
  context.subagent_runner = [&seen](const JsonValue& args, std::shared_ptr<std::atomic<bool>>) {
    seen.push_back(args);
    return JsonValue{{"output", "report " + std::to_string(seen.size())}};
  };
  const JsonValue first = TaskTool::execute(
      JsonValue{{"prompt", "audit parser"}, {"description", "audit"}}, context);
  const std::string id = first["metadata"].value("task_id", "");
  ASSERT_FALSE(id.empty());
  qcode::session::set_session_provider_model(id, "opencode", "m1");  // set by the runner

  const JsonValue second =
      TaskTool::execute(JsonValue{{"prompt", "now check the lexer"}, {"task_id", id}}, context);
  ASSERT_FALSE(second.contains("error"));
  EXPECT_EQ(second["metadata"].value("task_id", ""), id);
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[1].value("session_id", ""), id);
  EXPECT_TRUE(seen[1].value("resume", false));
  EXPECT_EQ(seen[1].value("model", ""), "opencode:m1");  // same model
  // The child keeps one conversation: prompt, report, follow-up, report.
  const auto rows = qcode::session::load_session_messages(id);
  ASSERT_EQ(rows.size(), 4u);
  EXPECT_EQ(rows[2].second, "now check the lexer");
  EXPECT_EQ(rows[3].second, "report 2");

  // Only this session's own subagents can be resumed.
  ToolExecutionContext other = context;
  other.session_id = "someone_else";
  EXPECT_THAT(TaskTool::execute(JsonValue{{"prompt", "x"}, {"task_id", id}}, other)
                  .value("error", ""),
              testing::HasSubstr("not a subagent of this team"));
}

TEST(TaskToolTest, ModelAndParentReachTheRunner) {
  qcode::session::ensure_session_row("parent_diff_1", "Parent", "p", "m", "/ws");
  JsonValue seen;
  ToolExecutionContext context;
  context.session_id = "parent_diff_1";
  context.subagent_runner = [&seen](const JsonValue& args, std::shared_ptr<std::atomic<bool>>) {
    seen = args;
    return JsonValue{{"output", "designed"}, {"provider", "anthropic"},
                     {"model", "claude-opus-5-5"}};
  };
  const JsonValue res = TaskTool::execute(
      JsonValue{{"prompt", "design the cache"}, {"model", "anthropic:claude-opus-5-5"}}, context);
  ASSERT_FALSE(res.contains("error")) << res.dump();
  EXPECT_EQ(seen.value("model", ""), "anthropic:claude-opus-5-5");  // paid is fine
  EXPECT_EQ(seen.value("parent_session_id", ""), "parent_diff_1");
  EXPECT_THAT(res.value("output", ""), testing::HasSubstr("anthropic:claude-opus-5-5"));
}

// Messages: the lead and a running subagent talk through their inboxes.
TEST(TaskToolTest, LeadAndSubagentExchangeMessages) {
  const std::string lead = "parent_msg_1";
  qcode::session::ensure_session_row(lead, "Parent", "p", "m", "/ws");
  ToolExecutionContext context;
  context.session_id = lead;
  // The subagent asks the lead a question, waits for the answer, reports it.
  context.subagent_runner = [](const JsonValue& args, std::shared_ptr<std::atomic<bool>> flag) {
    ToolExecutionContext me;
    me.session_id = args.value("session_id", "");
    me.abort_flag = flag;
    const JsonValue sent = TaskTool::execute(
        JsonValue{{"action", "message"}, {"task_id", "lead"}, {"prompt", "which port?"}}, me);
    if (sent.contains("error")) return JsonValue{{"error", sent["error"]}};
    const JsonValue reply =
        TaskTool::execute(JsonValue{{"action", "wait"}, {"task_id", "lead"}, {"timeout_s", 5}}, me);
    return JsonValue{{"output", "got: " + reply.value("output", "")}};
  };
  const std::string id =
      TaskTool::execute(JsonValue{{"prompt", "serve"}, {"background", true}}, context)
          ["metadata"].value("task_id", "");
  ASSERT_FALSE(id.empty());

  const JsonValue question =
      TaskTool::execute(JsonValue{{"action", "wait"}, {"timeout_s", 5}}, context);
  EXPECT_THAT(question.value("output", ""), testing::HasSubstr("[Message from " + id));
  EXPECT_THAT(question.value("output", ""), testing::HasSubstr("which port?"));

  const JsonValue answered = TaskTool::execute(
      JsonValue{{"action", "message"}, {"task_id", id}, {"prompt", "9196"}}, context);
  ASSERT_FALSE(answered.contains("error")) << answered.dump();

  const JsonValue report = TaskTool::execute(
      JsonValue{{"action", "wait"}, {"task_id", id}, {"timeout_s", 5}}, context);
  EXPECT_THAT(report.value("output", ""), testing::HasSubstr("got: [Message from lead]\n9196"));

  // A finished subagent cannot be messaged; the lead cannot message itself.
  EXPECT_THAT(TaskTool::execute(JsonValue{{"action", "message"}, {"task_id", id},
                                          {"prompt", "x"}}, context).value("error", ""),
              testing::HasSubstr("not running"));
  EXPECT_TRUE(TaskTool::execute(JsonValue{{"action", "message"}, {"task_id", "lead"},
                                          {"prompt", "x"}}, context).contains("error"));
  // With nothing running and nothing waiting, wait returns at once.
  EXPECT_THAT(TaskTool::execute(JsonValue{{"action", "wait"}}, context).value("output", ""),
              testing::HasSubstr("Nothing to wait for"));
}

// Several task calls in one message run concurrently through the executor.
TEST(TaskToolTest, ParallelCallsRunConcurrentlyThroughExecutor) {
  std::atomic<int> active{0};
  std::atomic<int> peak{0};
  std::mutex mu;
  std::set<std::thread::id> threads;

  GenerateOptions options;
  options.tools["task"] = TaskTool::definition();
  options.abort_flag = std::make_shared<std::atomic<bool>>(false);
  options.subagent_runner = [&](const JsonValue& args, std::shared_ptr<std::atomic<bool>>) {
    const int now = ++active;
    int prev = peak.load();
    while (now > prev && !peak.compare_exchange_weak(prev, now)) {}
    {
      std::lock_guard<std::mutex> lock(mu);
      threads.insert(std::this_thread::get_id());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    --active;
    return JsonValue{{"output", "done " + args.value("description", "")}};
  };

  std::vector<ToolCall> calls;
  for (int i = 0; i < 3; ++i) {
    ToolCall call;
    call.id = "call_" + std::to_string(i);
    call.tool_name = "task";
    call.arguments = {{"description", "job " + std::to_string(i)}, {"prompt", "work"}};
    calls.push_back(call);
  }

  const auto results =
      ToolExecutor::execute_tools_with_options(calls, options, /*parallel=*/true);
  ASSERT_EQ(results.size(), 3u);
  for (const auto& r : results) {
    ASSERT_TRUE(r.is_success()) << r.error_message();
    EXPECT_THAT(r.result.value("output", ""), testing::HasSubstr("done job"));
  }
  EXPECT_GE(peak.load(), 2);
  EXPECT_GE(threads.size(), 2u);
}

// A subagent is part of its parent's turn: it shows as running while it works,
// and stopping the parent session stops it.
TEST(TaskToolTest, RunningSubagentIsListedAndStoppedWithItsParent) {
  qcode::session::ensure_session_row("parent_stop_1", "Parent", "p", "m", "/ws");
  auto abort = std::make_shared<std::atomic<bool>>(false);
  ToolExecutionContext context;
  context.session_id = "parent_stop_1";
  context.abort_flag = abort;
  context.subagent_runner = [](const JsonValue&, std::shared_ptr<std::atomic<bool>> flag) {
    while (!flag->load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return JsonValue{{"error", "Subagent cancelled due to abort flag"}};
  };

  JsonValue out;
  std::thread worker([&] { out = TaskTool::execute(JsonValue{{"prompt", "long"}}, context); });

  std::string child;
  for (int i = 0; i < 400 && child.empty(); ++i) {
    const JsonValue listing = TaskTool::list_tasks("parent_stop_1");
    for (const auto& t : listing["metadata"]["tasks"]) {
      if (t.value("status", "") == "running") child = t.value("task_id", "");
    }
    if (child.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (child.empty()) {
    abort->store(true);
    worker.join();
    FAIL() << "subagent never listed as running; task returned: " << out.dump();
  }
  EXPECT_TRUE(TaskTool::is_session_running(child));

  TaskTool::delete_session_tasks("parent_stop_1");
  worker.join();
  EXPECT_TRUE(abort->load());
  EXPECT_TRUE(out.contains("error"));
  EXPECT_FALSE(TaskTool::is_session_running(child));
}

TEST(TaskToolTest, ListsFinishedChildSessionsFromDatabase) {
  const std::string parent = "parent_sess_abc123";
  qcode::session::ensure_session_row(parent, "Parent Chat", "openrouter", "model1", "/ws");
  qcode::session::ensure_session_row("ses_durable_child_1", "Audit simulation parity",
                                     "cursor", "composer-2.5", "/ws", parent);
  qcode::session::save_message("ses_durable_child_1", "Assistant", "Audit complete.");
  qcode::session::ensure_session_row("ses_durable_child_2", "Cut short", "cursor",
                                     "composer-2.5", "/ws", parent);
  qcode::session::save_message("ses_durable_child_2", "User", "start");

  std::map<std::string, std::string> status;
  const JsonValue listing = TaskTool::list_tasks(parent);
  for (const auto& t : listing["metadata"]["tasks"]) {
    status[t.value("task_id", "")] = t.value("status", "");
    EXPECT_EQ(t.value("parent_session_id", ""), parent);
  }
  EXPECT_EQ(status["ses_durable_child_1"], "done");
  EXPECT_EQ(status["ses_durable_child_2"], "interrupted");
}

// ── Cursor exec path: repair Cursor's Task protobuf leftovers ──

TEST(TaskToolTest, SwappedCursorTaskFieldsAreRepaired) {
  const JsonValue norm = TaskTool::normalize_spawn_args(JsonValue{
      {"prompt", "explore"},
      {"subagent_type", "cursor-grok-4.6"},
      {"model", "Read-only. Workspace: /home/abhi/project/qcode\n\nRead README.md"},
      {"description", "readme"},
  });
  EXPECT_THAT(norm.value("prompt", ""), testing::HasSubstr("README.md"));
  EXPECT_EQ(norm.value("model", ""), "cursor-grok-4.6");
  EXPECT_EQ(norm.value("description", ""), "readme");
}

TEST(TaskToolTest, PromptLikeModelBecomesThePrompt) {
  const JsonValue norm = TaskTool::normalize_spawn_args(JsonValue{
      {"prompt", "explore"},
      {"model", "Read-only. Workspace: /tmp"},
      {"description", "x"},
  });
  EXPECT_FALSE(norm.contains("model"));
  EXPECT_THAT(norm.value("prompt", ""), testing::HasSubstr("Workspace:"));
}

TEST(TaskToolTest, ReassemblesShreddedProtoPromptFields) {
  const std::string f10 =
      "AD-ONLY large nested session. Do not edit any files.\n\nInspect /home/a";
  const std::string f12 =
      "i/project/qcode with bash. Produce a LONG factual report covering TaskTool.";
  const JsonValue norm = TaskTool::normalize_spawn_args(JsonValue{
      {"prompt", "generalPurpose"},
      {"subagent_type", "composer-2.5-fast"},
      {"description", "call-abc\nfc_def"},
      {"10", f10},
      {"12", f12},
      {"4", 68},
      {"9", "c5a0606b85fc1aec05deb798dbce1f50"},
  });
  EXPECT_THAT(norm.value("prompt", ""), testing::HasSubstr("/home/ai/project/qcode"));
  EXPECT_THAT(norm.value("prompt", ""), testing::HasSubstr("TaskTool"));
  EXPECT_EQ(norm.value("model", ""), "composer-2.5-fast");
}

TEST(TaskToolTest, Field10AloneStillBecomesPromptWhenNamedIsModeToken) {
  const JsonValue norm = TaskTool::normalize_spawn_args(JsonValue{
      {"prompt", "explore"},
      {"10", "AD-ONLY large session test. Do not edit files. Workspace is /home/abh"},
  });
  EXPECT_THAT(norm.value("prompt", ""), testing::HasSubstr("Workspace is /home/abh"));
}

TEST(TaskToolTest, ProviderAndModelFieldsAreJoined) {
  const JsonValue norm = TaskTool::normalize_spawn_args(
      JsonValue{{"prompt", "scan the repo"}, {"provider", "openrouter"}, {"model", "x/y"}});
  EXPECT_EQ(norm.value("model", ""), "openrouter:x/y");
}

TEST(TaskToolTest, SessionIdParsedFromResult) {
  JsonValue result;
  result["output"] = "report";
  result["metadata"] = {{"sessionId", "ses_123abc"}, {"task_id", "ses_123abc"}};
  EXPECT_EQ(TaskTool::session_id_from_result(result), "ses_123abc");
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

TEST(MultiStepCoordinatorTest, AutoCompactsAndContinuesFromTheSummary) {
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
  opts.max_steps = 10;

  std::vector<Messages> requests;
  auto generate_func = [&](const GenerateOptions& step_opts) -> GenerateResult {
    requests.push_back(step_opts.messages);
    GenerateResult res;
    res.finish_reason = kFinishReasonStop;
    if (requests.size() == 1) {
      res.tool_calls.push_back(ToolCall("call_1", "calc_tool", JsonValue::object()));
      res.usage.prompt_tokens = 5000;  // over the threshold
    } else if (requests.size() == 2) {
      res.text = "## Tasks\n- report 42";  // the summarizer call
    } else {
      res.text = "Result is 42.";
    }
    return res;
  };
  std::vector<std::string> compacted;
  GenerateResult final_res = MultiStepCoordinator::execute_multi_step(
      opts, generate_func,
      MultiStepCoordinator::AutoCompact{
          .threshold = 1000,
          .on_compacted = [&](const std::string& m) { compacted.push_back(m); }});

  ASSERT_EQ(requests.size(), 3u);
  ASSERT_EQ(requests[1].size(), 4u);  // prompt, call, result, directive
  EXPECT_EQ(std::get<TextContentPart>(requests[1].back().content[0]).text,
            compaction::directive());
  ASSERT_EQ(requests[2].size(), 1u);
  const auto& text = std::get<TextContentPart>(requests[2][0].content[0]).text;
  EXPECT_EQ(text.rfind(compaction::kSummaryMarker, 0), 0u);
  EXPECT_NE(text.find("report 42"), std::string::npos);
  ASSERT_EQ(compacted.size(), 1u);
  EXPECT_EQ(compacted[0], text);
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
