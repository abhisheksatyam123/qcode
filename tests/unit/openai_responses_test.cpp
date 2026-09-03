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
  ASSERT_EQ(request["input"].size(), 1u);
  EXPECT_EQ(request["input"][0]["role"], "user");
  ASSERT_TRUE(request["input"][0]["content"].is_array());
  EXPECT_EQ(request["input"][0]["content"][0]["type"], "input_text");
  EXPECT_EQ(request["input"][0]["content"][0]["text"], "hello");
}

TEST(OpenAIResponsesTest, LowersToolTurnsWithoutNullAssistantContent) {
  OpenAIRequestBuilder builder(true);
  GenerateOptions options;
  options.model = "muse-spark-1.3-contributor-free";
  options.system = "be brief";
  options.messages = {
      Message::user("What is the weather?"),
      Message::assistant_with_tools(
          "", {ToolCallContentPart{
                  "call_1", "lookup", nlohmann::json{{"query", "weather"}}}}),
      Message::tool_results(
          {{"call_1", nlohmann::json{{"forecast", "sunny"}}, false}}),
      Message::assistant("Paris is sunny."),
      Message::user("thanks"),
  };

  const auto request = builder.build_request_json(options);
  ASSERT_TRUE(request.contains("input"));
  const auto& input = request["input"];
  ASSERT_EQ(input.size(), 6u);

  EXPECT_EQ(input[0]["role"], "system");
  EXPECT_EQ(input[0]["content"], "be brief");

  EXPECT_EQ(input[1]["role"], "user");
  ASSERT_TRUE(input[1]["content"].is_array());
  EXPECT_EQ(input[1]["content"][0]["type"], "input_text");
  EXPECT_EQ(input[1]["content"][0]["text"], "What is the weather?");

  EXPECT_EQ(input[2]["type"], "function_call");
  EXPECT_EQ(input[2]["call_id"], "call_1");
  EXPECT_EQ(input[2]["name"], "lookup");
  EXPECT_FALSE(input[2].contains("content"));

  EXPECT_EQ(input[3]["type"], "function_call_output");
  EXPECT_EQ(input[3]["call_id"], "call_1");
  EXPECT_TRUE(input[3]["output"].is_string());

  EXPECT_EQ(input[4]["role"], "assistant");
  ASSERT_TRUE(input[4]["content"].is_array());
  EXPECT_EQ(input[4]["content"][0]["type"], "output_text");
  EXPECT_EQ(input[4]["content"][0]["text"], "Paris is sunny.");

  EXPECT_EQ(input[5]["role"], "user");
  EXPECT_EQ(input[5]["content"][0]["type"], "input_text");
}

TEST(OpenAIChatCompletionsTest, ExtractsMuseSparkObjectReasoning) {
  EXPECT_EQ(extract_openai_reasoning_text(nlohmann::json{
                {"reasoning", {{"content", "step one"}}}}),
            "step one");
  EXPECT_EQ(extract_openai_reasoning_text(nlohmann::json{
                {"reasoning_details",
                 {{{"type", "reasoning.text"}, {"text", "think "}},
                  {{"type", "reasoning.summary"}, {"summary", "done"}}}}}),
            "think done");
  EXPECT_EQ(extract_openai_reasoning_text(nlohmann::json{
                {"reasoning", nlohmann::json::array({{"step a"}, {"step b"}})}}),
            "step astep b");
  EXPECT_EQ(extract_openai_reasoning_text(nlohmann::json{
                {"content",
                 {{{"type", "thought"}, {"text", "ponder"}},
                  {{"type", "text"}, {"text", "hello"}}}}}),
            "ponder");
}

TEST(OpenAIChatCompletionsTest, ParsesMuseSparkReasoningAndUsage) {
  OpenAIResponseParser parser;
  const nlohmann::json response{
      {"choices",
       {{{"message",
          {{"role", "assistant"},
           {"content", "ok"},
           {"reasoning", {{"content", "consider the repo"}}}}},
         {"finish_reason", "stop"}}}},
      {"usage",
       {{"prompt_tokens", 10},
        {"completion_tokens", 4},
        {"total_tokens", 14},
        {"reasoning_tokens", 32}}}};
  const auto result = parser.parse_success_completion_response(response);
  EXPECT_TRUE(result.is_success());
  EXPECT_EQ(result.reasoning, "consider the repo");
  EXPECT_EQ(result.usage.reasoning_completion_tokens, 32);
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

TEST(OpenAIChatCompletionsTest, OpenRouterClaudeMarksMessageCacheBreakpoints) {
  OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://openrouter.ai/api/v1");
  GenerateOptions options;
  options.model = "anthropic/claude-sonnet-4";
  options.system = "sys-a";
  options.messages = {
      Message::user("u1"),
      Message::assistant("a1"),
      Message::user("u2"),
  };

  const auto request = builder.build_request_json(options);
  ASSERT_TRUE(request.contains("messages"));
  const auto& messages = request["messages"];
  ASSERT_GE(messages.size(), 4u);
  ASSERT_TRUE(messages[0]["content"].is_array());
  EXPECT_EQ(messages[0]["content"][0]["cache_control"]["type"], "ephemeral");
  EXPECT_TRUE(messages[1]["content"].is_string());
  ASSERT_TRUE(messages[2]["content"].is_array());
  EXPECT_EQ(messages[2]["content"][0]["cache_control"]["type"], "ephemeral");
  ASSERT_TRUE(messages[3]["content"].is_array());
  EXPECT_EQ(messages[3]["content"][0]["cache_control"]["type"], "ephemeral");
  EXPECT_TRUE(request.contains("usage"));
  EXPECT_TRUE(request["usage"].value("include", false));
  EXPECT_FALSE(request.contains("prompt_cache_key"));
  EXPECT_FALSE(request.contains("promptCacheKey"));
}

TEST(OpenAIChatCompletionsTest, ZenNonClaudeSkipsMessageCacheBreakpoints) {
  OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");
  GenerateOptions options;
  options.model = "x-preview-f-free";
  options.messages = {Message::user("hi")};

  const auto request = builder.build_request_json(options);
  ASSERT_TRUE(request["messages"][0]["content"].is_string());
  EXPECT_FALSE(request.contains("prompt_cache_key"));
}

TEST(OpenAIChatCompletionsTest, ZenGpt5SetsPromptCacheKeyFromSession) {
  OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");
  GenerateOptions options;
  options.model = "gpt-5";
  options.session_id = "sess-cache-1";
  options.messages = {Message::user("hi")};

  const auto request = builder.build_request_json(options);
  EXPECT_EQ(request.value("promptCacheKey", ""), "sess-cache-1");
  EXPECT_EQ(request.value("prompt_cache_key", ""), "sess-cache-1");
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

TEST(OpenAIChatCompletionsTest, TreatsNativeNetworkErrorAsRetryableDrop) {
  OpenAIResponseParser parser;
  const nlohmann::json response = {
      {"id", "gen-drop"},
      {"model", "stealth/ox-alpha"},
      {"choices",
       {{{"index", 0},
         {"finish_reason", "stop"},
         {"native_finish_reason", "network_error"},
         {"message",
          {{"role", "assistant"},
           {"content", nullptr},
           {"reasoning", nullptr}}}}}}};

  const auto result = parser.parse_success_completion_response(response);
  EXPECT_FALSE(result.is_success());
  EXPECT_EQ(result.finish_reason, kFinishReasonError);
  ASSERT_TRUE(result.is_retryable.has_value());
  EXPECT_TRUE(*result.is_retryable);
  EXPECT_NE(result.error_message().find("network error"), std::string::npos);
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
