#pragma once

#include <qcode/core/generate_options.h>
#include <qcode/core/message.h>

#include <cstdlib>
#include <regex>
#include <string>
#include <thread>
#include <utility>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace qcode {
namespace test {

/// GitHub Actions injects missing secrets as "". Treat those as unset.
inline const char* env_or_null(const char* name) {
  const char* v = std::getenv(name);
  return (v != nullptr && v[0] != '\0') ? v : nullptr;
}

/// Live-network tests (real provider APIs) run only when QCODE_LIVE_TESTS is
/// set. CI sets it for PRs labelled `live-tests` and for manual runs.
inline bool live_tests_enabled() { return env_or_null("QCODE_LIVE_TESTS") != nullptr; }

/// Answers every POST on 127.0.0.1 with a fixed status and body. Lets error-path
/// tests exercise a real HTTP round trip without leaving the machine.
class LocalStatusServer {
 public:
  LocalStatusServer(int status, std::string body) {
    // Pre-routing runs for every path, so the provider's endpoint is irrelevant.
    server_.set_pre_routing_handler(
        [status, body = std::move(body)](const httplib::Request&,
                                         httplib::Response& res) {
          res.status = status;
          res.set_content(body, "application/json");
          return httplib::Server::HandlerResponse::Handled;
        });
    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ > 0) {
      thread_ = std::thread([this] { server_.listen_after_bind(); });
    }
  }
  ~LocalStatusServer() {
    server_.stop();
    if (thread_.joinable()) thread_.join();
  }
  LocalStatusServer(const LocalStatusServer&) = delete;
  LocalStatusServer& operator=(const LocalStatusServer&) = delete;

  std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

 private:
  httplib::Server server_;
  int port_ = -1;
  std::thread thread_;
};

// Base test fixture with common setup
class AITestFixture : public ::testing::Test {
 protected:
  void SetUp() override;
  void TearDown() override;

  // Common test data
  static constexpr const char* kTestApiKey = "sk-test123456789";
  static constexpr const char* kTestModel = "gpt-4o";
  static constexpr const char* kTestPrompt = "Hello, world!";
  static constexpr const char* kTestBaseUrl = "https://api.openai.com";

  // Anthropic test data
  static constexpr const char* kTestAnthropicApiKey = "sk-ant-test123456789";
  static constexpr const char* kTestAnthropicModel =
      "claude-sonnet-4-5-20250929";
  static constexpr const char* kTestAnthropicBaseUrl =
      "https://api.anthropic.com";
};

// OpenAI specific test fixture
class OpenAITestFixture : public AITestFixture {
 protected:
  void SetUp() override;

  // Sample request/response data
  GenerateOptions createBasicOptions();
  GenerateOptions createAdvancedOptions();
  nlohmann::json createValidChatCompletionResponse();
  nlohmann::json createErrorResponse(int status_code,
                                     const std::string& message);
  nlohmann::json createStreamResponse();

  // Message builders
  Messages createSampleConversation();
  Message createUserMessage(const std::string& content);
  Message createAssistantMessage(const std::string& content);
  Message createSystemMessage(const std::string& content);
};

// Anthropic specific test fixture
class AnthropicTestFixture : public AITestFixture {
 protected:
  void SetUp() override;

  // Sample request/response data
  GenerateOptions createBasicAnthropicOptions();
  GenerateOptions createAdvancedAnthropicOptions();
  nlohmann::json createValidAnthropicResponse();
  nlohmann::json createAnthropicErrorResponse(int status_code,
                                              const std::string& message);
  nlohmann::json createAnthropicStreamResponse();

  // Message builders
  Messages createSampleAnthropicConversation();
  Message createAnthropicUserMessage(const std::string& content);
  Message createAnthropicAssistantMessage(const std::string& content);
  Message createAnthropicSystemMessage(const std::string& content);
};

// Test data generators
class TestDataGenerator {
 public:
  // Generate various GenerateOptions configurations
  static std::vector<GenerateOptions> generateOptionsVariations();

  // Generate OpenAI API responses
  static nlohmann::json createMinimalValidResponse();
  static nlohmann::json createFullValidResponse();
  static nlohmann::json createResponseWithUsage(int prompt_tokens,
                                                int completion_tokens);
  static nlohmann::json createResponseWithMetadata();

  // Generate error scenarios
  static std::vector<std::pair<int, std::string>> errorScenarios();

  // Generate streaming events
  static std::vector<std::string> createStreamingEvents();
  static std::vector<std::string> createAnthropicStreamingEvents();

  // Edge case data
  static GenerateOptions createEdgeCaseOptions();
  static std::string createLargePrompt(size_t size);
  static nlohmann::json createMalformedResponse();
};

// Assertion helpers
class TestAssertions {
 public:
  // Assert GenerateResult properties
  static void assertSuccess(const GenerateResult& result);
  static void assertError(const GenerateResult& result,
                          const std::string& expected_error = "");
  static void assertUsage(const Usage& usage,
                          int expected_prompt,
                          int expected_completion);

  // Assert JSON structure
  static void assertValidRequestJson(const nlohmann::json& request);
  static void assertHasRequiredFields(const nlohmann::json& json,
                                      const std::vector<std::string>& fields);

  // Assert OpenAI client state
  static void assertClientValid(const class OpenAIClient& client);
  static void assertModelSupported(const class OpenAIClient& client,
                                   const std::string& model);

  // Assert Anthropic client state
  static void assertAnthropicClientValid(const class AnthropicClient& client);
  static void assertAnthropicModelSupported(const class AnthropicClient& client,
                                            const std::string& model);
};

}  // namespace test
}  // namespace qcode