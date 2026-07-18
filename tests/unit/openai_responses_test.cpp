#include <qcode/types/generate_options.h>
#include <qcode/types/message.h>
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

}  // namespace
}  // namespace openai
}  // namespace qcode
