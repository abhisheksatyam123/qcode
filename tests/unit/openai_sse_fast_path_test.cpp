// Contract test for the plain-content-delta fast path in the OpenAI SSE
// parser: the same stream must produce the same events with the fast path on
// and off. Covers the shapes the fast path takes, the shapes it must leave to
// the full parser, and escapes.
#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <qcode/core/stream_event.h>

#include "providers/openai/openai_stream.h"

namespace qcode {
namespace {

struct Seen {
  StreamEventType type;
  std::string text;
  bool operator==(const Seen&) const = default;
};

std::vector<Seen> run(const std::vector<std::string>& lines, bool fast) {
  openai::OpenAIStreamImpl stream;
  stream.test_set_fast_delta_path(fast);
  for (const auto& line : lines) stream.test_parse_sse_line(line);
  // [DONE] completes the stream, so draining ends instead of reporting an
  // idle timeout. It also pushes the same finish event on both paths.
  stream.test_parse_sse_line("data: [DONE]");
  std::vector<Seen> out;
  while (auto ev = stream.poll_event(std::chrono::milliseconds(0))) {
    out.push_back({ev->type, ev->text_delta});
  }
  return out;
}

void expect_same(const std::vector<std::string>& lines) {
  EXPECT_EQ(run(lines, true), run(lines, false));
}

TEST(OpenAISseFastPathTest, CompactChunksMatchFullParse) {
  expect_same({
      R"(data: {"id":"c1","object":"chat.completion.chunk","created":1,"model":"m","choices":[{"index":0,"delta":{"role":"assistant","content":""},"logprobs":null,"finish_reason":null}]})",
      R"(data: {"id":"c1","object":"chat.completion.chunk","created":1,"model":"m","choices":[{"index":0,"delta":{"content":"Hello"},"logprobs":null,"finish_reason":null}]})",
      R"(data: {"id":"c1","object":"chat.completion.chunk","created":1,"model":"m","choices":[{"index":0,"delta":{"content":" world"},"finish_reason":null}]})",
      R"(data: {"id":"c1","object":"chat.completion.chunk","created":1,"model":"m","choices":[{"index":0,"delta":{"content":""},"finish_reason":null}]})",
  });
}

TEST(OpenAISseFastPathTest, SpacedChunksMatchFullParse) {
  expect_same({
      R"(data: {"choices": [{"index": 0, "delta": {"content": "word1 "}, "finish_reason": null}]})",
      R"(data: {"choices": [{"index": 0, "delta": {"content": "word2"}, "logprobs": null, "finish_reason": null}]})",
  });
}

TEST(OpenAISseFastPathTest, EscapesMatchFullParse) {
  expect_same({
      R"(data: {"choices":[{"index":0,"delta":{"content":"say \"hi\"\\ now\nnext\tcol\/x"},"finish_reason":null}]})",
      R"(data: {"choices":[{"index":0,"delta":{"content":"unicode raw: é 漢"},"finish_reason":null}]})",
  });
}

TEST(OpenAISseFastPathTest, FinishAndUsageChunksMatchFullParse) {
  expect_same({
      R"(data: {"choices":[{"index":0,"delta":{"content":"done"},"finish_reason":null}]})",
      R"(data: {"choices":[{"index":0,"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":3,"completion_tokens":1,"total_tokens":4}})",
      "data: [DONE]",
  });
}

TEST(OpenAISseFastPathTest, ToolCallAndReasoningChunksMatchFullParse) {
  expect_same({
      R"(data: {"choices":[{"index":0,"delta":{"content":"I will look."},"finish_reason":null}]})",
      R"(data: {"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"bash","arguments":"{}"}}]},"finish_reason":null}]})",
      R"(data: {"choices":[{"index":0,"delta":{"reasoning_content":"thinking about it"},"finish_reason":null}]})",
  });
}

TEST(OpenAISseFastPathTest, UnicodeEscapeFallsBackAndStillDecodes) {
  // \u escapes are not decoded by the fast path, so the full parser handles
  // them; the result must match.
  expect_same({
      R"(data: {"choices":[{"index":0,"delta":{"content":"café"},"finish_reason":null}]})",
  });
}

TEST(OpenAISseFastPathTest, PlainDeltaDecodesToItsText) {
  // A plain delta decodes to its text on the default path.
  const std::vector<std::string> lines = {
      R"(data: {"choices":[{"index":0,"delta":{"content":"ok"},"finish_reason":null}]})"};
  const auto fast = run(lines, true);
  ASSERT_FALSE(fast.empty());
  EXPECT_EQ(fast[0].text, "ok");
}

}  // namespace
}  // namespace qcode
