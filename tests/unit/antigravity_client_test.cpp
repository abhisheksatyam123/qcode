#include "ai/antigravity.h"
#include "ai/tui/config.h"
#include "ai/types/generate_options.h"
#include "providers/antigravity/antigravity_request_builder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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
  AntigravityRequestBuilder builder("configured-project");
  GenerateOptions opts;
  opts.model = "gemini-3-flash";
  opts.prompt = "Hello";

  nlohmann::json req = builder.build_request_json(opts);

  // The Antigravity wire format wraps the Gemini request in a Vertex envelope
  // (not the raw OpenAI shape): "request" key + project/identity headers.
  ASSERT_TRUE(req.contains("request")) << req.dump();
  EXPECT_EQ(req["userAgent"].get<std::string>(), "antigravity");
  EXPECT_EQ(req["requestType"].get<std::string>(), "agent");
  EXPECT_EQ(req["project"].get<std::string>(), "configured-project");
  EXPECT_EQ(req["model"].get<std::string>(),
            "gemini-3-flash-agent");  // gemini-3-flash -> *-agent mapping
  ASSERT_TRUE(req["request"].contains("contents"));
}

TEST(AntigravityClientTest, EmbeddingsFailWithoutNetworkRequest) {
  auto client = create_client("dummy-token");
  const auto result = client.embeddings(
      EmbeddingOptions{"embedding-model", "hello"});
  ASSERT_TRUE(result.error.has_value());
  EXPECT_THAT(*result.error, testing::HasSubstr("does not support"));
}

}  // namespace

TEST(AntigravityClientTest, GenerateTextE2E) {
  if (std::getenv("QCODE_RUN_LIVE_TESTS") == nullptr) {
    GTEST_SKIP() << "Set QCODE_RUN_LIVE_TESTS=1 to run provider E2E tests";
  }
  const auto access_token = ai::tui::get_antigravity_token();
  if (access_token.empty()) {
    GTEST_SKIP() << "No usable Antigravity credential";
  }

  Options client_options;
  for (const auto& provider : ai::tui::load_providers_from_config()) {
    if (provider.id == "antigravity") {
      client_options.base_url = provider.api_url;
      client_options.project_id = provider.project_id;
      break;
    }
  }
  Client client = create_client(access_token, client_options);
  ASSERT_TRUE(client.is_valid());
  
  const char* test_model = std::getenv("ANTIGRAVITY_TEST_MODEL");
  GenerateOptions opts;
  opts.model = test_model ? test_model : "gemini-2.5-flash";
  opts.prompt = "Reply with exactly the word: OK";
  GenerateResult out = client.generate_text(opts);
  EXPECT_TRUE(out.is_success()) << out.error_message();
  EXPECT_FALSE(out.text.empty());
}

TEST(AntigravityClientTest, CompactionLiveTest) {
  if (std::getenv("QCODE_RUN_LIVE_TESTS") == nullptr) {
    GTEST_SKIP() << "Set QCODE_RUN_LIVE_TESTS=1 to run provider E2E tests";
  }
  const auto access_token = ai::tui::get_antigravity_token();
  if (access_token.empty()) {
    GTEST_SKIP() << "No usable Antigravity credential";
  }

  Options client_options;
  for (const auto& provider : ai::tui::load_providers_from_config()) {
    if (provider.id == "antigravity") {
      client_options.base_url = provider.api_url;
      client_options.project_id = provider.project_id;
      break;
    }
  }
  Client client = create_client(access_token, client_options);
  ASSERT_TRUE(client.is_valid());

  GenerateOptions opts;
  opts.model = "gemini-3-flash";
  opts.system = "Summarize this conversation into a handoff packet. Output only: ## Tasks\n## Systems";
  opts.messages = {Message::user("User: hi\nAssistant: hello there! How can I help you today?")};

  GenerateResult out = client.generate_text(opts);
  EXPECT_TRUE(out.is_success()) << out.error_message();
  EXPECT_FALSE(out.text.empty());
  std::cout << "[CompactionLiveTest Output]: " << out.text << std::endl;
}

}  // namespace antigravity
}  // namespace ai
