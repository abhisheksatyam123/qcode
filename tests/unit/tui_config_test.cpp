#include <qcode/config/config.h>
#include <qcode/transform/provider_transform.h>
#include <qcode/ui/commands.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

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
              ("qcode-config-test-" + std::to_string(getpid()) + "-" +
               std::to_string(++seq_) + ".json")),
        env_("OPENCODE_CONFIG", path_.string()) {
    std::ofstream output(path_);
    output << json;
  }
  ~ScopedConfig() { std::filesystem::remove(path_); }

  std::vector<ProviderInfo> load() const { return load_providers_from_config(); }

 private:
  static inline std::atomic<int> seq_{0};
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
  ASSERT_NE(FindModel(models, "cursor-grok-4.6"), nullptr);
  ASSERT_NE(FindModel(models, "composer-2.5"), nullptr);
  EXPECT_EQ(FindModel(models, "grok-4.6"), nullptr);
}

TEST(TuiConfigTest, NestedReasoningObjectSetsEffortsAndProtocol) {
  ScopedConfig config(R"({
      "provider": {
        "opencode": {
          "models": {
            "muse-spark-1.3-contributor-free": {
              "name": "Muse Spark",
              "tool_call": true,
              "protocol": "responses",
              "reasoning": {
                "efforts": ["low", "high"],
                "default": "high",
                "field": "reasoning"
              }
            }
          }
        }
      }
    })");

  const auto providers = config.load();
  ASSERT_EQ(providers.size(), 1u);
  const auto* model = FindModel(providers.front().models,
                                "muse-spark-1.3-contributor-free");
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(model->reasoning);
  EXPECT_EQ(model->protocol, "responses");
  EXPECT_EQ(model->reasoning_field, "reasoning");
  EXPECT_EQ(model->reasoning_default, "high");
  ASSERT_EQ(model->reasoning_efforts.size(), 2u);
  EXPECT_EQ(model->reasoning_efforts[0], "low");
  EXPECT_EQ(model->reasoning_efforts[1], "high");
  EXPECT_EQ(ProviderTransform::default_variant(*model), "high");

  const auto entries = build_variant_entries(*model);
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].id, "off");
  EXPECT_EQ(entries[1].id, "low");
  EXPECT_EQ(entries[2].id, "high");
}

TEST(TuiConfigTest, PickerVariantsComeFromJsonNotHardcodedCatalog) {
  ScopedConfig config(R"({
      "provider": {
        "openrouter": {
          "name": "OpenRouter",
          "models": {
            "deepseek/deepseek-v4-flash-0731": {
              "name": "DeepSeek V4 Flash",
              "reasoning": true,
              "reasoning_efforts": ["low", "medium", "high", "max"],
              "reasoning_default": "medium"
            }
          }
        },
        "antigravity": {
          "name": "Antigravity",
          "models": {
            "gemini-3.8-flash": {
              "name": "Gemini 3.8 Flash",
              "reasoning": true,
              "reasoning_efforts": ["low", "medium", "high"],
              "reasoning_default": "medium"
            }
          }
        }
      }
    })");

  const auto providers = config.load();
  ASSERT_EQ(providers.size(), 2u);
  const auto* openrouter = FindProvider(providers, "openrouter");
  const auto* antigravity = FindProvider(providers, "antigravity");
  ASSERT_NE(openrouter, nullptr);
  ASSERT_NE(antigravity, nullptr);

  const auto* deepseek =
      FindModel(openrouter->models, "deepseek/deepseek-v4-flash-0731");
  const auto* gemini = FindModel(antigravity->models, "gemini-3.8-flash");
  ASSERT_NE(deepseek, nullptr);
  ASSERT_NE(gemini, nullptr);
  EXPECT_EQ(ProviderTransform::default_variant(*deepseek), "medium");
  EXPECT_EQ(ProviderTransform::default_variant(*gemini), "medium");
  EXPECT_TRUE(ProviderTransform::is_allowed_variant(*deepseek, "max"));
  EXPECT_FALSE(ProviderTransform::is_allowed_variant(*gemini, "max"));

  std::vector<std::string> gemini_ids;
  for (const auto& entry : build_variant_entries(*gemini)) {
    gemini_ids.push_back(entry.id);
  }
  EXPECT_EQ(gemini_ids, (std::vector<std::string>{"off", "low", "medium", "high"}));
}

TEST(TuiConfigTest, HomeConfigDrivesProvidersModelsAndEfforts) {
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    GTEST_SKIP() << "HOME is unset";
  }
  const auto path = std::filesystem::path(home) / ".config/opencode/opencode.json";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "no ~/.config/opencode/opencode.json";
  }
  ScopedEnv env("OPENCODE_CONFIG", path.string());
  const auto providers = load_providers_from_config();
  ASSERT_FALSE(providers.empty());
  EXPECT_NE(FindProvider(providers, "opencode"), nullptr);
  EXPECT_NE(FindProvider(providers, "openrouter"), nullptr);
  EXPECT_NE(FindProvider(providers, "antigravity"), nullptr);

  const auto* zen = FindProvider(providers, "opencode");
  ASSERT_NE(zen, nullptr);
  EXPECT_EQ(FindModel(zen->models, "hy3-free"), nullptr);
  const auto* muse = FindModel(zen->models, "muse-spark-1.3-contributor-free");
  if (muse != nullptr) {
    EXPECT_EQ(muse->protocol, "responses");
    EXPECT_EQ(muse->reasoning_field, "reasoning");
    EXPECT_EQ(ProviderTransform::default_variant(*muse), "medium");
    EXPECT_FALSE(ProviderTransform::is_allowed_variant(*muse, "max"));
  }

  const auto* antigravity = FindProvider(providers, "antigravity");
  ASSERT_NE(antigravity, nullptr);
  const auto* gemini = FindModel(antigravity->models, "gemini-3.8-flash");
  if (gemini != nullptr) {
    EXPECT_EQ(ProviderTransform::default_variant(*gemini), "medium");
    EXPECT_TRUE(ProviderTransform::is_allowed_variant(*gemini, "high"));
    EXPECT_FALSE(ProviderTransform::is_allowed_variant(*gemini, "max"));
  }

  const auto* openrouter = FindProvider(providers, "openrouter");
  ASSERT_NE(openrouter, nullptr);
  const auto* deepseek =
      FindModel(openrouter->models, "deepseek/deepseek-v4-flash-0731");
  if (deepseek != nullptr) {
    EXPECT_TRUE(ProviderTransform::is_allowed_variant(*deepseek, "max"));
    EXPECT_EQ(ProviderTransform::default_variant(*deepseek), "medium");
  }

  for (const auto& provider : providers) {
    for (const auto& model : provider.models) {
      if (!model.reasoning) continue;
      EXPECT_FALSE(ProviderTransform::reasoning_variants(model).empty())
          << provider.id << "/" << model.id;
    }
  }
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
