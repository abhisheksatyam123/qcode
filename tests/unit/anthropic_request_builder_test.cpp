#include "providers/anthropic/anthropic_request_builder.h"

#include <gtest/gtest.h>

namespace ai {
namespace anthropic {
namespace {

TEST(AnthropicRequestBuilderTest, EnablesPromptCachingOnSystemAndTopLevel) {
  AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-sonnet-4-6";
  options.system = "You are a careful coding agent.";
  options.messages = {Message::user("Hello")};
  options.budget_tokens = 8000;

  const auto request = builder.build_request_json(options);

  ASSERT_TRUE(request.contains("cache_control"));
  EXPECT_EQ(request["cache_control"]["type"], "ephemeral");

  ASSERT_TRUE(request["system"].is_array());
  ASSERT_FALSE(request["system"].empty());
  EXPECT_EQ(request["system"][0]["type"], "text");
  EXPECT_EQ(request["system"][0]["text"], "You are a careful coding agent.");
  EXPECT_EQ(request["system"][0]["cache_control"]["type"], "ephemeral");

  EXPECT_EQ(request["thinking"]["type"], "enabled");
  EXPECT_EQ(request["thinking"]["budget_tokens"], 8000);
}

}  // namespace
}  // namespace anthropic
}  // namespace ai
