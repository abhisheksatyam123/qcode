#include "ai/tui/config.h"

#include <algorithm>
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

TEST(TuiConfigTest, DetectsExpiredAntigravityTokenFile) {
  const auto path = std::filesystem::temp_directory_path() /
                    "qcode-antigravity-token-test.json";
  {
    std::ofstream output(path);
    output << R"({
      "token": {
        "access_token": "stale-token",
        "refresh_token": "refresh",
        "expiry": "2020-01-01T00:00:00+00:00",
        "expires_in": 3599
      }
    })";
  }
  ScopedEnv api_key("ANTIGRAVITY_API_KEY", "");
  unsetenv("ANTIGRAVITY_API_KEY");
  ScopedEnv token_file("ANTIGRAVITY_TOKEN_FILE", path.string());
  EXPECT_TRUE(antigravity_token_needs_refresh());
  std::filesystem::remove(path);
}

TEST(TuiConfigTest, EnvApiKeySkipsAntigravityRefreshCheck) {
  ScopedEnv api_key("ANTIGRAVITY_API_KEY", "env-token");
  EXPECT_FALSE(antigravity_token_needs_refresh());
}

TEST(TuiConfigTest, AddsCurrentCursorModelsWhenDiscoveryIsUnavailable) {
  const auto path = std::filesystem::temp_directory_path() /
                    "qcode-cursor-config-test.json";
  {
    std::ofstream output(path);
    output << R"({
      "provider": {
        "cursor": {
          "name": "Cursor",
          "models": {
            "composer-2.5": {"name": "Composer 2.5"},
            "claude-opus-4.8": {"name": "Claude Opus 4.8"}
          }
        }
      }
    })";
  }
  ScopedEnv config("OPENCODE_CONFIG", path.string());
  ScopedEnv auth_file("CURSOR_AUTH_FILE", path.string() + ".missing");
  ScopedEnv api_key("CURSOR_API_KEY", "");
  unsetenv("CURSOR_API_KEY");

  const auto providers = load_providers_from_config();

  ASSERT_EQ(providers.size(), 1u);
  const auto& models = providers.front().models;
  const auto has_model = [&models](const std::string& id) {
    return std::any_of(models.begin(), models.end(),
                       [&id](const ModelInfo& model) {
                         return model.id == id;
                       });
  };
  EXPECT_TRUE(has_model("composer-2.5"));
  EXPECT_TRUE(has_model("cursor-grok-4.5-high"));
  EXPECT_TRUE(has_model("gpt-5.6-sol-medium"));
  EXPECT_TRUE(has_model("gpt-5.6-terra-medium"));
  EXPECT_TRUE(has_model("gpt-5.6-luna-medium"));
  EXPECT_TRUE(has_model("claude-fable-5-high"));
  EXPECT_TRUE(has_model("claude-fable-5-thinking-high"));
  EXPECT_FALSE(has_model("claude-opus-4.8"));
  const auto grok = std::find_if(
      models.begin(), models.end(), [](const ModelInfo& model) {
        return model.id == "cursor-grok-4.5-high";
      });
  ASSERT_NE(grok, models.end());
  EXPECT_EQ(grok->context_window, 200000);
  EXPECT_TRUE(grok->tool_call);
  std::filesystem::remove(path);
}

}  // namespace
}  // namespace tui
}  // namespace ai
