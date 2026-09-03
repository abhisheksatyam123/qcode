#include <qcode/core/config.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

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

class ScopedConfig {
 public:
  explicit ScopedConfig(std::string_view json)
      : path_(std::filesystem::temp_directory_path() /
              ("qcode-config-test-" + std::to_string(++seq_) + ".json")),
        env_("OPENCODE_CONFIG", path_.string()) {
    std::ofstream output(path_);
    output << json;
  }
  ~ScopedConfig() { std::filesystem::remove(path_); }

  std::vector<ProviderInfo> load() const { return load_providers_from_config(); }

 private:
  static inline int seq_ = 0;
  std::filesystem::path path_;
  ScopedEnv env_;
};

const ProviderInfo* FindProvider(const std::vector<ProviderInfo>& providers,
                                 std::string_view id) {
  const auto it = std::find_if(
      providers.begin(), providers.end(),
      [id](const ProviderInfo& provider) { return provider.id == id; });
  return it == providers.end() ? nullptr : &*it;
}

const ModelInfo* FindModel(const std::vector<ModelInfo>& models,
                           std::string_view id) {
  const auto it = std::find_if(
      models.begin(), models.end(),
      [id](const ModelInfo& model) { return model.id == id; });
  return it == models.end() ? nullptr : &*it;
}

TEST(TuiConfigTest, LoadsProviderOptionsFromJson) {
  ScopedConfig config(R"({
      "provider": {
        "opencode": {
          "name": "OpenCode Zen",
          "protocol": "chat_completions",
          "options": {
            "baseURL": "https://zen.example.test/v1/",
            "apiKey": "{env:QCODE_TEST_PROVIDER_KEY}",
            "headers": {
              "User-Agent": "from-json",
              "X-Test": "{env:QCODE_TEST_HEADER}"
            }
          },
          "models": {
            "model-1": {
              "name": "Model One",
              "reasoning": true,
              "tool_call": true,
              "protocol": "responses",
              "reasoning_efforts": ["low", "high"],
              "reasoning_default": "high",
              "limit": {"context": 1000, "output": 200}
            }
          }
        }
      }
    })");
  ScopedEnv key("QCODE_TEST_PROVIDER_KEY", "secret");
  ScopedEnv header("QCODE_TEST_HEADER", "header-value");

  const auto providers = config.load();
  ASSERT_EQ(providers.size(), 1u);
  const auto& provider = providers.front();
  EXPECT_EQ(provider.api_url, "https://zen.example.test/v1");
  EXPECT_EQ(provider.api_key, "secret");
  EXPECT_EQ(provider.headers.at("User-Agent"), "from-json");
  EXPECT_EQ(provider.headers.at("X-Test"), "header-value");
  EXPECT_EQ(provider.protocol, "chat_completions");
  ASSERT_EQ(provider.models.size(), 1u);
  const auto& model = provider.models.front();
  EXPECT_EQ(model.protocol, "responses");
  EXPECT_EQ(model.context_window, 1000);
  EXPECT_EQ(model.output_limit, 200);
  EXPECT_TRUE(model.reasoning);
  EXPECT_TRUE(model.tool_call);
  ASSERT_EQ(model.reasoning_efforts.size(), 2u);
  EXPECT_EQ(model.reasoning_efforts[0], "low");
  EXPECT_EQ(model.reasoning_efforts[1], "high");
  EXPECT_EQ(model.reasoning_default, "high");
}

TEST(TuiConfigTest, ConfiguredModelsAreThePicker) {
  ScopedConfig config(R"({
      "provider": {
        "opencode": {
          "name": "OpenCode Zen",
          "models": {
            "configured-a": {"name": "A", "tool_call": true},
            "configured-b": {"name": "B", "tool_call": false}
          }
        },
        "openrouter": {"name": "OpenRouter", "models": {}}
      }
    })");

  const auto providers = config.load();
  ASSERT_EQ(providers.size(), 2u);

  const auto* zen = FindProvider(providers, "opencode");
  ASSERT_NE(zen, nullptr);
  ASSERT_EQ(zen->models.size(), 2u);
  const auto* keep = FindModel(zen->models, "configured-a");
  const auto* plain = FindModel(zen->models, "configured-b");
  ASSERT_NE(keep, nullptr);
  ASSERT_NE(plain, nullptr);
  EXPECT_TRUE(keep->tool_call);
  EXPECT_FALSE(plain->tool_call);

  const auto* openrouter = FindProvider(providers, "openrouter");
  ASSERT_NE(openrouter, nullptr);
  EXPECT_TRUE(openrouter->models.empty());
}

TEST(TuiConfigTest, DropsRetiredZenIds) {
  ScopedConfig config(R"cfg({
      "provider": {
        "opencode": {
          "name": "OpenCode Zen",
          "models": {
            "hy3-free": {"name": "HY3 retired"},
            "keep-me": {"name": "Keep"}
          }
        }
      }
    })cfg");

  const auto providers = config.load();
  ASSERT_EQ(providers.size(), 1u);
  ASSERT_EQ(providers.front().models.size(), 1u);
  EXPECT_EQ(providers.front().models.front().id, "keep-me");
}

TEST(TuiConfigTest, RemapsCursorPickerIdsWithoutAddingFamilies) {
  ScopedConfig config(R"({
      "provider": {
        "cursor": {
          "name": "Cursor",
          "models": {
            "cursor-grok-4.6": {"name": "Cursor Grok 4.6"},
            "composer-2.5": {"name": "Composer 2.5"}
          }
        }
      }
    })");

  const auto models = config.load().front().models;
  ASSERT_EQ(models.size(), 2u);
  ASSERT_NE(FindModel(models, "grok-4.6"), nullptr);
  ASSERT_NE(FindModel(models, "composer-2.5"), nullptr);
  EXPECT_EQ(FindModel(models, "cursor-grok-4.6"), nullptr);
}

TEST(TuiConfigTest, AntigravityTokenRefreshHelpers) {
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
  unsetenv("ANTIGRAVITY_API_KEY");
  ScopedEnv token_file("ANTIGRAVITY_TOKEN_FILE", path.string());
  EXPECT_TRUE(antigravity_token_needs_refresh());
  std::filesystem::remove(path);

  ScopedEnv api_key("ANTIGRAVITY_API_KEY", "env-token");
  EXPECT_FALSE(antigravity_token_needs_refresh());
}

}  // namespace
}  // namespace qcode
