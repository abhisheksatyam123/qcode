#include <qcode/core/generate_options.h>
#include "providers/cursor/cursor_proto.h"
#include "providers/cursor/cursor_request_builder.h"
#include "providers/cursor/cursor_response_parser.h"

#include <gtest/gtest.h>

#include <string>

namespace qcode {
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

TEST(CursorProviderTest, DoesNotReuseSessionIdAsConversationId) {
  GenerateOptions options;
  options.model = "composer-2.5";
  options.session_id = "11111111-2222-3333-4444-555555555555";
  options.messages = {Message::user("ping")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);
  // A reused session id poisons the next turn after an idle-end/abort.
  EXPECT_EQ(request.find(options.session_id), std::string::npos);
}

TEST(CursorProviderTest, KeepsPromptPrefixStableForAutomaticCache) {
  GenerateOptions options;
  options.model = "claude-opus-5";
  options.session_id = "cache-session-cursor";
  options.system = "Stable prefix for Cursor automatic cache.";
  options.reasoning_effort = "high";
  options.messages = {Message::user("first"), Message::assistant("ack"),
                      Message::user("second")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);
  EXPECT_EQ(request.find(options.session_id), std::string::npos);
  EXPECT_NE(request.find("claude-opus-5-thinking-high"), std::string::npos);
  EXPECT_NE(request.find("Stable prefix for Cursor automatic cache."),
            std::string::npos);
}

TEST(CursorProviderTest, MapsGrokVariantOntoWireModelSlug) {
  GenerateOptions options;
  options.model = "grok-4.6";
  options.system = "Be concise.";
  options.reasoning_effort = "high";
  options.messages = {Message::user("hi")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);

  EXPECT_NE(request.find("cursor-grok-4.6-high"), std::string::npos);
  EXPECT_EQ(request.find("Reasoning effort:"), std::string::npos);
  EXPECT_NE(request.find("Be concise."), std::string::npos);
}

TEST(CursorProviderTest, GrokDefaultEffortMapsOntoMediumSku) {
  GenerateOptions options;
  options.model = "cursor-grok-4.6-high";
  options.messages = {Message::user("hi")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);

  EXPECT_NE(request.find("cursor-grok-4.6-medium"), std::string::npos);
}

TEST(CursorProviderTest, MapsOpusLowVariantOntoThinkingSlug) {
  GenerateOptions options;
  options.model = "claude-opus-5";
  options.reasoning_effort = "low";
  options.messages = {Message::user("hi")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);
  EXPECT_NE(request.find("claude-opus-5-thinking-low"), std::string::npos);
}

TEST(CursorProviderTest, ClassifyAgentPayloadHandlesErrorsAndPreventsFalsePositives) {
  // Real JSON error object
  const std::string real_error = "{\"error\": \"Invalid API key\", \"code\": 401}";
  auto ev1 = CursorResponseParser::classify_agent_payload(real_error);
  EXPECT_EQ(ev1.kind, AgentStreamEvent::Kind::kError);
  EXPECT_EQ(ev1.error, real_error);

  // Real JSON message error object
  const std::string real_message_error = "{\"message\": \"Quota exceeded\"}";
  auto ev2 = CursorResponseParser::classify_agent_payload(real_message_error);
  EXPECT_EQ(ev2.kind, AgentStreamEvent::Kind::kError);
  EXPECT_EQ(ev2.error, real_message_error);

  // Binary payload containing the word "error" and '{' but not valid JSON
  const std::string false_positive = " o-\"-MpQ>7J&System:Tools are available when needed, including error checks { ... }";
  auto ev3 = CursorResponseParser::classify_agent_payload(false_positive);
  EXPECT_NE(ev3.kind, AgentStreamEvent::Kind::kError);
}

TEST(CursorProviderTest, ClassifiesThinkingDeltaAsReasoning) {
  const auto thinking_delta = proto::bytes_field(1, "ponder this");
  const auto update = proto::bytes_field(4, thinking_delta);
  const auto payload = proto::bytes_field(1, update);
  const auto ev = CursorResponseParser::classify_agent_payload(payload);
  EXPECT_EQ(ev.kind, AgentStreamEvent::Kind::kReasoningDelta);
  EXPECT_EQ(ev.text, "ponder this");
}

TEST(CursorProviderTest, ConnectHeartbeatIsNotTurnEnded) {
  // Connect keepalive: field 1 = "j\0" (0a 02 6a 00). Must stay heartbeat so
  // the client can keep the native agent tool loop open across keepalives.
  const std::string heartbeat("\x0a\x02\x6a\x00", 4);
  const auto ev = CursorResponseParser::classify_agent_payload(heartbeat);
  EXPECT_EQ(ev.kind, AgentStreamEvent::Kind::kHeartbeat);
}

TEST(CursorProviderTest, MultiTurnPromptIncludesPriorTurns) {
  GenerateOptions options;
  options.model = "composer-2.5";
  options.session_id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
  options.system = "Be helpful.";
  options.messages = {
      Message::user("First question"),
      Message::assistant("First answer"),
      Message::user("Follow up"),
  };

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);

  EXPECT_NE(request.find("First question"), std::string::npos);
  EXPECT_NE(request.find("First answer"), std::string::npos);
  EXPECT_NE(request.find("Follow up"), std::string::npos);
}

}  // namespace
}  // namespace cursor
}  // namespace qcode

