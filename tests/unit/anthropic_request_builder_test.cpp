#include "providers/anthropic/anthropic_request_builder.h"
#include "providers/anthropic/anthropic_response_parser.h"
#include "providers/anthropic/anthropic_thinking.h"

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

  // Claude >=4.6 ignores budget_tokens: adaptive + display is the wire form.
  EXPECT_EQ(request["thinking"]["type"], "adaptive");
  EXPECT_EQ(request["thinking"]["display"], "summarized");
}

TEST(AnthropicRequestBuilderTest, MapsReasoningEffortToThinkingBudget) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-sonnet-4-20250514";
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

// Anthropic input_tokens excludes cache reads/writes; qcode reports the full
// prompt with cached as a subset (regression: session showed prompt=10 for a
// 160-message Claude Code Max session served almost entirely from cache).
TEST(AnthropicUsageTest, NormalizesCachedInputIntoPromptTokens) {
  const nlohmann::json usage = {{"input_tokens", 3},
                                {"cache_read_input_tokens", 40000},
                                {"cache_creation_input_tokens", 1200},
                                {"output_tokens", 250}};
  const auto in = normalize_input_usage(usage);
  EXPECT_TRUE(in.present);
  EXPECT_EQ(in.prompt_tokens, 41203);
  EXPECT_EQ(in.cached_prompt_tokens, 40000);

  const nlohmann::json response = {
      {"content", {{{"type", "text"}, {"text", "ok"}}}},
      {"stop_reason", "end_turn"},
      {"usage", usage}};
  AnthropicResponseParser parser;
  const auto res = parser.parse_success_completion_response(response);
  EXPECT_EQ(res.usage.prompt_tokens, 41203);
  EXPECT_EQ(res.usage.cached_prompt_tokens, 40000);
  EXPECT_EQ(res.usage.completion_tokens, 250);
  EXPECT_EQ(res.usage.total_tokens, 41453);
}

TEST(AnthropicUsageTest, OutputOnlyDeltaIsNotInputUsage) {
  const auto in = normalize_input_usage(nlohmann::json{{"output_tokens", 9}});
  EXPECT_FALSE(in.present);
  EXPECT_EQ(in.prompt_tokens, 0);
  EXPECT_EQ(in.cached_prompt_tokens, 0);
}


// ── Adaptive thinking (Claude >=4.6) ───────────────────────────────────────
// Probed live against api.anthropic.com (claude-opus-5-5):
//   thinking{type:disabled}   -> HTTP 400 "use adaptive and output_config.effort"
//   thinking{type:enabled}    -> 200 but EMPTY thinking text, ~60 thinking tokens
//   adaptive (display unset)  -> 200 but EMPTY thinking text
//   adaptive+summarized+effort-> thinking text present, tokens scale with effort
TEST(AnthropicThinkingTest, DetectsAdaptiveModels) {
  EXPECT_TRUE(anthropic_uses_adaptive_thinking("claude-opus-5-5"));
  EXPECT_TRUE(anthropic_uses_adaptive_thinking("claude-sonnet-5-5"));
  EXPECT_TRUE(anthropic_uses_adaptive_thinking("claude-haiku-5-5"));
  EXPECT_TRUE(anthropic_uses_adaptive_thinking("claude-opus-4-7"));
  EXPECT_TRUE(anthropic_uses_adaptive_thinking("claude-opus-4-6"));
  EXPECT_TRUE(anthropic_uses_adaptive_thinking("claude-sonnet-4-6"));
  // "-thinking" variants resolve to the same base model.
  EXPECT_TRUE(anthropic_uses_adaptive_thinking("claude-opus-5-5-thinking"));

  EXPECT_FALSE(anthropic_uses_adaptive_thinking("claude-opus-4-5"));
  EXPECT_FALSE(
      anthropic_uses_adaptive_thinking("claude-sonnet-4-20250514"));
  EXPECT_FALSE(
      anthropic_uses_adaptive_thinking("claude-3-7-sonnet-20250219"));
  EXPECT_FALSE(
      anthropic_uses_adaptive_thinking("claude-opus-4-5-20251101"));
  EXPECT_FALSE(anthropic_uses_adaptive_thinking("glm-4.6"));
  // Catalog-prefixed ids (third-party Anthropic-compatible endpoints).
  EXPECT_TRUE(anthropic_uses_adaptive_thinking("zen/claude-opus-5-5"));
}

TEST(AnthropicRequestBuilderTest, AdaptiveModelSendsEffortWithoutBudgetTokens) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-opus-5-5";
  options.messages = {Message::user("Hello")};
  options.reasoning_effort = "high";

  const auto request = builder.build_request_json(options);

  ASSERT_TRUE(request.contains("thinking"));
  EXPECT_EQ(request["thinking"]["type"], "adaptive");
  EXPECT_EQ(request["thinking"]["display"], "summarized");
  EXPECT_FALSE(request["thinking"].contains("budget_tokens"));
  ASSERT_TRUE(request.contains("output_config"));
  EXPECT_EQ(request["output_config"]["effort"], "high");
  // High-effort thinking needs room for tens of thousands of think tokens.
  EXPECT_GE(request["max_tokens"].get<int>(), 32000);
}

TEST(AnthropicRequestBuilderTest, AdaptiveModelIgnoresBudgetTokens) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-sonnet-5-5";
  options.messages = {Message::user("Hello")};
  // generation_service still fills budget_tokens for Anthropic providers; the
  // adaptive form must win so effort actually drives thinking.
  options.budget_tokens = 16000;
  options.reasoning_effort = "medium";

  const auto request = builder.build_request_json(options);
  EXPECT_EQ(request["thinking"]["type"], "adaptive");
  EXPECT_EQ(request["thinking"]["display"], "summarized");
  EXPECT_FALSE(request["thinking"].contains("budget_tokens"));
  EXPECT_EQ(request["output_config"]["effort"], "medium");
}

TEST(AnthropicRequestBuilderTest, AdaptiveModelWithoutEffortSendsNoOutputConfig) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-opus-5-5";
  options.messages = {Message::user("Hello")};
  options.reasoning_effort = "off";

  const auto request = builder.build_request_json(options);
  // Never type:disabled (HTTP 400 on >=4.6); display is what makes the text
  // visible, so it rides even without an effort.
  EXPECT_EQ(request["thinking"]["type"], "adaptive");
  EXPECT_EQ(request["thinking"]["display"], "summarized");
  EXPECT_FALSE(request.contains("output_config"));
  EXPECT_EQ(request["max_tokens"].get<int>(), 4096);
}

TEST(AnthropicRequestBuilderTest, EffortXhighClampsToMaxOnClaude46) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-opus-4-6";
  options.messages = {Message::user("Hello")};
  options.reasoning_effort = "xhigh";

  const auto request = builder.build_request_json(options);
  EXPECT_EQ(request["thinking"]["type"], "adaptive");
  // 4.6 predates xhigh; max is its top effort.
  EXPECT_EQ(request["output_config"]["effort"], "max");
  EXPECT_GE(request["max_tokens"].get<int>(), 32000);

  // 4.7+ does take xhigh verbatim.
  options.model = "claude-opus-4-7";
  const auto request2 = builder.build_request_json(options);
  EXPECT_EQ(request2["output_config"]["effort"], "xhigh");
}

TEST(AnthropicRequestBuilderTest, LegacyModelKeepsBudgetTokens) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-opus-4-5";
  options.messages = {Message::user("Hello")};
  options.reasoning_effort = "high";

  const auto request = builder.build_request_json(options);
  EXPECT_EQ(request["thinking"]["type"], "enabled");
  EXPECT_EQ(request["thinking"]["budget_tokens"], 16000);
  EXPECT_FALSE(request.contains("output_config"));
  EXPECT_EQ(request["max_tokens"].get<int>(), 17024);

  // Explicit budget wins over the effort-derived one on legacy models.
  options.model = "claude-3-7-sonnet-20250219";
  options.budget_tokens = 4096;
  const auto request2 = builder.build_request_json(options);
  EXPECT_EQ(request2["thinking"]["type"], "enabled");
  EXPECT_EQ(request2["thinking"]["budget_tokens"], 4096);
}

TEST(AnthropicRequestBuilderTest, HistoryEchoesSignedThinkingForAdaptiveModel) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-opus-5-5";
  options.messages = {
      Message::user("Hi"),
      Message(kMessageRoleAssistant,
              {ReasoningContentPart{"kept reasoning", "sig-keep"},
               ReasoningContentPart{"unsigned reasoning", ""},
               ReasoningContentPart{"", "sig-empty-text"},
               TextContentPart{"answer"}}),
      Message::user("again")};
  options.reasoning_effort = "high";

  const auto request = builder.build_request_json(options);
  const auto& content = request["messages"][1]["content"];
  ASSERT_TRUE(content.is_array());

  int thinking_blocks = 0;
  bool found_signed = false;
  bool found_empty_text = false;
  for (const auto& block : content) {
    if (block.value("type", "") != "thinking") continue;
    ++thinking_blocks;
    if (block.value("signature", "") == "sig-keep") {
      found_signed = true;
      EXPECT_EQ(block.value("thinking", ""), "kept reasoning");
    }
    if (block.value("signature", "") == "sig-empty-text") {
      // Signed but empty text is legal and must ride along.
      found_empty_text = true;
      EXPECT_EQ(block.value("thinking", ""), "");
    }
    EXPECT_NE(block.value("signature", ""), "");
  }
  EXPECT_EQ(thinking_blocks, 2);
  EXPECT_TRUE(found_signed);
  EXPECT_TRUE(found_empty_text);
}

}  // namespace
}  // namespace anthropic
}  // namespace qcode
