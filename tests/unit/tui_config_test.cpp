#include <gmock/gmock.h>
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

TEST(TuiConfigTest, VariantObjectsThinkingAndModelDefaults) {
  ScopedConfig config(R"({
      "provider": {
        "anthropic": {
          "model_defaults": {
            "reasoning": true,
            "thinking": {"type": "adaptive", "display": "summarized", "allow_off": false},
            "limit": {"context": 1000000, "output": 128000},
            "variants": {
              "low": {},
              "high": {"max_tokens": 32000, "description": "Deep"},
              "xhigh": {"max_tokens": 64000},
              "max": {"max_tokens": 64000},
              "ultra": {"label": "Ultra", "effort": "max", "max_tokens": 128000,
                        "prompt": "Think harder.", "description": "Deepest"}
            }
          },
          "models": {
            "claude-opus-5-5": {
              "name": "Claude Opus 5.5",
              "reasoning_default": "xhigh",
              "cost": {"input": 4, "output": 20, "cache_read": 0.2, "cache_write": 5}
            },
            "claude-haiku-5-5": {
              "name": "Claude Haiku 5.5",
              "reasoning_efforts": ["low", "medium"],
              "variants": {"ultra": null, "xhigh": {"disabled": true}},
              "thinking": {"display": "omitted"}
            }
          }
        }
      }
    })");

  const auto providers = config.load();
  ASSERT_EQ(providers.size(), 1u);
  const auto* opus = FindModel(providers.front().models, "claude-opus-5-5");
  ASSERT_NE(opus, nullptr);
  EXPECT_TRUE(opus->reasoning);
  EXPECT_EQ(opus->context_window, 1000000);
  EXPECT_EQ(opus->output_limit, 128000);
  EXPECT_EQ(opus->thinking_type, "adaptive");
  EXPECT_EQ(opus->thinking_display, "summarized");
  EXPECT_FALSE(opus->thinking_allow_off);
  EXPECT_DOUBLE_EQ(opus->input_cost, 4.0);
  EXPECT_DOUBLE_EQ(opus->cache_read_cost, 0.2);
  EXPECT_DOUBLE_EQ(opus->cache_write_cost, 5.0);
  EXPECT_THAT(opus->reasoning_efforts,
              testing::ElementsAre("low", "high", "xhigh", "max", "ultra"));
  EXPECT_EQ(ProviderTransform::default_variant(*opus), "xhigh");
  const auto* ultra = ProviderTransform::find_variant(*opus, "ultra");
  ASSERT_NE(ultra, nullptr);
  EXPECT_EQ(ultra->label, "Ultra");
  EXPECT_EQ(ultra->effort, "max");
  EXPECT_EQ(ultra->max_tokens, 128000);
  EXPECT_EQ(ultra->prompt, "Think harder.");
  EXPECT_EQ(build_variant_entries(*opus).front().id, "low");  // no Off row

  // Merge patch: null removes an inherited variant, disabled hides one, and
  // the variants object wins over a legacy effort list.
  const auto* haiku = FindModel(providers.front().models, "claude-haiku-5-5");
  ASSERT_NE(haiku, nullptr);
  EXPECT_THAT(haiku->reasoning_efforts, testing::ElementsAre("low", "high", "max"));
  EXPECT_EQ(haiku->thinking_type, "adaptive");
  EXPECT_EQ(haiku->thinking_display, "omitted");
  EXPECT_EQ(ProviderTransform::find_variant(*haiku, "ultra"), nullptr);
}

TEST(TuiConfigTest, TopLevelModelDefaultsAndOutputBudget) {
  // Top-level model_defaults < provider model_defaults < model (merge patch).
  // The request budget is max_tokens capped by limit.output; nothing is
  // capped in code.
  ScopedConfig config(R"({
      "model_defaults": {"max_tokens": 32000, "limit": {"context": 200000}},
      "provider": {
        "opencode": {
          "model_defaults": {"max_tokens": 16000},
          "models": {
            "a": {"tool_call": true, "limit": {"context": 1000000, "output": 8192}},
            "b": {"tool_call": true, "max_tokens": null},
            "c": {"tool_call": true, "max_tokens": 64000, "limit": {"output": 128000}}
          }
        },
        "openrouter": {
          "models": {
            "d": {"tool_call": true},
            "e": {"tool_call": true, "limit": {"output": 4096}}
          }
        }
      }
    })");
  const auto providers = config.load();
  const auto* zen = FindProvider(providers, "opencode");
  const auto* openrouter = FindProvider(providers, "openrouter");
  ASSERT_NE(zen, nullptr);
  ASSERT_NE(openrouter, nullptr);

  const auto* a = FindModel(zen->models, "a");
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->max_tokens, 16000);
  EXPECT_EQ(a->context_window, 1000000);
  EXPECT_EQ(ProviderTransform::max_output_tokens(*a), std::optional<int>(8192));

  const auto* b = FindModel(zen->models, "b");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->max_tokens, 0);  // null removed the inherited default
  EXPECT_EQ(b->context_window, 200000);
  EXPECT_EQ(ProviderTransform::max_output_tokens(*b), std::nullopt);

  const auto* c = FindModel(zen->models, "c");
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->context_window, 200000);  // limit objects merge key by key
  EXPECT_EQ(c->output_limit, 128000);
  EXPECT_EQ(ProviderTransform::max_output_tokens(*c), std::optional<int>(64000));

  const auto* d = FindModel(openrouter->models, "d");
  ASSERT_NE(d, nullptr);
  EXPECT_EQ(ProviderTransform::max_output_tokens(*d), std::optional<int>(32000));
  const auto* e = FindModel(openrouter->models, "e");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(ProviderTransform::max_output_tokens(*e), std::optional<int>(4096));
}

TEST(TuiConfigTest, ContextWindowIsNeverGuessedFromModelName) {
  // Names that used to map to 2M / 1M / 200k windows stay unknown (0) until
  // opencode.json sets limit.context.
  ScopedConfig config(R"({
      "provider": {
        "opencode": {
          "models": {
            "gemini-3.1-pro": {"tool_call": true},
            "deepseek-v4-1m": {"tool_call": true},
            "claude-opus-5-5": {"tool_call": true, "limit": {"context": 1000000}}
          }
        }
      }
    })");
  const auto providers = config.load();
  ASSERT_EQ(providers.size(), 1u);
  const auto& models = providers.front().models;
  ASSERT_NE(FindModel(models, "gemini-3.1-pro"), nullptr);
  EXPECT_EQ(FindModel(models, "gemini-3.1-pro")->context_window, 0);
  EXPECT_EQ(FindModel(models, "deepseek-v4-1m")->context_window, 0);
  EXPECT_EQ(FindModel(models, "claude-opus-5-5")->context_window, 1000000);
}

TEST(TuiConfigTest, MistypedVariantFieldsDoNotAbortConfig) {
  ScopedConfig config(R"({
      "provider": {
        "anthropic": {
          "models": {
            "claude-sonnet-5-5": {
              "reasoning": true,
              "variants": {"high": {"max_tokens": "lots", "effort": 7}},
              "thinking": {"type": 1, "allow_off": "no"},
              "cost": {"input": "cheap", "cache_read": 0.2}
            }
          }
        }
      }
    })");
  const auto providers = config.load();
  ASSERT_EQ(providers.size(), 1u);
  const auto* model = FindModel(providers.front().models, "claude-sonnet-5-5");
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->variants.size(), 1u);
  EXPECT_EQ(model->variants[0].max_tokens, 0);
  EXPECT_EQ(model->variants[0].effort, "");
  EXPECT_EQ(model->thinking_type, "");
  EXPECT_TRUE(model->thinking_allow_off);
  EXPECT_DOUBLE_EQ(model->input_cost, 0.0);
  EXPECT_DOUBLE_EQ(model->cache_read_cost, 0.2);
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
    const std::string expected = gemini->reasoning_default.empty() ? "low" : gemini->reasoning_default;
    EXPECT_EQ(ProviderTransform::default_variant(*gemini), expected);
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

TEST(TuiConfigTest, FormatProviderCatalogForPrompt) {
  std::vector<ProviderInfo> providers;
  ProviderInfo p1;
  p1.id = "openrouter";
  p1.name = "OpenRouter";
  ModelInfo m1;
  m1.id = "deepseek/deepseek-v4-flash-0731";
  m1.name = "DeepSeek V4 Flash";
  m1.reasoning = true;
  p1.models.push_back(m1);

  ProviderInfo p2;
  p2.id = "antigravity";
  p2.name = "Antigravity";
  ModelInfo m2;
  m2.id = "gemini-3.8-flash";
  m2.name = "Gemini 3.8 Flash";
  m2.reasoning = false;
  p2.models.push_back(m2);

  providers.push_back(p1);
  providers.push_back(p2);

  std::string catalog_md = format_provider_catalog_for_prompt(providers);
  EXPECT_THAT(catalog_md, testing::HasSubstr("### Available Providers & Models"));
  EXPECT_THAT(catalog_md, testing::HasSubstr("**openrouter** (OpenRouter)"));
  EXPECT_THAT(catalog_md, testing::HasSubstr("`deepseek/deepseek-v4-flash-0731`"));
  EXPECT_THAT(catalog_md, testing::HasSubstr("[reasoning]"));
  EXPECT_THAT(catalog_md, testing::HasSubstr("**antigravity** (Antigravity)"));
  EXPECT_THAT(catalog_md, testing::HasSubstr("`gemini-3.8-flash`"));
  EXPECT_THAT(catalog_md, testing::HasSubstr("model: \"<provider>:<model_id>\""));
}

TEST(TuiConfigTest, LoadsEveryConfiguredProvider) {
  ScopedConfig config(R"({
      "provider": {
        "opencode": {"name": "OpenCode", "models": {"m1": {}}},
        "openrouter": {"name": "OpenRouter", "models": {"m2": {}}},
        "cursor": {"name": "Cursor", "models": {"m3": {}}},
        "antigravity": {"name": "Antigravity", "models": {"m4": {}}},
        "openai": {"name": "OpenAI", "models": {"m5": {}}},
        "anthropic": {"name": "Anthropic", "models": {"m6": {}}},
        "qpilot": {"name": "QPilot", "models": {"m7": {}}},
        "qgenie": {"name": "QGenie", "models": {"m8": {}}},
        "unknown_vendor": {"name": "Unknown", "models": {"m9": {}}}
      }
    })");

  const auto providers = config.load();
  // Every provider in the config is loaded: the app is cross-provider.
  ASSERT_EQ(providers.size(), 9u);
  EXPECT_NE(FindProvider(providers, "opencode"), nullptr);
  EXPECT_NE(FindProvider(providers, "openrouter"), nullptr);
  EXPECT_NE(FindProvider(providers, "cursor"), nullptr);
  EXPECT_NE(FindProvider(providers, "antigravity"), nullptr);
  EXPECT_NE(FindProvider(providers, "openai"), nullptr);
  EXPECT_NE(FindProvider(providers, "anthropic"), nullptr);
  EXPECT_NE(FindProvider(providers, "qpilot"), nullptr);
  EXPECT_NE(FindProvider(providers, "qgenie"), nullptr);
  EXPECT_NE(FindProvider(providers, "unknown_vendor"), nullptr);
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
