#include "ai/tui/config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>

#include <gtest/gtest.h>

namespace ai {
namespace tui {
namespace {

class ScopedEnv {
 public:
  ScopedEnv(const char* name, const std::string& value) : name_(name) {
    if (const char* old = std::getenv(name)) saved_ = old;
    setenv(name, value.c_str(), 1);
  }
  ~ScopedEnv() {
    if (saved_.has_value()) {
      setenv(name_.c_str(), saved_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::optional<std::string> saved_;
};

TEST(TuiConfigTest, LoadsModernOpenCodeProviderOptions) {
  const auto path = std::filesystem::temp_directory_path() /
                    "qcode-opencode-config-test.json";
  {
    std::ofstream output(path);
    output << R"({
      "provider": {
        "custom": {
          "name": "Custom",
          "npm": "@ai-sdk/openai",
          "options": {
            "baseURL": "https://example.test/v1/",
            "apiKey": "{env:QCODE_TEST_PROVIDER_KEY}",
            "headers": {"X-Test": "{env:QCODE_TEST_HEADER}"}
          },
          "models": {
            "model-1": {
              "name": "Model One",
              "reasoning": true,
              "tool_call": true,
              "limit": {"context": 1000, "output": 200}
            }
          }
        }
      }
    })";
  }
  ScopedEnv config("OPENCODE_CONFIG", path.string());
  ScopedEnv key("QCODE_TEST_PROVIDER_KEY", "secret");
  ScopedEnv header("QCODE_TEST_HEADER", "header-value");
  const auto providers = load_providers_from_config();
  ASSERT_EQ(providers.size(), 1u);
  const auto& provider = providers.front();
  EXPECT_EQ(provider.id, "custom");
  EXPECT_EQ(provider.api_url, "https://example.test/v1");
  EXPECT_EQ(provider.api_key, "secret");
  EXPECT_EQ(provider.headers.at("X-Test"), "header-value");
  EXPECT_EQ(provider.protocol, "responses");
  ASSERT_EQ(provider.models.size(), 1u);
  EXPECT_EQ(provider.models.front().context_window, 1000);
  EXPECT_EQ(provider.models.front().output_limit, 200);
  EXPECT_TRUE(provider.models.front().reasoning);
  EXPECT_TRUE(provider.models.front().tool_call);
  std::filesystem::remove(path);
}

}  // namespace
}  // namespace tui
}  // namespace ai
