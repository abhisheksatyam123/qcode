#include "providers/openai/openai_response_parser.h"

#include <gtest/gtest.h>

namespace qcode {
namespace test {

// One reader for every dialect, so "the model reasoned" survives the trip
// from the wire to the Stats tab.
TEST(OpenAIUsageTest, ChatCompletionsShape) {
  const Usage usage = openai::parse_openai_usage({
      {"prompt_tokens", 120},
      {"completion_tokens", 30},
      {"total_tokens", 150},
      {"prompt_tokens_details", {{"cached_tokens", 100}}},
      {"completion_tokens_details", {{"reasoning_tokens", 7}}}});
  EXPECT_EQ(usage.prompt_tokens, 120);
  EXPECT_EQ(usage.completion_tokens, 30);
  EXPECT_EQ(usage.total_tokens, 150);
  EXPECT_EQ(usage.cached_prompt_tokens, 100);
  EXPECT_EQ(usage.reasoning_completion_tokens, 7);
}

TEST(OpenAIUsageTest, ResponsesShapeKeepsReasoningAndCacheTokens) {
  // The regression: /responses reported these under different names and the
  // reader dropped them, so a reasoning model looked like it never thought.
  const Usage usage = openai::parse_openai_usage({
      {"input_tokens", 16852},
      {"output_tokens", 189},
      {"total_tokens", 17041},
      {"input_tokens_details", {{"cached_tokens", 4096}}},
      {"output_tokens_details", {{"reasoning_tokens", 96}}}});
  EXPECT_EQ(usage.prompt_tokens, 16852);
  EXPECT_EQ(usage.completion_tokens, 189);
  EXPECT_EQ(usage.cached_prompt_tokens, 4096);
  EXPECT_EQ(usage.reasoning_completion_tokens, 96);
}

TEST(OpenAIUsageTest, FlatReasoningTokensStillCount) {
  const Usage usage = openai::parse_openai_usage(
      {{"prompt_tokens", 10}, {"completion_tokens", 4}, {"reasoning_tokens", 32}});
  EXPECT_EQ(usage.reasoning_completion_tokens, 32);
}

TEST(OpenAIUsageTest, GeminiThoughtsCount) {
  const Usage usage = openai::parse_openai_usage({
      {"promptTokenCount", 100},
      {"candidatesTokenCount", 20},
      {"thoughtsTokenCount", 55},
      {"totalTokenCount", 175},
      {"cachedContentTokenCount", 64}});
  EXPECT_EQ(usage.prompt_tokens, 100);
  EXPECT_EQ(usage.completion_tokens, 20);
  EXPECT_EQ(usage.reasoning_completion_tokens, 55);
  EXPECT_EQ(usage.cached_prompt_tokens, 64);
}

TEST(OpenAIUsageTest, AnthropicStyleCacheWriteAndThinking) {
  const Usage usage = openai::parse_openai_usage({
      {"input_tokens", 500},
      {"output_tokens", 80},
      {"cache_read_input_tokens", 400},
      {"cache_creation_input_tokens", 90},
      {"thinking_tokens", 64}});
  EXPECT_EQ(usage.prompt_tokens, 500);
  EXPECT_EQ(usage.completion_tokens, 80);
  EXPECT_EQ(usage.cached_prompt_tokens, 400);
  EXPECT_EQ(usage.cache_write_tokens, 90);
  EXPECT_EQ(usage.reasoning_completion_tokens, 64);
}

TEST(OpenAIUsageTest, EmptyAndNonObjectUsageAreSafe) {
  EXPECT_EQ(openai::parse_openai_usage(nlohmann::json::object()).total_tokens, 0);
  EXPECT_EQ(openai::parse_openai_usage(nlohmann::json()).prompt_tokens, 0);
}

TEST(OpenAIUsageTest, TotalFallsBackToPromptPlusCompletion) {
  const Usage usage = openai::parse_openai_usage(
      {{"input_tokens", 10}, {"output_tokens", 5}});
  EXPECT_EQ(usage.total_tokens, 15);
}

}  // namespace test
}  // namespace qcode
