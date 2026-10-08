#include "providers/anthropic/anthropic_request_builder.h"
#include "providers/anthropic/anthropic_response_parser.h"

#include <gtest/gtest.h>

namespace qcode {
namespace anthropic {
namespace {

TEST(AnthropicRequestBuilderTest, EnablesPromptCachingOnSystemBlocksOnly) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-sonnet-5-5";
  options.system = "You are a careful coding agent.";
  options.messages = {Message::user("Hello")};
  options.budget_tokens = 8000;

  const auto request = builder.build_request_json(options);

  // Top-level cache_control is not a valid Anthropic API field; breakpoints
  // live on system/messages/tools blocks only.
  EXPECT_FALSE(request.contains("cache_control"));

  ASSERT_TRUE(request["system"].is_array());
  ASSERT_FALSE(request["system"].empty());
  EXPECT_EQ(request["system"][0]["type"], "text");
  EXPECT_EQ(request["system"][0]["text"], "You are a careful coding agent.");
  EXPECT_EQ(request["system"][0]["cache_control"]["type"], "ephemeral");

  EXPECT_EQ(request["thinking"]["type"], "enabled");
  EXPECT_EQ(request["thinking"]["budget_tokens"], 8000);
}

TEST(AnthropicRequestBuilderTest, MapsReasoningEffortToThinkingBudget) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-sonnet-5-5";
  options.messages = {Message::user("Hello")};
  options.reasoning_effort = "low";

  const auto request = builder.build_request_json(options);
  EXPECT_EQ(request["thinking"]["type"], "enabled");
  EXPECT_EQ(request["thinking"]["budget_tokens"], 2000);
}

// A step with thinking + text + tool_use is appended to history as the
// parser's response message. It must carry the tool_use, or the next request
// drops the step's tool_result as an orphan and the model never sees it.
TEST(AnthropicRequestBuilderTest, ToolResultsSurviveStepWithThinkingAndText) {
  const nlohmann::json response = {
      {"content",
       {{{"type", "thinking"}, {"thinking", "Need the file."}, {"signature", "sig-1"}},
        {{"type", "text"}, {"text", "Let me read it."}},
        {{"type", "tool_use"},
         {"id", "toolu_1"},
         {"name", "read"},
         {"input", {{"path", "a.txt"}}}}}},
      {"stop_reason", "tool_use"},
      {"usage", {{"input_tokens", 10}, {"output_tokens", 5}}}};
  AnthropicResponseParser parser;
  auto step = parser.parse_success_completion_response(response);
  ASSERT_EQ(step.tool_calls.size(), 1u);
  ASSERT_EQ(step.response_messages.size(), 1u);
  EXPECT_TRUE(step.response_messages.front().has_tool_calls());

  GenerateOptions options;
  // Thinking-suffixed model so the builder enables the thinking budget, as in
  // production thinking turns (wire id is stripped to claude-opus-5-5).
  options.model = "claude-opus-5-5-thinking";
  options.messages = {Message::user("Read a.txt"),
                      step.response_messages.front(),
                      Message(kMessageRoleUser,
                              {ToolResultContentPart{"toolu_1", "hello", false}})};
  AnthropicRequestBuilder builder;
  const auto request = builder.build_request_json(options);

  bool found_result = false;
  for (const auto& msg : request["messages"]) {
    if (!msg["content"].is_array()) continue;
    for (const auto& block : msg["content"]) {
      if (block.value("type", "") == "tool_result" &&
          block.value("tool_use_id", "") == "toolu_1") {
        found_result = true;
      }
    }
  }
  EXPECT_TRUE(found_result);
  // Thinking leads the assistant turn, as Anthropic requires with tool use.
  const auto& assistant = request["messages"][1];
  ASSERT_TRUE(assistant["content"].is_array());
  EXPECT_EQ(assistant["content"][0]["type"], "thinking");
  EXPECT_EQ(assistant["content"].back()["type"], "tool_use");
}


TEST(AnthropicRequestBuilderTest, MarksLastTwoUserTurnsSkipsAssistantTail) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-sonnet-5-5";
  options.system = "sys";
  options.messages = {Message::user("first"),
                      Message::assistant("ack"),
                      Message::user("second")};

  const auto request = builder.build_request_json(options);
  ASSERT_EQ(request["messages"].size(), 3u);
  // Both user turns carry a breakpoint ...
  EXPECT_TRUE(request["messages"][0].dump().find("cache_control") != std::string::npos);
  EXPECT_TRUE(request["messages"][2].dump().find("cache_control") != std::string::npos);
  // ... the assistant tail in between does not.
  EXPECT_TRUE(request["messages"][1].dump().find("cache_control") == std::string::npos);
}

}  // namespace
}  // namespace anthropic
}  // namespace qcode
