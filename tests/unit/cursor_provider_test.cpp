#include "ai/types/generate_options.h"
#include "providers/cursor/cursor_proto.h"
#include "providers/cursor/cursor_request_builder.h"
#include "providers/cursor/cursor_response_parser.h"

#include <gtest/gtest.h>

#include <string>

namespace ai {
namespace cursor {
namespace {

TEST(CursorProviderTest, ParsesLiveModelCatalogMetadata) {
  const auto sol = proto::bytes_field(1, "gpt-5.6-sol") +
                   proto::bytes_field(4, "GPT-5.6 Sol");
  const auto fable = proto::bytes_field(1, "claude-fable-5-thinking-high") +
                     proto::bytes_field(3, "Claude Fable 5 High");
  const auto response =
      proto::bytes_field(1, sol) + proto::bytes_field(1, fable);

  const auto models =
      CursorResponseParser::parse_get_usable_models(response);

  ASSERT_EQ(models.size(), 2u);
  EXPECT_EQ(models[0].id, "gpt-5.6-sol");
  EXPECT_EQ(models[0].name, "GPT-5.6 Sol");
  EXPECT_EQ(models[1].id, "claude-fable-5-thinking-high");
  EXPECT_EQ(models[1].name, "Claude Fable 5 High");
}

TEST(CursorProviderTest, FallsBackToModelIdWhenDisplayNameIsMissing) {
  const auto model = proto::bytes_field(1, "composer-2.5");
  const auto response = proto::bytes_field(1, model);

  const auto models =
      CursorResponseParser::parse_get_usable_models(response);

  ASSERT_EQ(models.size(), 1u);
  EXPECT_EQ(models.front().id, "composer-2.5");
  EXPECT_EQ(models.front().name, "composer-2.5");
}

TEST(CursorProviderTest, SendsStableConversationPrefixForAutomaticCaching) {
  GenerateOptions options;
  options.model = "gpt-5.6-sol";
  options.system = "Follow the project rules.";
  options.messages = {
      Message::user("Inspect the provider."),
      Message::assistant("I found the provider."),
      Message::user("Fix the model catalog."),
  };

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);

  EXPECT_NE(request.find("System:\nFollow the project rules."),
            std::string::npos);
  EXPECT_NE(request.find("user:\nInspect the provider."),
            std::string::npos);
  EXPECT_NE(request.find("assistant:\nI found the provider."),
            std::string::npos);
  EXPECT_NE(request.find("user:\nFix the model catalog."),
            std::string::npos);
}

}  // namespace
}  // namespace cursor
}  // namespace ai
