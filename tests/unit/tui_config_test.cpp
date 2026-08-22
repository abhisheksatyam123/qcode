#include <qcode/core/config.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>

#include <gtest/gtest.h>

namespace qcode {
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

TEST(TuiConfigTest, HydratesMissingOpenCodeFreeModels) {
  const auto path = std::filesystem::temp_directory_path() /
                    "qcode-opencode-free-models-test.json";
  {
    std::ofstream output(path);
    output << R"cfg({
      "provider": {
        "opencode": {
          "name": "OpenCode Zen",
          "models": {
            "hy3-free": {"name": "HY3 (Free)", "tool_call": true},
            "north-mini-code-free": {"name": "North Mini Code (Free)"}
          }
        }
      }
    })cfg";
  }
  ScopedEnv config("OPENCODE_CONFIG", path.string());
  const auto providers = load_providers_from_config();
  ASSERT_EQ(providers.size(), 1u);
  EXPECT_EQ(providers.front().id, "opencode");
  EXPECT_EQ(providers.front().api_url, "https://opencode.ai/zen/v1");
  EXPECT_EQ(providers.front().headers.at("User-Agent"), "opencode/1.18.18");
  EXPECT_EQ(providers.front().headers.at("x-opencode-client"), "cli");
  const auto& models = providers.front().models;
  const auto has_model = [&models](const std::string& id) {
    return std::any_of(models.begin(), models.end(),
                       [&id](const ModelInfo& model) {
                         return model.id == id;
                       });
  };
  EXPECT_TRUE(has_model("big-pickle"));
  EXPECT_TRUE(has_model("mimo-v2.5-free"));
  EXPECT_TRUE(has_model("hy3-free"));
  EXPECT_TRUE(has_model("laguna-s-2.1-free"));
  EXPECT_TRUE(has_model("nemotron-3.5-lightning-free"));
  EXPECT_FALSE(has_model("north-mini-code-free"));
  const auto pickle = std::find_if(
      models.begin(), models.end(), [](const ModelInfo& model) {
        return model.id == "big-pickle";
      });
  ASSERT_NE(pickle, models.end());
  EXPECT_EQ(pickle->context_window, 200000);
  EXPECT_TRUE(pickle->tool_call);
  EXPECT_TRUE(pickle->reasoning);
  std::filesystem::remove(path);
}

TEST(TuiConfigTest, HandlesCursorProviderConfigWhenDiscoveryIsUnavailable) {
  const auto path = std::filesystem::temp_directory_path() /
                    "qcode-cursor-config-test.json";
  {
    std::ofstream output(path);
    output << R"({
      "provider": {
        "cursor": {
          "name": "Cursor",
          "models": {
            "cursor-grok-4.6": {"name": "Cursor Grok 4.6"},
            "composer-2.5": {"name": "Composer 2.5"}
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
  EXPECT_TRUE(has_model("cursor-grok-4.6"));
  EXPECT_FALSE(has_model("composer-2.5"));
  std::filesystem::remove(path);
}

}  // namespace
}  // namespace qcode
