#include <qcode/core/generate_options.h>
#include <qcode/core/message.h>
#include "providers/openai/openai_request_builder.h"
#include "providers/openai/openai_response_parser.h"

#include <gtest/gtest.h>

namespace qcode {
namespace openai {
namespace {

TEST(OpenAIResponsesTest, BuildsResponsesRequestWithTools) {
  OpenAIRequestBuilder builder(true);
  GenerateOptions options;
  options.model = "gpt-5.4";
  options.prompt = "hello";
  options.reasoning_effort = "high";
  options.tools.emplace(
      "lookup", Tool{"Lookup", {{"type", "object"}}});
  const auto request = builder.build_request_json(options);
  EXPECT_TRUE(request.contains("input"));
  EXPECT_FALSE(request.contains("messages"));
  EXPECT_EQ(request["reasoning"]["effort"], "high");
  EXPECT_EQ(request["tools"][0]["name"], "lookup");
}

TEST(OpenAIChatCompletionsTest, AddsCacheControlForClaudeModelIds) {
  OpenAIRequestBuilder builder(false);
  GenerateOptions options;
  options.model = "anthropic/claude-sonnet-4";
  options.messages = {Message::user("hi")};
  options.reasoning_effort = "medium";

  const auto request = builder.build_request_json(options);
  ASSERT_TRUE(request.contains("cache_control"));
  EXPECT_EQ(request["cache_control"]["type"], "ephemeral");
  EXPECT_EQ(request["reasoning_effort"], "medium");
}

TEST(OpenAIChatCompletionsTest, SkipsCacheControlForNonClaudeModels) {
  OpenAIRequestBuilder builder(false);
  GenerateOptions options;
  options.model = "gpt-4o";
  options.messages = {Message::user("hi")};

  const auto request = builder.build_request_json(options);
  EXPECT_FALSE(request.contains("cache_control"));
}

TEST(OpenAIResponsesTest, ParsesTextAndFunctionCalls) {
  OpenAIResponseParser parser;
  const nlohmann::json response{
      {"id", "response-1"},
      {"model", "gpt-5.4"},
      {"status", "completed"},
      {"output",
       {{{"type", "message"},
         {"content", {{{"type", "output_text"}, {"text", "hello"}}}}},
        {{"type", "function_call"},
         {"call_id", "call-1"},
         {"name", "lookup"},
         {"arguments", R"({"q":"x"})"}}}},
      {"usage",
       {{"input_tokens", 2}, {"output_tokens", 3}, {"total_tokens", 5}}}};
  const auto result = parser.parse_success_completion_response(response);
  EXPECT_TRUE(result.is_success());
  EXPECT_EQ(result.text, "hello");
  ASSERT_EQ(result.tool_calls.size(), 1u);
  EXPECT_EQ(result.tool_calls[0].tool_name, "lookup");
  EXPECT_EQ(result.usage.total_tokens, 5);
}

// ── Interleaved-reasoning (ox-alpha / deepseek-v4-flash) parity ──

// Zen free models return thinking in message.reasoning_content; the parser
// must surface it even when tool calls ride along, so the tool loop can
// replay it on the next step.
TEST(OpenAIChatCompletionsTest, ParsesReasoningContentWithToolCalls) {
  OpenAIResponseParser parser;
  const nlohmann::json response{
      {"choices",
       {{{"index", 0},
         {"finish_reason", "tool_calls"},
         {"message",
          {{"role", "assistant"},
           {"content", ""},
           {"reasoning_content", "need to read the file first"},
           {"tool_calls",
            {{{"id", "call-1"},
              {"type", "function"},
              {"function",
               {{"name", "read_file"}, {"arguments", R"({"path":"a"})"}}}}}}}}}}},
      {"usage",
       {{"prompt_tokens", 100},
        {"completion_tokens", 40},
        {"total_tokens", 140},
        {"prompt_tokens_details", {{"cached_tokens", 64}}},
        {"completion_tokens_details", {{"reasoning_tokens", 30}}}}}};

  const auto result = parser.parse_success_completion_response(response);
  ASSERT_TRUE(result.is_success());
  EXPECT_TRUE(result.text.empty());
  ASSERT_EQ(result.tool_calls.size(), 1u);

  // Reasoning part rides alongside the tool-call parts.
  const auto& msg = result.response_messages.at(0);
  EXPECT_EQ(msg.role, kMessageRoleAssistant);
  bool saw_reasoning = false;
  for (const auto& part : msg.content) {
    if (const auto* rp = std::get_if<ReasoningContentPart>(&part)) {
      saw_reasoning = rp->text == "need to read the file first";
    }
  }
  EXPECT_TRUE(saw_reasoning);

  // Thinking-token accounting.
  EXPECT_EQ(result.usage.reasoning_completion_tokens, 30);
  EXPECT_EQ(result.usage.cached_prompt_tokens, 64);
}

// The request builder replays reasoning via reasoning_content and sends an
// explicit null content when the assistant turn had no text.
TEST(OpenAIChatCompletionsTest, ReplaysReasoningContentAndNullContent) {
  OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");

  GenerateOptions options;
  options.model = "x-preview-f-free";
  Message assistant =
      Message::assistant_with_tools("", {ToolCallContentPart(
                                            "call_1", "read_file",
                                            nlohmann::json{{"path", "a"}})});
  assistant.content.emplace_back(ReasoningContentPart{"prior thoughts", ""});
  options.messages = {Message::user("hi"), std::move(assistant)};

  const auto body = builder.build_request_json(options);
  const auto& replayed = body["messages"].at(1);
  EXPECT_EQ(replayed.value("role", ""), "assistant");
  EXPECT_TRUE(replayed.contains("content"));
  EXPECT_TRUE(replayed["content"].is_null());
  EXPECT_EQ(replayed.value("reasoning_content", ""), "prior thoughts");
}

}  // namespace
}  // namespace openai
}  // namespace qcode
