// Tool-loop streaming: StreamAccumulator matches the non-streaming parser, the
// tool loop streams steps for clients that support it (and aborts promptly),
// and the TUI / server persist streamed text in timeline order.

#include "generation/stream_step.h"
#include "providers/openai/openai_response_parser.h"
#include "providers/openai/openai_stream.h"
#include "routes/session_runtime.h"

#include <qcode/core/event.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/generation/generation_service.h>
#include <qcode/providers/registry.h>
#include <qcode/session/session_store.h>
#include <qcode/ui/app_store.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace qcode {
namespace {

using namespace contract;

// ── StreamAccumulator parity with OpenAIResponseParser ──────────────

GenerateResult accumulate_sse(const std::vector<std::string>& chunks) {
  openai::OpenAIStreamImpl impl;
  for (const auto& chunk : chunks) impl.test_parse_sse_line("data: " + chunk);
  impl.test_parse_sse_line("data: [DONE]");
  StreamAccumulator step;
  while (auto event = impl.poll_event(std::chrono::milliseconds(0))) {
    step.add(*event);
  }
  return step.take();
}

std::string describe(const Message& message) {
  std::string out = std::to_string(static_cast<int>(message.role));
  for (const auto& part : message.content) {
    if (const auto* text = std::get_if<TextContentPart>(&part)) {
      out += "|text:" + text->text;
    } else if (const auto* call = std::get_if<ToolCallContentPart>(&part)) {
      out += "|call:" + call->id + "," + call->tool_name + "," +
             call->arguments.dump() + "," + call->thought_signature;
    } else if (const auto* thought = std::get_if<ReasoningContentPart>(&part)) {
      out += "|reasoning:" + thought->text + "," + thought->signature;
    } else {
      out += "|other";
    }
  }
  return out;
}

void expect_same_step(const GenerateResult& streamed,
                      const GenerateResult& parsed) {
  EXPECT_TRUE(streamed.is_success());
  EXPECT_EQ(streamed.text, parsed.text);
  EXPECT_EQ(streamed.reasoning, parsed.reasoning);
  EXPECT_EQ(streamed.finish_reason, parsed.finish_reason);
  EXPECT_EQ(streamed.usage.prompt_tokens, parsed.usage.prompt_tokens);
  EXPECT_EQ(streamed.usage.completion_tokens, parsed.usage.completion_tokens);
  EXPECT_EQ(streamed.usage.total_tokens, parsed.usage.total_tokens);
  EXPECT_EQ(streamed.usage.cached_prompt_tokens,
            parsed.usage.cached_prompt_tokens);
  EXPECT_EQ(streamed.usage.reasoning_completion_tokens,
            parsed.usage.reasoning_completion_tokens);
  ASSERT_EQ(streamed.tool_calls.size(), parsed.tool_calls.size());
  for (size_t i = 0; i < parsed.tool_calls.size(); ++i) {
    EXPECT_EQ(streamed.tool_calls[i].to_string(), parsed.tool_calls[i].to_string());
    EXPECT_EQ(streamed.tool_calls[i].thought_signature,
              parsed.tool_calls[i].thought_signature);
  }
  ASSERT_EQ(streamed.response_messages.size(), parsed.response_messages.size());
  for (size_t i = 0; i < parsed.response_messages.size(); ++i) {
    EXPECT_EQ(describe(streamed.response_messages[i]),
              describe(parsed.response_messages[i]));
  }
}

TEST(StreamAccumulatorTest, ToolStepMatchesNonStreamingParser) {
  const auto parsed = openai::OpenAIResponseParser().parse_success_completion_response(
      nlohmann::json::parse(R"({
        "id": "gen-1", "model": "m",
        "choices": [{"index": 0, "finish_reason": "tool_calls", "message": {
          "role": "assistant", "content": "Let me look.", "reasoning": "Need ls.",
          "reasoning_details": [{"type": "reasoning.text", "text": "Need ls.", "signature": "sig-1"}],
          "tool_calls": [{"id": "call_1", "type": "function", "thought_signature": "ts-1",
                          "function": {"name": "bash", "arguments": "{\"command\":\"ls\"}"}}]}}],
        "usage": {"prompt_tokens": 120, "completion_tokens": 30, "total_tokens": 150,
                  "prompt_tokens_details": {"cached_tokens": 100},
                  "completion_tokens_details": {"reasoning_tokens": 7}}})"));

  const auto streamed = accumulate_sse({
      R"({"choices":[{"index":0,"delta":{"role":"assistant","reasoning":"Need ","reasoning_details":[{"type":"reasoning.text","text":"Need ","signature":null}]}}]})",
      R"({"choices":[{"index":0,"delta":{"reasoning":"ls.","reasoning_details":[{"type":"reasoning.text","text":"ls.","signature":"sig-1"}]}}]})",
      R"({"choices":[{"index":0,"delta":{"content":"Let me "}}]})",
      R"({"choices":[{"index":0,"delta":{"content":"look."}}]})",
      R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","thought_signature":"ts-1","function":{"name":"bash","arguments":"{\"comm"}}]}}]})",
      R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"function":{"arguments":"and\":\"ls\"}"}}]}}]})",
      R"({"choices":[{"index":0,"delta":{},"finish_reason":"tool_calls"}],"usage":null})",
      R"({"choices":[],"usage":{"prompt_tokens":120,"completion_tokens":30,"total_tokens":150,"prompt_tokens_details":{"cached_tokens":100},"completion_tokens_details":{"reasoning_tokens":7}}})",
  });

  expect_same_step(streamed, parsed);
  EXPECT_EQ(streamed.finish_reason, kFinishReasonToolCalls);
  ASSERT_EQ(streamed.tool_calls.size(), 1u);
  EXPECT_EQ(streamed.tool_calls[0].thought_signature, "ts-1");
}

TEST(StreamAccumulatorTest, TextStepWithReasoningMatchesNonStreamingParser) {
  const auto parsed = openai::OpenAIResponseParser().parse_success_completion_response(
      nlohmann::json::parse(R"({
        "choices": [{"index": 0, "finish_reason": "length", "message": {
          "role": "assistant", "content": "Hi there.", "reasoning_content": "Greet."}}],
        "usage": {"prompt_tokens": 9, "completion_tokens": 4, "total_tokens": 13}})"));

  const auto streamed = accumulate_sse({
      R"({"choices":[{"index":0,"delta":{"reasoning_content":"Greet."}}]})",
      R"({"choices":[{"index":0,"delta":{"content":"Hi there."},"finish_reason":"length"}],"usage":{"prompt_tokens":9,"completion_tokens":4,"total_tokens":13}})",
  });

  expect_same_step(streamed, parsed);
  EXPECT_EQ(streamed.finish_reason, kFinishReasonLength);
}

TEST(StreamAccumulatorTest, UnparsableToolArgumentsDropTheCallLikeTheParser) {
  StreamAccumulator step;
  step.add(StreamEvent::tool_call("call_1", "bash", "{\"command\":"));
  step.add(StreamEvent::tool_call("call_2", "bash", ""));
  step.add(StreamEvent(kStreamEventTypeFinish));
  const auto result = step.take();
  ASSERT_EQ(result.tool_calls.size(), 1u);
  EXPECT_EQ(result.tool_calls[0].id, "call_2");
  EXPECT_EQ(result.tool_calls[0].arguments, nlohmann::json::object());
}

TEST(StreamAccumulatorTest, ErrorIsClassifiedAndKeepsStreamedText) {
  StreamAccumulator step;
  step.add(StreamEvent("partial"));
  step.add(StreamEvent(kStreamEventTypeError, "HTTP 429 error: rate limit exceeded"));
  const auto result = step.take();
  EXPECT_FALSE(result.is_success());
  EXPECT_EQ(result.error_message(), "HTTP 429 error: rate limit exceeded");
  EXPECT_EQ(result.is_retryable, std::optional<bool>(true));
  EXPECT_EQ(result.text, "partial");
}

// ── Tool loop over a scripted streaming client ───────────────────────

// Plays back a step's events; a stream with no script stays open and idle
// until it is stopped, like a model that has not answered yet.
class ScriptedStream : public internal::StreamResultImpl {
 public:
  explicit ScriptedStream(std::vector<StreamEvent> events, bool idle = false)
      : events_(events.begin(), events.end()), idle_(idle) {}

  StreamEvent get_next_event() override {
    auto event = poll_event(std::chrono::hours(1));
    return event ? *event : StreamEvent(kStreamEventTypeFinish);
  }
  bool has_more_events() const override {
    return !events_.empty() || (idle_ && !stopped_);
  }
  void stop_stream() override { stopped_ = true; }
  std::optional<StreamEvent> poll_event(std::chrono::milliseconds timeout) override {
    if (!events_.empty()) {
      StreamEvent event = std::move(events_.front());
      events_.pop_front();
      return event;
    }
    if (idle_ && !stopped_) std::this_thread::sleep_for(timeout);
    return std::nullopt;
  }

 private:
  std::deque<StreamEvent> events_;
  bool idle_;
  std::atomic<bool> stopped_{false};
};

struct Script {
  std::vector<std::vector<StreamEvent>> steps;
  std::atomic<int> streams{0};
  std::atomic<int> generate_calls{0};
};

class FakeStreamingClient : public Client {
 public:
  explicit FakeStreamingClient(std::shared_ptr<Script> script)
      : script_(std::move(script)) {}

  GenerateResult generate_text(const GenerateOptions&) override {
    ++script_->generate_calls;
    return GenerateResult("generate_text must not be called");
  }
  StreamResult stream_text(const StreamOptions&) override {
    const int step = script_->streams++;
    if (step < static_cast<int>(script_->steps.size())) {
      return StreamResult(std::make_unique<ScriptedStream>(script_->steps[step]));
    }
    return StreamResult(std::make_unique<ScriptedStream>(
        std::vector<StreamEvent>{}, /*idle=*/true));
  }
  bool supports_tool_streaming() const override { return true; }
  bool is_valid() const override { return true; }
  std::string provider_name() const override { return "fake"; }

 private:
  std::shared_ptr<Script> script_;
};

class StreamingToolLoopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto dir = std::filesystem::temp_directory_path() / "qcode_stream_step_test";
    std::filesystem::create_directories(dir);
    workspace_ = dir.string();
    db_path_ = (dir / ("test_" + std::to_string(::getpid()) + "_" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()) +
                       ".db"))
                   .string();
    setenv("QCODE_DB_PATH", db_path_.c_str(), 1);
    session::init_database();

    script_ = std::make_shared<Script>();
    providers::ProviderRegistry::instance().register_provider(
        "fake-stream", [script = script_](const providers::ProviderOptions&) {
          return providers::ClientResolution{
              Client(std::make_unique<FakeStreamingClient>(script)), ""};
        });
    ModelInfo model;
    model.name = model.id = "m";
    model.tool_call = true;
    ProviderInfo provider;
    provider.name = "Fake";
    provider.id = "fake-stream";
    provider.models = {model};
    providers_.push_back(provider);

    contract::register_all_events(bus_);
    record<MessageDelta>();
    record<ToolCallStarted>();
    record<ToolCallCompleted>();
    record<ErrorOccurred>();
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove(db_path_, ec);
    std::filesystem::remove(db_path_ + "-wal", ec);
    std::filesystem::remove(db_path_ + "-shm", ec);
    unsetenv("QCODE_DB_PATH");
  }

  // Records "<event>:<detail>" for each delivered event of type E.
  template <typename E>
  void record() {
    subs_.push_back(bus_.subscribe<E>([this](const typename E::Payload& p) {
      std::lock_guard<std::mutex> lock(log_mutex_);
      if constexpr (std::is_same_v<E, MessageDelta>) {
        log_.push_back(p.done ? "done" : "text:" + p.text);
      } else if constexpr (std::is_same_v<E, ToolCallStarted>) {
        log_.push_back("started:" + p.tool_name);
      } else if constexpr (std::is_same_v<E, ToolCallCompleted>) {
        log_.push_back("completed:" + p.tool_name);
      } else {
        if (p.severity != "info") log_.push_back("error:" + p.message);
      }
    }));
  }

  GenerationContext make_ctx() const {
    GenerationContext ctx;
    ctx.agent_mode = "build";
    ctx.workspace = workspace_;
    return ctx;
  }

  void run(GenerationContext& ctx) {
    run_generation_with_bus("fake-stream", "m", "You are a test.",
                            {Message::user("list files")}, /*enable_tools=*/true,
                            providers_, bus_, ctx);
  }

  std::shared_ptr<Script> script_;
  std::vector<ProviderInfo> providers_;
  bus::BusRuntime bus_;
  std::vector<bus::Subscription> subs_;
  std::mutex log_mutex_;
  std::vector<std::string> log_;
  std::string workspace_;
  std::string db_path_;
};

TEST_F(StreamingToolLoopTest, StreamsEachStepInTimelineOrder) {
  StreamEvent finish_tools(kStreamEventTypeFinish);
  finish_tools.finish_reason = kFinishReasonToolCalls;
  script_->steps = {
      {StreamEvent("Let me "), StreamEvent("look."),
       StreamEvent::tool_call("call_1", "bash", R"({"command":"echo hi"})"),
       finish_tools},
      {StreamEvent("Done."), StreamEvent(kStreamEventTypeFinish)},
  };
  GenerationContext ctx = make_ctx();
  run(ctx);
  bus_.drain();

  EXPECT_EQ(script_->generate_calls, 0);
  EXPECT_EQ(script_->streams, 2);
  const std::vector<std::string> expected{
      "text:Let me look.", "started:bash", "completed:bash", "text:Done.", "done"};
  EXPECT_EQ(log_, expected);
}

TEST_F(StreamingToolLoopTest, AbortWhileTheStreamIsIdleReturnsPromptly) {
  GenerationContext ctx = make_ctx();
  std::chrono::steady_clock::time_point aborted_at;
  std::thread esc([&ctx, &aborted_at] {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    aborted_at = std::chrono::steady_clock::now();
    ctx.abort_flag->store(true);
  });
  run(ctx);
  const auto returned_at = std::chrono::steady_clock::now();
  esc.join();
  bus_.drain();

  EXPECT_LT(returned_at - aborted_at, std::chrono::seconds(1));
  EXPECT_EQ(script_->generate_calls, 0);
  ASSERT_FALSE(log_.empty());
  EXPECT_EQ(log_.front(), "error:Generation stopped");
}

// ── Persistence order of streamed text ───────────────────────────────

class StreamPersistenceTest : public StreamingToolLoopTest {
 protected:
  void publish_tool_step(const std::string& sid, const std::string& post_text) {
    bus_.publish<MessageDelta>({.session_id = sid, .text = "Let me look.", .done = false});
    bus_.publish<ToolCallStarted>({.session_id = sid, .tool_call_id = "call_1",
                                   .tool_name = "bash",
                                   .arguments = {{"command", "ls"}}});
    bus_.publish<ToolCallCompleted>({.session_id = sid, .tool_call_id = "call_1",
                                     .tool_name = "bash", .result = {{"output", "a"}},
                                     .is_error = false, .duration_ms = 1.0});
    if (!post_text.empty()) {
      bus_.publish<MessageDelta>({.session_id = sid, .text = post_text, .done = false});
    }
    bus_.publish<MessageDelta>({.session_id = sid, .text = "", .done = true});
    bus_.drain();
  }

  static std::vector<std::string> rows_of(const std::string& sid) {
    std::vector<std::string> rows;
    for (const auto& [role, text] : session::load_session_messages(sid)) {
      rows.push_back(role == "Assistant" || role == "Reasoning" ? role + ":" + text
                                                                : role);
    }
    return rows;
  }
};

TEST_F(StreamPersistenceTest, TuiSavesTextBeforeTheToolCallItPreceded) {
  AppStore store(bus_);
  store.wire();
  const std::string sid = session::create_new_session("prov", "m", workspace_);
  store.set_session_id(sid);

  publish_tool_step(sid, "Done.");
  EXPECT_EQ(rows_of(sid), (std::vector<std::string>{
                              "Assistant:Let me look.", "ToolCall", "ToolResult",
                              "Assistant:Done."}));
}

TEST_F(StreamPersistenceTest, TuiDoesNotSaveFlushedTextAgainAtTurnEnd) {
  AppStore store(bus_);
  store.wire();
  const std::string sid = session::create_new_session("prov", "m", workspace_);
  store.set_session_id(sid);

  publish_tool_step(sid, "");
  EXPECT_EQ(rows_of(sid), (std::vector<std::string>{
                              "Assistant:Let me look.", "ToolCall", "ToolResult"}));
}

TEST_F(StreamPersistenceTest, ServerSavesReasoningThenTextThenToolCall) {
  const std::string sid = session::create_new_session("prov", "m", workspace_);
  auto session = std::make_shared<server::GenSession>();
  session->id = sid;
  auto subs = server::subscribe_session(bus_, session);

  bus_.publish<ReasoningDelta>({.session_id = sid, .text = "Need ls.", .signature = "", .done = false});
  bus_.publish<MessageDelta>({.session_id = sid, .text = "Let me look.", .done = false});
  bus_.publish<ToolCallStarted>({.session_id = sid, .tool_call_id = "call_1",
                                 .tool_name = "bash", .arguments = {{"command", "ls"}}});
  bus_.drain();

  EXPECT_EQ(rows_of(sid), (std::vector<std::string>{
                              "Reasoning:Need ls.", "Assistant:Let me look.", "ToolCall"}));
}

}  // namespace
}  // namespace qcode
