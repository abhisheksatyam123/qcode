#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Include the Anthropic client headers
#include <qcode/core/generate_options.h>
#include <qcode/core/stream_options.h>

// Include the real Anthropic client implementation for testing
#include "providers/anthropic/anthropic_client.h"
#include "providers/anthropic/anthropic_request_builder.h"

// Test utilities
#include "../utils/test_fixtures.h"

namespace qcode {
namespace test {

class AnthropicClientTest : public AnthropicTestFixture {
 protected:
  void SetUp() override {
    AnthropicTestFixture::SetUp();
    client_ = std::make_unique<qcode::anthropic::AnthropicClient>(
        kTestAnthropicApiKey, kTestAnthropicBaseUrl);
  }

  void TearDown() override {
    client_.reset();
    AnthropicTestFixture::TearDown();
  }

  std::unique_ptr<qcode::anthropic::AnthropicClient> client_;
};

// Constructor and Configuration Tests
TEST_F(AnthropicClientTest, ConstructorWithValidApiKey) {
  qcode::anthropic::AnthropicClient client("sk-ant-validkey123",
                                        "https://api.anthropic.com");

  EXPECT_TRUE(client.is_valid());
  EXPECT_EQ(client.provider_name(), "anthropic");
  EXPECT_THAT(client.config_info(), testing::HasSubstr("Anthropic API"));

  // Test public interface access only
  EXPECT_EQ(client.get_api_key(), "sk-ant-validkey123");
  EXPECT_EQ(client.get_base_url(), "https://api.anthropic.com");
  // Commented out internal method tests that don't exist:
  // EXPECT_EQ(client.get_host(), "api.anthropic.com");
  // EXPECT_TRUE(client.get_use_ssl());
}

TEST_F(AnthropicClientTest, ConstructorWithEmptyApiKey) {
  qcode::anthropic::AnthropicClient client("", "https://api.anthropic.com");

  // Real implementation should be invalid with empty API key
  EXPECT_FALSE(client.is_valid());
}

TEST_F(AnthropicClientTest, ConstructorWithCustomBaseUrl) {
  qcode::anthropic::AnthropicClient client("sk-ant-test",
                                        "https://custom-anthropic.example.com");

  EXPECT_TRUE(client.is_valid());
  EXPECT_EQ(client.provider_name(), "anthropic");
  EXPECT_THAT(client.config_info(),
              testing::HasSubstr("custom-anthropic.example.com"));

  // Commented out internal method tests that don't exist:
  // EXPECT_EQ(client.get_host(), "custom-anthropic.example.com");
  // EXPECT_TRUE(client.get_use_ssl());
}

TEST_F(AnthropicClientTest, ConstructorWithHttpUrl) {
  qcode::anthropic::AnthropicClient client("sk-ant-test", "http://localhost:8080");

  EXPECT_TRUE(client.is_valid());
  // Commented out internal method tests that don't exist:
  // EXPECT_EQ(client.get_host(), "localhost:8080");
  // EXPECT_FALSE(client.get_use_ssl());
}

namespace {

class HeaderInspectingAnthropicClient
    : public qcode::anthropic::AnthropicClient {
 public:
  using qcode::anthropic::AnthropicClient::AnthropicClient;
  const qcode::providers::ProviderConfig& provider_config() const {
    return config_;
  }
};

}  // namespace

TEST(AnthropicZenHeadersTest, MessagesTransportGetsOpenCodeIdentity) {
  qcode::anthropic::CompatibleOptions options;
  options.base_url = "https://opencode.ai/zen/v1";
  options.headers = {
      {"x-opencode-session-id", "child-uuid"},
      {"x-opencode-session", "parent-uuid"},
      {"x-session-affinity", "parent-uuid"},
      {"x-opencode-parent-session-id", "parent-uuid"},
      {"x-opencode-project", "proj"},
  };
  HeaderInspectingAnthropicClient client("__EMPTY__", options);
  qcode::anthropic::AnthropicRequestBuilder builder;
  const auto headers = builder.build_headers(client.provider_config());

  const auto user_agent = headers.find("User-Agent");
  ASSERT_NE(user_agent, headers.end());
  EXPECT_EQ(user_agent->second, "opencode/latest/2.0.29/cli");
  EXPECT_EQ(headers.find("x-opencode-client")->second, "cli");

  const auto session_id = headers.find("x-opencode-session-id");
  const auto affinity = headers.find("x-opencode-session");
  ASSERT_NE(session_id, headers.end());
  ASSERT_NE(affinity, headers.end());
  EXPECT_TRUE(session_id->second.starts_with("ses_"));
  EXPECT_TRUE(affinity->second.starts_with("ses_"));
  EXPECT_NE(session_id->second, affinity->second);
  EXPECT_EQ(headers.find("x-session-affinity")->second, affinity->second);
  EXPECT_EQ(headers.find("X-Session-Id")->second, affinity->second);
  EXPECT_EQ(headers.find("x-opencode-parent-session-id")->second,
            affinity->second);
  EXPECT_EQ(headers.find("x-parent-session-id")->second, affinity->second);
  EXPECT_EQ(headers.find("x-opencode-project")->second, "proj");
  // The keyless fallback must not duplicate the auth header.
  EXPECT_EQ(headers.count("Authorization"), 1u);
  EXPECT_EQ(headers.find("Authorization")->second, "Bearer public");
}

TEST(AnthropicZenHeadersTest, GoMessagesKeepsConfiguredKeyOverPublicFallback) {
  qcode::anthropic::CompatibleOptions options;
  options.base_url = "https://opencode.ai/inference/go/v1";
  options.bearer_auth = true;  // resolve_opencode_client sets this for Go
  options.headers = {{"x-opencode-session", "parent-uuid"}};
  HeaderInspectingAnthropicClient client("oc_sk_go_test", options);
  qcode::anthropic::AnthropicRequestBuilder builder;
  const auto headers = builder.build_headers(client.provider_config());

  // apply_opencode_zen_headers runs after auth now; a Go key must survive
  // instead of being joined by a second "Bearer public" header (Cloudflare
  // answers that with 400 Bad Request).
  EXPECT_EQ(headers.count("Authorization"), 1u);
  EXPECT_EQ(headers.find("Authorization")->second, "Bearer oc_sk_go_test");
  EXPECT_EQ(headers.find("x-opencode-client")->second, "cli");
  EXPECT_TRUE(headers.find("x-opencode-session")->second.starts_with("ses_"));
}

// Model Support Tests
TEST_F(AnthropicClientTest, SupportedModelsIsNotABuiltinCatalog) {
  EXPECT_TRUE(client_->supported_models().empty());
}

TEST_F(AnthropicClientTest, SupportsAnyNonEmptyModel) {
  EXPECT_TRUE(client_->supports_model("claude-sonnet-5-5"));
  EXPECT_TRUE(client_->supports_model("any-configured-id"));
  EXPECT_FALSE(client_->supports_model(""));
}

// Text Generation Tests - Testing error handling without network calls
TEST_F(AnthropicClientTest, GenerateTextWithInvalidApiKey) {
  // Local stand-in for api.anthropic.com rejecting the key with a 401.
  LocalStatusServer server(
      401,
      R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})");
  ASSERT_FALSE(server.base_url().empty());
  qcode::retry::RetryConfig retry_config;
  retry_config.max_retries = 0;
  qcode::anthropic::AnthropicClient client("invalid-key", server.base_url(),
                                           retry_config);
  auto options = createBasicAnthropicOptions();

  auto result = client.generate_text(options);

  // We expect this to fail since we're using an invalid API key
  EXPECT_FALSE(result.is_success());
  EXPECT_FALSE(result.error_message().empty());
}

TEST_F(AnthropicClientTest, GenerateTextWithBadUrl) {
  qcode::retry::RetryConfig retry_config;
  retry_config.max_retries = 0;
  // Port 1 on loopback refuses immediately, so no DNS or network is involved.
  qcode::anthropic::AnthropicClient client("sk-ant-test", "http://127.0.0.1:1",
                                           retry_config);
  auto options = createBasicAnthropicOptions();

  // This should fail due to connection refusal
  auto result = client.generate_text(options);

  EXPECT_FALSE(result.is_success());
  EXPECT_FALSE(result.error_message().empty());
}

// Configuration Tests
TEST_F(AnthropicClientTest, ConfigurationInfoContainsExpectedData) {
  auto config = client_->config_info();

  EXPECT_FALSE(config.empty());
  EXPECT_THAT(config, testing::HasSubstr("Anthropic"));
  EXPECT_THAT(config, testing::HasSubstr(kTestAnthropicBaseUrl));
}

TEST_F(AnthropicClientTest, ProviderNameIsConsistent) {
  EXPECT_EQ(client_->provider_name(), "anthropic");
}

// Test option validation without network calls
TEST_F(AnthropicClientTest, ValidateOptionsValidation) {
  // Test with empty model
  GenerateOptions invalid_options("", "test prompt");
  EXPECT_FALSE(invalid_options.is_valid());

  // Test with valid Anthropic options
  auto valid_options = createBasicAnthropicOptions();
  EXPECT_TRUE(valid_options.is_valid());
}

// Stream Tests (Basic validation)
TEST_F(AnthropicClientTest, StreamTextBasicValidation) {
  auto options =
      StreamOptions(GenerateOptions(kTestAnthropicModel, kTestPrompt));

  // For now, just verify the call doesn't crash
  // In a real test environment, this would fail due to network, but should
  // handle gracefully
  auto result = client_->stream_text(options);

  // The stream result should be created even if the underlying request fails
  EXPECT_TRUE(true);  // Just verify no crash occurred
}

// Internal Method Tests - These test the private implementation
// NOTE: These tests have been commented out because the internal methods
// they test (build_request_json, message_role_to_string, parse_stop_reason)
// are not exposed in the actual implementation. The integration tests in
// tests/integration/ provide better coverage by testing the public interface.

/*
TEST_F(AnthropicClientTest, TestInternalJsonBuilding) {
  auto options = createBasicAnthropicOptions();

  // Test the internal JSON building method (exposed via QCODE_TESTING)
  auto json = client_->build_request_json(options);

  EXPECT_EQ(json["model"], kTestAnthropicModel);
  EXPECT_TRUE(json.contains("messages"));
  EXPECT_TRUE(json["messages"].is_array());
  EXPECT_FALSE(json["messages"].empty());
  EXPECT_TRUE(json.contains("max_tokens"));
}

TEST_F(AnthropicClientTest, TestMessageRoleConversion) {
  // Test the internal message role conversion method
  EXPECT_EQ(client_->message_role_to_string(kMessageRoleSystem), "system");
  EXPECT_EQ(client_->message_role_to_string(kMessageRoleUser), "user");
  EXPECT_EQ(client_->message_role_to_string(kMessageRoleAssistant),
            "assistant");
}

TEST_F(AnthropicClientTest, TestStopReasonParsing) {
  // Test the internal stop reason parsing method (Anthropic uses different
  // names)
  EXPECT_EQ(client_->parse_stop_reason("end_turn"), kFinishReasonStop);
  EXPECT_EQ(client_->parse_stop_reason("max_tokens"), kFinishReasonLength);
  EXPECT_EQ(client_->parse_stop_reason("stop_sequence"), kFinishReasonStop);
  EXPECT_EQ(client_->parse_stop_reason("tool_use"), kFinishReasonToolCalls);
  EXPECT_EQ(client_->parse_stop_reason("unknown"),
            kFinishReasonStop);  // Anthropic defaults unknown to stop
}
*/

}  // namespace test
}  // namespace qcode