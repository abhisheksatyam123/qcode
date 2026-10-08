#include "providers/anthropic/anthropic_stream.h"
#include "generation/stream_step.h"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

namespace qcode {
namespace {

using anthropic::AnthropicStreamImpl;

std::vector<StreamEvent> drain(AnthropicStreamImpl& s) {
  std::vector<StreamEvent> out;
  while (auto ev = s.poll_event(std::chrono::milliseconds(0))) {
    out.push_back(*ev);
  }
  return out;
}

TEST(AnthropicSseTest, ToolStepWithThinkingAndCachedUsage) {
  AnthropicStreamImpl s;
  s.feed_sse_for_test(R"({"type":"message_start","message":{"usage":{"input_tokens":5,"cache_creation_input_tokens":0,"cache_read_input_tokens":1000,"output_tokens":1}}})");
  s.feed_sse_for_test(R"({"type":"content_block_start","index":0,"content_block":{"type":"thinking","thinking":""}})");
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"plan"}})");
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":0,"delta":{"type":"signature_delta","signature":"SIG"}})");
  s.feed_sse_for_test(R"({"type":"content_block_stop","index":0})");
  s.feed_sse_for_test(R"({"type":"ping"})");
  s.feed_sse_for_test(R"({"type":"content_block_start","index":1,"content_block":{"type":"text","text":""}})");
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":1,"delta":{"type":"text_delta","text":"Running."}})");
  s.feed_sse_for_test(R"({"type":"content_block_stop","index":1})");
  s.feed_sse_for_test(R"({"type":"content_block_start","index":2,"content_block":{"type":"tool_use","id":"toolu_1","name":"bash","input":{}}})");
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":2,"delta":{"type":"input_json_delta","partial_json":"{\"command\": \"ls"}})");
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":2,"delta":{"type":"input_json_delta","partial_json":" -la\"}"}})");
  s.feed_sse_for_test(R"({"type":"content_block_stop","index":2})");
  s.feed_sse_for_test(R"({"type":"message_delta","delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":50,"output_tokens_details":{"thinking_tokens":30}}})");
  s.feed_sse_for_test(R"({"type":"message_stop"})");

  StreamAccumulator acc;
  for (const auto& ev : drain(s)) acc.add(ev);
  GenerateResult r = acc.take();

  ASSERT_TRUE(r.is_success()) << r.error_message();
  EXPECT_EQ(r.reasoning, "plan");
  EXPECT_EQ(r.text, "Running.");
  ASSERT_EQ(r.tool_calls.size(), 1u);
  EXPECT_EQ(r.tool_calls[0].id, "toolu_1");
  EXPECT_EQ(r.tool_calls[0].tool_name, "bash");
  EXPECT_EQ(r.tool_calls[0].arguments.value("command", ""), "ls -la");
  EXPECT_EQ(r.finish_reason, kFinishReasonToolCalls);
  EXPECT_EQ(r.usage.prompt_tokens, 1005);
  EXPECT_EQ(r.usage.cached_prompt_tokens, 1000);
  EXPECT_EQ(r.usage.completion_tokens, 50);
  EXPECT_EQ(r.usage.reasoning_completion_tokens, 30);
  // The assistant message keeps the signed thinking for replay.
  ASSERT_EQ(r.response_messages.size(), 1u);
  EXPECT_TRUE(r.response_messages[0].has_tool_calls());
  EXPECT_TRUE(r.response_messages[0].has_reasoning());
}

TEST(AnthropicSseTest, StartBlockInputUsedWhenNoDeltas) {
  AnthropicStreamImpl s;
  s.feed_sse_for_test(R"({"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"a","name":"read","input":{"path":"x"}}})");
  s.feed_sse_for_test(R"({"type":"content_block_stop","index":0})");
  s.feed_sse_for_test(R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"b","name":"list","input":{}}})");
  s.feed_sse_for_test(R"({"type":"content_block_stop","index":1})");
  auto ev = drain(s);
  ASSERT_EQ(ev.size(), 2u);
  EXPECT_TRUE(ev[0].is_tool_call());
  EXPECT_EQ(ev[0].tool_payload, R"({"path":"x"})");
  EXPECT_EQ(ev[1].tool_payload, "{}");
}

TEST(AnthropicSseTest, InterleavedToolBlocks) {
  AnthropicStreamImpl s;
  s.feed_sse_for_test(R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"1","name":"t1","input":{}}})");
  s.feed_sse_for_test(R"({"type":"content_block_start","index":2,"content_block":{"type":"tool_use","id":"2","name":"t2","input":{}}})");
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"a\":"}})");
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":2,"delta":{"type":"input_json_delta","partial_json":"{\"b\":2}"}})");
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"1}"}})");
  s.feed_sse_for_test(R"({"type":"content_block_stop","index":2})");
  s.feed_sse_for_test(R"({"type":"content_block_stop","index":1})");
  auto ev = drain(s);
  ASSERT_EQ(ev.size(), 2u);
  EXPECT_EQ(ev[0].tool_call_id, "2");
  EXPECT_EQ(ev[0].tool_payload, R"({"b":2})");
  EXPECT_EQ(ev[1].tool_call_id, "1");
  EXPECT_EQ(ev[1].tool_payload, R"({"a":1})");
}

TEST(AnthropicSseTest, ErrorAfterContentIsSurfaced) {
  AnthropicStreamImpl s;
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"hi"}})");
  s.feed_sse_for_test(R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})");
  auto ev = drain(s);
  ASSERT_EQ(ev.size(), 2u);
  EXPECT_TRUE(ev[1].is_error());
  EXPECT_NE(ev[1].error.value_or("").find("overloaded_error"), std::string::npos);
}

TEST(AnthropicSseTest, NonRetryableErrorBeforeContentIsSurfaced) {
  AnthropicStreamImpl s;
  s.feed_sse_for_test(R"({"type":"error","error":{"type":"invalid_request_error","message":"bad"}})");
  auto ev = drain(s);
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_TRUE(ev[0].is_error());
}

TEST(AnthropicSseTest, MaxTokensStopMapsToLength) {
  AnthropicStreamImpl s;
  s.feed_sse_for_test(R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"x"}})");
  s.feed_sse_for_test(R"({"type":"message_delta","delta":{"stop_reason":"max_tokens"},"usage":{"output_tokens":9}})");
  s.feed_sse_for_test(R"({"type":"message_stop"})");
  auto ev = drain(s);
  ASSERT_EQ(ev.size(), 2u);
  EXPECT_TRUE(ev[1].is_finish());
  EXPECT_EQ(ev[1].finish_reason, kFinishReasonLength);
}

}  // namespace
}  // namespace qcode
