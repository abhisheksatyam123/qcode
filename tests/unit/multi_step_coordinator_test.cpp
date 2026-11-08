#include <qcode/core/message.h>
#include <qcode/core/tool.h>
#include <qcode/session/session_store.h>
#include <qcode/tools/multi_step_coordinator.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace qcode {
namespace {

class MultiStepCoordinatorTest : public ::testing::Test {
 protected:
    void SetUp() override {
        std::error_code ec;
        std::filesystem::create_directories("/tmp/qcode_multi_step_test", ec);
        db_path_ = "/tmp/qcode_multi_step_test/test_" +
                   std::to_string(::getpid()) + "_" +
                   std::to_string(
                       std::chrono::steady_clock::now().time_since_epoch().count()) +
                   ".db";
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(db_path_ + "-wal", ec);
        std::filesystem::remove(db_path_ + "-shm", ec);
        setenv("QCODE_DB_PATH", db_path_.c_str(), 1);
        session::init_database();
        sid_ = session::create_new_session("test", "test-model", "/tmp");
        ASSERT_FALSE(sid_.empty());
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(db_path_ + "-wal", ec);
        std::filesystem::remove(db_path_ + "-shm", ec);
        unsetenv("QCODE_DB_PATH");
    }

    std::string db_path_;
    std::string sid_;
};

// A subagent tool loop must persist each step's thinking before its tool rows
// and replay it (with signatures) in the next request.
TEST_F(MultiStepCoordinatorTest, PersistsAndReplaysReasoningAcrossToolSteps) {
    GenerateOptions options("test-model", "do the thing");
    options.max_steps = 10;
    options.session_id = sid_;
    options.tools["echo"] = Tool(
        "echo", nlohmann::json::object(),
        [](const nlohmann::json& args, const ToolExecutionContext&) {
            return nlohmann::json{{"echoed", args.value("value", "")}};
        });
    options.on_tool_call_start = [this](const ToolCall& call) {
        session::save_message(
            sid_, "ToolCall",
            nlohmann::json{{"id", call.id},
                           {"name", call.tool_name},
                           {"arguments", call.arguments}}
                .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    };
    options.on_tool_call_finish = [this](const ToolResult& result) {
        session::save_message(
            sid_, "ToolResult",
            nlohmann::json{{"tool_call_id", result.tool_call_id},
                           {"tool_name", result.tool_name},
                           {"result", result.result},
                           {"is_error", !result.is_success()},
                           {"duration_ms", 0}}
                .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    };

    int calls = 0;
    bool replayed_reasoning = false;
    bool replayed_signature = false;
    GenerateResult result = MultiStepCoordinator::execute_multi_step(
        options, [&](const GenerateOptions& step_options) {
            ++calls;
            if (calls == 1) {
                GenerateResult step;
                step.text = "checking";
                step.reasoning = "first step thinking";
                step.finish_reason = kFinishReasonToolCalls;
                step.tool_calls.emplace_back(
                    "call-1", "echo", nlohmann::json{{"value", "hi"}},
                    "call-sig");
                return step;
            }
            // The second request must replay step one's thinking and its
            // provider thought signature next to the tool call.
            for (const auto& message : step_options.messages) {
                if (message.role != kMessageRoleAssistant ||
                    !message.has_tool_calls()) {
                    continue;
                }
                replayed_reasoning =
                    message.get_reasoning() == "first step thinking";
                replayed_signature =
                    message.get_tool_calls().at(0).thought_signature ==
                    "call-sig";
            }
            GenerateResult step;
            step.text = "finished";
            step.reasoning = "second step thinking";
            step.finish_reason = kFinishReasonStop;
            return step;
        });

    EXPECT_EQ(calls, 2);
    EXPECT_TRUE(replayed_reasoning);
    EXPECT_TRUE(replayed_signature);
    EXPECT_EQ(result.finish_reason, kFinishReasonStop);
    EXPECT_EQ(result.text, "checkingfinished");
    ASSERT_EQ(result.steps.size(), 2u);
    ASSERT_FALSE(result.response_messages.empty());
    EXPECT_TRUE(result.response_messages.front().has_reasoning());
    EXPECT_EQ(result.response_messages.front().get_reasoning(),
              "first step thinking");

    const auto rows = session::load_session_messages(sid_);
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].first, "Reasoning");
    EXPECT_EQ(rows[0].second, "first step thinking");
    EXPECT_EQ(rows[1].first, "ToolCall");
    EXPECT_EQ(rows[2].first, "ToolResult");
    EXPECT_EQ(rows[3].first, "Reasoning");
    EXPECT_EQ(rows[3].second, "second step thinking");

    const auto history = session::load_session_history_parsed(sid_);
    ASSERT_EQ(history.size(), 3u);
    EXPECT_TRUE(history[0].has_reasoning());
    EXPECT_EQ(history[0].get_reasoning(), "first step thinking");
    ASSERT_EQ(history[0].get_tool_calls().size(), 1u);
    EXPECT_EQ(history[0].get_tool_calls()[0].id, "call-1");
    EXPECT_TRUE(history[1].has_tool_results());
    EXPECT_TRUE(history[2].has_reasoning());
    EXPECT_EQ(history[2].get_reasoning(), "second step thinking");
}

// Provider parsers that return signed thinking only on the response message
// (Anthropic) must keep the signature in the persisted row.
TEST_F(MultiStepCoordinatorTest, PersistsSignedReasoningFromResponseMessage) {
    GenerateOptions options("test-model", "answer signed");
    options.max_steps = 5;
    options.session_id = sid_;

    const GenerateResult result = MultiStepCoordinator::execute_multi_step(
        options, [](const GenerateOptions&) {
            GenerateResult step;
            step.text = "answer";
            step.finish_reason = kFinishReasonStop;
            step.response_messages.push_back(
                Message::assistant_with_reasoning("answer", "signed thinking",
                                                  "sig-42"));
            return step;
        });

    EXPECT_EQ(result.finish_reason, kFinishReasonStop);
    // TaskTool saves the subagent's final answer after the turn returns.
    session::save_message(sid_, "Assistant", "answer");

    const auto rows = session::load_session_messages(sid_);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].first, "Reasoning");
    EXPECT_NE(rows[0].second.find("sig-42"), std::string::npos);
    EXPECT_EQ(rows[1].first, "Assistant");

    const auto history = session::load_session_history_parsed(sid_);
    ASSERT_EQ(history.size(), 1u);
    EXPECT_TRUE(history[0].has_reasoning());
    EXPECT_EQ(history[0].get_reasoning(), "signed thinking");
    EXPECT_EQ(history[0].get_text(), "answer");
    ASSERT_EQ(history[0].content.size(), 2u);
    const auto* reasoning =
        std::get_if<ReasoningContentPart>(&history[0].content[0]);
    ASSERT_NE(reasoning, nullptr);
    EXPECT_EQ(reasoning->signature, "sig-42");
}

// Child and orchestrator sessions are independent: thinking is written only
// to the child session the task tool created. The orchestrator sees just the
// child's output as a tool-result message.
TEST_F(MultiStepCoordinatorTest, ReasoningStaysInTheChildSession) {
    const std::string parent_sid =
        session::create_new_session("test", "test-model", "/tmp", "Parent");
    ASSERT_FALSE(parent_sid.empty());
    const std::string child_sid =
        session::create_new_session("test", "test-model", "/tmp", "Child");
    ASSERT_FALSE(child_sid.empty());

    GenerateOptions options("test-model", "child work");
    options.max_steps = 5;
    options.session_id = child_sid;
    const GenerateResult result = MultiStepCoordinator::execute_multi_step(
        options, [](const GenerateOptions&) {
            GenerateResult step;
            step.text = "child answer";
            step.reasoning = "child thinking";
            step.finish_reason = kFinishReasonStop;
            return step;
        });

    // TaskTool saves the final answer to the child and hands the orchestrator
    // only the output text (persisted as a tool result on the parent side).
    session::save_message(child_sid, "Assistant", result.text);
    session::save_message(
        parent_sid, "ToolResult",
        R"({"tool_call_id":"task-1","result":{"output":"child answer"},"is_error":false})");

    const auto child_rows = session::load_session_messages(child_sid);
    ASSERT_EQ(child_rows.size(), 2u);
    EXPECT_EQ(child_rows[0].first, "Reasoning");
    EXPECT_EQ(child_rows[0].second, "child thinking");
    EXPECT_EQ(child_rows[1].first, "Assistant");

    const auto parent_rows = session::load_session_messages(parent_sid);
    ASSERT_EQ(parent_rows.size(), 1u);
    EXPECT_EQ(parent_rows[0].first, "ToolResult");
    EXPECT_EQ(parent_rows[0].second.find("child thinking"), std::string::npos);
}

// The max_steps == 1 fast path is still a subagent turn and must persist its
// thinking.
TEST_F(MultiStepCoordinatorTest, SingleStepPersistsReasoning) {
    GenerateOptions options("test-model", "single step");
    options.max_steps = 1;
    options.session_id = sid_;

    MultiStepCoordinator::execute_multi_step(
        options, [](const GenerateOptions&) {
            GenerateResult step;
            step.text = "done";
            step.reasoning = "single step thinking";
            step.finish_reason = kFinishReasonStop;
            return step;
        });

    const auto rows = session::load_session_messages(sid_);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].first, "Reasoning");
    EXPECT_EQ(rows[0].second, "single step thinking");
}

}  // namespace
}  // namespace qcode
