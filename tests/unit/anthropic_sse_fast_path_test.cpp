// Contract test for the text-delta fast path in the Anthropic SSE parser: the
// same stream must produce the same events with the fast path on and off.
#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include <qcode/core/stream_event.h>

#include "providers/anthropic/anthropic_stream.h"

namespace qcode {
namespace {

struct Seen {
  StreamEventType type;
  std::string text;
  bool operator==(const Seen&) const = default;
};

std::vector<Seen> run(const std::vector<std::string>& events, bool fast) {
  anthropic::AnthropicStreamImpl stream;
  stream.set_fast_delta_path_for_test(fast);
  for (const auto& event : events) stream.feed_sse_for_test(event);
  // message_stop pushes the finish event on both paths, so the stream is
  // consistent when drained.
  stream.feed_sse_for_test(R"({"type":"message_stop"})");
  std::vector<Seen> out;
  while (auto ev = stream.poll_event(std::chrono::milliseconds(0))) {
    out.push_back({ev->type, ev->text_delta});
  }
  return out;
}

void expect_same(const std::vector<std::string>& events) {
  EXPECT_EQ(run(events, true), run(events, false));
}

TEST(AnthropicSseFastPathTest, CompactTextDeltasMatchFullParse) {
  expect_same({
      R"({"type":"message_start","message":{"id":"m","usage":{"input_tokens":3,"output_tokens":1}}})",
      R"({"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})",
      R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}})",
      R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":" world"}})",
      R"({"type":"content_block_stop","index":0})",
      R"({"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":2}})",
  });
}

TEST(AnthropicSseFastPathTest, SpacedAndMultiDigitIndexMatchFullParse) {
  expect_same({
      R"({"type": "content_block_delta", "index": 12, "delta": {"type": "text_delta", "text": "spaced"}})",
  });
}

TEST(AnthropicSseFastPathTest, EscapesMatchFullParse) {
  expect_same({
      R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"a \"quote\" \\ back\nline\tTab \/ slash"}})",
      R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"raw UTF-8 é 漢"}})",
  });
}

TEST(AnthropicSseFastPathTest, ThinkingAndToolDeltasMatchFullParse) {
  expect_same({
      R"({"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"hmm"}})",
      R"({"type":"content_block_delta","index":0,"delta":{"type":"signature_delta","signature":"sig"}})",
      R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"t1","name":"bash","input":{}}})",
      R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"cmd\":\"ls\"}"}})",
      R"({"type":"content_block_stop","index":1})",
  });
}

TEST(AnthropicSseFastPathTest, UnicodeEscapeFallsBackAndStillDecodes) {
  expect_same({
      R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"café"}})",
  });
}

TEST(AnthropicSseFastPathTest, PlainDeltaDecodesToItsText) {
  const auto events = run(
      {R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"ok"}})"}, true);
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events[0].text, "ok");
}

}  // namespace
}  // namespace qcode
