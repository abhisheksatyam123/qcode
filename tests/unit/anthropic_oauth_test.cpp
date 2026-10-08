#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "providers/anthropic/anthropic_oauth.h"
#include "providers/anthropic/anthropic_request_builder.h"
#include "providers/anthropic/anthropic_client.h"
#include <qcode/config/config.h>
#include <qcode/providers/registry.h>

namespace qcode {
namespace anthropic {
namespace {

TEST(AnthropicOAuthTest, Base64UrlEncodeMatchesStandard) {
  // RFC 7636 test vector or known data
  const unsigned char data[] = {0, 1, 2, 3, 250, 255};
  std::string encoded = base64url_encode(data, sizeof(data));
  EXPECT_EQ(encoded, "AAECA_r_");
}

TEST(AnthropicOAuthTest, Sha1HexComputesCorrectly) {
  EXPECT_EQ(sha1_hex("hello"), "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d");
}

TEST(AnthropicOAuthTest, RewriteClaudePromptTags) {
  std::string input = "Here are <directories>/src</directories> and <env>PATH=/bin</env>";
  std::string rewritten = rewrite_claude_prompt_tags(input);
  EXPECT_EQ(rewritten, "Here are [directories]/src[/directories] and [env]PATH=/bin[/env]");
}

TEST(AnthropicOAuthTest, FormatBillingHeaderMatchesClaudeCliPattern) {
  std::string header = format_billing_header();
  EXPECT_THAT(header, testing::StartsWith("x-anthropic-billing-header: cc_version=2.1.300."));
  EXPECT_THAT(header, testing::EndsWith("; cc_entrypoint=cli;\n"));
}

TEST(AnthropicOAuthTest, BillingFingerprintIsStableWithinProcess) {
  // Prompt caching requires a byte-stable system prefix; the fingerprint must
  // not rotate between requests in the same process.
  EXPECT_EQ(generate_billing_fingerprint(), generate_billing_fingerprint());
  EXPECT_EQ(format_billing_header(), format_billing_header());
  EXPECT_EQ(format_billing_header_value(), format_billing_header_value());
}

TEST(AnthropicOAuthTest, BillingHeaderValueMatchesHttpHeaderShape) {
  // HTTP header value: no name prefix, no trailing newline.
  const std::string v = format_billing_header_value();
  EXPECT_THAT(v, testing::StartsWith("cc_version=2.1.300."));
  EXPECT_THAT(v, testing::EndsWith("; cc_entrypoint=cli;"));
  EXPECT_EQ(v.find("\n"), std::string::npos);
}

TEST(AnthropicOAuthTest, TokenExpirationCheck) {
  int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  // Expired in the past
  EXPECT_TRUE(is_claude_token_expired(now_ms - 1000));
  // Expiring within 4 minutes (less than 5 min buffer)
  EXPECT_TRUE(is_claude_token_expired(now_ms + (4 * 60 * 1000)));
  // Expiring in 2 hours (more than 5 min buffer)
  EXPECT_FALSE(is_claude_token_expired(now_ms + (2 * 3600 * 1000)));
  // No expiry set (zero or negative)
  EXPECT_FALSE(is_claude_token_expired(0));
}

TEST(AnthropicOAuthTest, CredentialsReadWriteRoundtrip) {
  const auto tmp_dir = std::filesystem::temp_directory_path() / "qcode_test_claude_auth";
  std::filesystem::create_directories(tmp_dir);
  const auto creds_file = tmp_dir / "test_credentials.json";

  ClaudeCredentials creds;
  creds.access_token = "sk-ant-oat01-roundtrip-test";
  creds.refresh_token = "refresh-roundtrip-test";
  creds.expires_at_ms = 1800000000000ULL;
  creds.subscription_type = "max";
  creds.rate_limit_tier = "tier_1";

  EXPECT_TRUE(write_claude_credentials(creds, creds_file));

  auto loaded = read_claude_credentials(creds_file);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->access_token, creds.access_token);
  EXPECT_EQ(loaded->refresh_token, creds.refresh_token);
  EXPECT_EQ(loaded->expires_at_ms, creds.expires_at_ms);
  EXPECT_EQ(loaded->subscription_type, "max");
  EXPECT_EQ(loaded->rate_limit_tier, "tier_1");

  std::error_code ec;
  std::filesystem::remove_all(tmp_dir, ec);
}

TEST(AnthropicOAuthTest, RequestBuilderInjectsBillingHeaderAndRewritesTags) {
  AnthropicRequestBuilder builder(/*is_oauth=*/true);
  GenerateOptions options;
  options.model = "claude-sonnet-5-5";
  options.system = "System info in <directories>/home/user</directories> and <env>VAR=1</env>";
  options.messages = {Message::user("Hello")};

  nlohmann::json req = builder.build_request_json(options);

  ASSERT_TRUE(req.contains("system"));
  ASSERT_TRUE(req["system"].is_array());
  ASSERT_FALSE(req["system"].empty());

  std::string system_text = req["system"][0]["text"].get<std::string>();
  // Billing attribution travels as an HTTP header now, never as system text
  // (any prefix byte change breaks the prompt-cache prefix match).
  EXPECT_THAT(system_text, testing::Not(testing::HasSubstr("x-anthropic-billing-header")));
  EXPECT_THAT(system_text, testing::StartsWith("System info in [directories]"));
  EXPECT_THAT(system_text, testing::HasSubstr("[directories]/home/user[/directories]"));
  EXPECT_THAT(system_text, testing::HasSubstr("[env]VAR=1[/env]"));
  EXPECT_FALSE(system_text.find("<directories>") != std::string::npos);
  EXPECT_FALSE(system_text.find("<env>") != std::string::npos);
}

TEST(AnthropicOAuthTest, ClientSetsClaudeCodeHeadersWhenUsingOAuth) {
  CompatibleOptions options;
  options.is_oauth = true;
  AnthropicClient client("sk-ant-oat01-my-token", options);

  GenerateOptions gen_opts;
  gen_opts.model = "claude-sonnet-5-5";
  gen_opts.system = "Test agent";
  gen_opts.messages = {Message::user("Hi")};

  AnthropicRequestBuilder builder(true);
  providers::ProviderConfig config{
      .api_key = "sk-ant-oat01-my-token",
      .base_url = "https://api.anthropic.com",
      .completions_endpoint_path = "/v1/messages",
      .auth_header_name = "Authorization",
      .auth_header_prefix = "Bearer ",
      .extra_headers = {
          {"anthropic-version", "2023-06-01"},
          {"anthropic-beta", kAnthropicBetaHeaders},
          {"User-Agent", "claude-cli/2.1.300 (ext, cli)"},
          {"x-app", "cli"},
          {"x-claude-code-session-id", sha1_hex("anthropic-session-test")}
      }
  };

  auto headers = builder.build_headers(config);
  EXPECT_EQ(headers.find("Authorization")->second, "Bearer sk-ant-oat01-my-token");
  EXPECT_EQ(headers.find("anthropic-beta")->second, kAnthropicBetaHeaders);
  EXPECT_EQ(headers.find("User-Agent")->second, "claude-cli/2.1.300 (ext, cli)");
  EXPECT_EQ(headers.find("x-app")->second, "cli");
  EXPECT_TRUE(headers.find("x-claude-code-session-id") != headers.end());
}

}  // namespace
}  // namespace anthropic
}  // namespace qcode
