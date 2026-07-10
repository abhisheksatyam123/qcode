#include "ai/antigravity.h"
#include "ai/tui/config.h"
#include "ai/types/generate_options.h"
#include "ai/logger.h"
#include "providers/antigravity/antigravity_request_builder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <fstream>

namespace ai {
namespace antigravity {
namespace {

TEST(AntigravityClientTest, CreateClientIsValidAndNamed) {
  Client client = create_client(
      "dummy-token",
      "https://daily-cloudcode-pa.googleapis.com/v1internal");
  EXPECT_TRUE(client.is_valid());  // api_key non-empty
  EXPECT_EQ(client.provider_name(), "antigravity");
  EXPECT_EQ(client.default_model(), "gemini-3-flash");
}

TEST(AntigravityClientTest, RequestBuilderProducesAntigravityEnvelope) {
  AntigravityRequestBuilder builder;
  GenerateOptions opts;
  opts.model = "gemini-3-flash";
  opts.prompt = "Hello";

  nlohmann::json req = builder.build_request_json(opts);

  // The Antigravity wire format wraps the Gemini request in a Vertex envelope
  // (not the raw OpenAI shape): "request" key + project/identity headers.
  ASSERT_TRUE(req.contains("request")) << req.dump();
  EXPECT_EQ(req["userAgent"].get<std::string>(), "antigravity");
  EXPECT_EQ(req["requestType"].get<std::string>(), "agent");
  EXPECT_EQ(req["model"].get<std::string>(),
            "gemini-3-flash-agent");  // gemini-3-flash -> *-agent mapping
  ASSERT_TRUE(req["request"].contains("contents"));
}

}  // namespace

TEST(AntigravityClientTest, GenerateTextE2E) {
  // End-to-end proof that the Antigravity client works against Google's
  // cloudcode-pa endpoint. Uses the file fallback + OAuth refresh path
  // (matching opencode's logic). No ANTIGRAVITY_API_KEY required.
  
  // Read the token file (opencode's antigravity-oauth-token format)
  const char* token_file_env = std::getenv("ANTIGRAVITY_TOKEN_FILE");
  std::string token_path = token_file_env ? token_file_env : "";
  if (token_path.empty()) {
    const char* home = std::getenv("HOME");
    token_path = home ? std::string(home) + "/.gemini/antigravity-cli/antigravity-oauth-token" : "";
  }
  
  nlohmann::json token_data;
  {
    std::ifstream f(token_path);
    if (!f.is_open()) {
      GTEST_SKIP() << "Antigravity token file not found at " << token_path;
    }
    token_data = nlohmann::json::parse(f);
  }
  
  // Extract refresh_token and do OAuth refresh (hardcoded creds from opencode)
  auto tok = token_data.value("token", nlohmann::json::object());
  std::string refresh_token = tok.value("refresh_token", "");
  if (refresh_token.empty()) {
    GTEST_SKIP() << "No refresh_token in antigravity token file";
  }
  
  // OAuth refresh with hardcoded client_id/secret (from opencode)
  const char* client_id = "1071006060591-tmhssin2h21lcre235vtolojh4g403ep.apps.googleusercontent.com";
  const char* client_secret = "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";
  
  // Build and execute refresh request
  std::string post_data = "grant_type=refresh_token&refresh_token=" + refresh_token +
                          "&client_id=" + client_id + "&client_secret=" + client_secret;
  
  // Use httplib to do the refresh
  httplib::Client cli("https://oauth2.googleapis.com");
  cli.set_connection_timeout(0, 20000000);  // 20 sec
  auto res = cli.Post("/token", post_data, "application/x-www-form-urlencoded");
  
  if (!res || res->status != 200) {
    GTEST_SKIP() << "OAuth refresh failed: " << (res ? std::to_string(res->status) : "connection error");
  }
  
  nlohmann::json refresh_resp = nlohmann::json::parse(res->body);
  std::string access_token = refresh_resp.value("access_token", "");
  if (access_token.empty()) {
    GTEST_SKIP() << "OAuth refresh returned empty access_token";
  }
  
  // Enable debug logging
  ai::logger::install_logger(std::make_shared<ai::logger::ConsoleLogger>(ai::logger::LogLevel::kLogLevelDebug));
  
  Client client = create_client(access_token);
  ASSERT_TRUE(client.is_valid());
  
  const char* test_model = std::getenv("ANTIGRAVITY_TEST_MODEL");
  GenerateOptions opts;
  opts.model = test_model ? test_model : "gemini-2.5-flash";
  opts.prompt = "Reply with exactly the word: OK";
  GenerateResult out = client.generate_text(opts);
  EXPECT_TRUE(out.is_success()) << out.error_message();
  EXPECT_FALSE(out.text.empty());
}

}  // namespace antigravity
}  // namespace ai
