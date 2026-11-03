#include <qcode/providers/registry.h>

#include <cstdlib>
#include <algorithm>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <qcode/core/generate_options.h>
#include <qcode/config/config.h>
#include <qcode/providers/provider_profile.h>

namespace qcode {
namespace providers {
namespace {

// Scoped env-var guard so key-dependent resolver tests are deterministic and
// don't leak state into other tests in the same process.
class ScopedEnv {
 public:
  explicit ScopedEnv(const char* name) : name_(name) {
    if (const char* existing = std::getenv(name)) {
      had_ = true;
      saved_ = existing;
    }
  }
  ~ScopedEnv() {
    if (had_)
      ::setenv(name_, saved_.c_str(), 1);
    else
      ::unsetenv(name_);
  }
  void set(const std::string& v) { ::setenv(name_, v.c_str(), 1); }
  void unset() { ::unsetenv(name_); }

 private:
  const char* name_;
  bool had_ = false;
  std::string saved_;
};

TEST(RegistryTest, CoreProvidersRegisterAndResolveOpenRouter) {
  ScopedEnv key("OPENROUTER_API_KEY");
  key.set("dummy-token");
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("openrouter", "");
  EXPECT_TRUE(res.ok());
  EXPECT_TRUE(res.error.empty());
}

TEST(RegistryTest, GenericProviderResolves) {
  ScopedEnv key("OPENCODE_API_KEY");
  key.set("dummy-token");
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("opencode", "");
  EXPECT_TRUE(res.ok());
}

TEST(RegistryTest, UnknownProviderFailsWithoutConfiguredEndpoint) {
  register_core_providers();
  ClientResolution res =
      ProviderRegistry::instance().resolve("does-not-exist", "");
  EXPECT_FALSE(res.ok());
}

TEST(RegistryTest, OpenRouterRequiresKey) {
  ScopedEnv key("OPENROUTER_API_KEY");
  key.unset();
  register_core_providers();
  ClientResolution res =
      ProviderRegistry::instance().resolve("openrouter", "");
  EXPECT_FALSE(res.ok());
  EXPECT_NE(res.error.find("OPENROUTER_API_KEY"), std::string::npos);
  EXPECT_NE(res.error.find("opencode.json"), std::string::npos);
}

TEST(RegistryTest, OpenAIRequiresKeyAndPointsAtConfig) {
  ScopedEnv key("OPENAI_API_KEY");
  key.unset();
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("openai", "");
  EXPECT_FALSE(res.ok());
  EXPECT_NE(res.error.find("OPENAI_API_KEY"), std::string::npos);
  EXPECT_NE(res.error.find("opencode.json"), std::string::npos);
}

TEST(RegistryTest, OpenRouterSucceedsWithKey) {
  ScopedEnv key("OPENROUTER_API_KEY");
  key.set("dummy-token");
  register_core_providers();
  ClientResolution res =
      ProviderRegistry::instance().resolve("openrouter", "");
  EXPECT_TRUE(res.ok());
}

TEST(RegistryTest, UnconfiguredProvidersRejected) {
  ScopedEnv claudeCreds("CLAUDE_CREDENTIALS_FILE");
  claudeCreds.set("/tmp/nonexistent_claude_creds_test.json");
  ScopedEnv antKey("ANTHROPIC_API_KEY");
  antKey.unset();
  ScopedEnv oaiKey("OPENAI_API_KEY");
  oaiKey.unset();
  register_core_providers();
  EXPECT_FALSE(ProviderRegistry::instance().resolve("openai", "").ok());
  EXPECT_FALSE(ProviderRegistry::instance().resolve("anthropic", "").ok());
  EXPECT_FALSE(ProviderRegistry::instance().resolve("qpilot", "").ok());
  EXPECT_FALSE(ProviderRegistry::instance().resolve("qgenie", "").ok());
}

TEST(RegistryTest, OpenAIResolvesWithKey) {
  ScopedEnv key("OPENAI_API_KEY");
  key.set("sk-test-openai-key");
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("openai", "");
  EXPECT_TRUE(res.ok());
}

TEST(RegistryTest, AnthropicResolvesWithApiKey) {
  ScopedEnv key("ANTHROPIC_API_KEY");
  key.set("sk-ant-test-key");
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("anthropic", "");
  EXPECT_TRUE(res.ok());
}

TEST(RegistryTest, CustomOpenAICompatibleResolvesWithBaseUrl) {
  register_core_providers();
  ProviderOptions options;
  options.base_url = "http://localhost:11434/v1";
  options.api_key = "dummy";
  ClientResolution res = ProviderRegistry::instance().resolve("custom-ollama", options);
  EXPECT_TRUE(res.ok());
}

TEST(RegistryTest, CustomProviderCanBeRegistered) {
  register_core_providers();
  bool called = false;
  ProviderRegistry::instance().register_provider(
      "custom", [&called](const ProviderOptions&) {
        called = true;
        return ClientResolution{Client{}, std::string{}};
      });
  ClientResolution res = ProviderRegistry::instance().resolve("custom", "");
  EXPECT_TRUE(res.ok());
  EXPECT_TRUE(called);
}

TEST(RegistryTest, ResolutionFailureHelper) {
  ClientResolution fail = ClientResolution::fail("boom");
  EXPECT_FALSE(fail.ok());
  EXPECT_EQ(fail.error, "boom");
}

}  // namespace

TEST(RegistryTest, OpenCodeResolvesWithoutKey) {
  ScopedEnv key("OPENCODE_API_KEY");
  key.unset();
  register_core_providers();
  ProviderOptions options;
  options.base_url = "https://custom.opencode.ai/v1";
  ClientResolution res = ProviderRegistry::instance().resolve("opencode", options);
  EXPECT_FALSE(res.ok());
  EXPECT_FALSE(res.error.empty());
}

TEST(RegistryTest, OpenCodeResolvesWithConfiguredKey) {
  // When OPENCODE_API_KEY is set, the generic resolver picks it up (enables real
  // OpenAI-compatible endpoints that require authentication).
  ScopedEnv key("OPENCODE_API_KEY");
  key.set("dummy-token");
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("opencode", "");
  EXPECT_TRUE(res.ok());
  EXPECT_TRUE(res.error.empty());
}

TEST(RegistryTest, OpenCodeGoRequiresKey) {
  ScopedEnv go_key("OPENCODE_GO_API_KEY");
  ScopedEnv opencode_key("OPENCODE_API_KEY");
  go_key.unset();
  opencode_key.unset();
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("opencode-go", "");
  EXPECT_FALSE(res.ok());
  EXPECT_THAT(res.error, testing::HasSubstr("OpenCode Go API key"));
}

TEST(RegistryTest, OpenCodeGoResolvesWithGoKey) {
  ScopedEnv go_key("OPENCODE_GO_API_KEY");
  go_key.set("oc_sk_test");
  register_core_providers();
  ProviderOptions options;
  options.protocol = "messages";
  options.completions_path = "/messages";
  ClientResolution res =
      ProviderRegistry::instance().resolve("opencode-go", options);
  EXPECT_TRUE(res.ok()) << res.error;
}

TEST(RegistryTest, OpenCodeOpenRouterE2E) {
  // End-to-end proof that the generic "opencode" provider works against a real
  // OpenAI-compatible endpoint. Runs only when OPENROUTER_API_KEY is available.
  const char* ort = std::getenv("OPENROUTER_API_KEY");
  if (!ort || std::getenv("QCODE_RUN_LIVE_TESTS") == nullptr) {
    GTEST_SKIP() << "Live provider tests disabled";
  }
  ScopedEnv key("OPENCODE_API_KEY");
  key.set(ort);
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve(
      "opencode", "https://openrouter.ai/api");
  ASSERT_TRUE(res.ok()) << res.error;
  GenerateOptions opts;
  opts.model = "openai/gpt-4o-mini";
  opts.prompt = "Reply with exactly the word: OK";
  GenerateResult out = res.client.generate_text(opts);
  EXPECT_TRUE(out.is_success()) << out.error_message();
  EXPECT_FALSE(out.text.empty());
}

TEST(RegistryTest, OpenCodeGoE2E) {
  if (std::getenv("QCODE_RUN_LIVE_TESTS") == nullptr) {
    GTEST_SKIP() << "Live provider tests disabled";
  }
  const auto providers = qcode::load_providers_from_config();
  const auto it = std::find_if(
      providers.begin(), providers.end(),
      [](const auto& provider) { return provider.id == "opencode-go"; });
  if (it == providers.end() || it->models.empty()) {
    GTEST_SKIP() << "OpenCode Go provider/model not configured";
  }
  // One model per wire protocol: chat/completions, messages, responses and
  // Google generateContent all share the unified Go gateway.
  for (const char* model_id :
       {"glm-5.3-flash", "claude-haiku-5-5", "grok-4.7", "gemini-4-argon"}) {
    const auto model_it = std::find_if(
        it->models.begin(), it->models.end(),
        [&](const auto& model) { return model.id == model_id; });
    if (model_it == it->models.end()) continue;

    ProviderOptions options;
    options.base_url = it->api_url;
    options.api_key = it->api_key;
    options.headers = it->headers;
    options.protocol = model_it->protocol;
    prepare_provider_call(options, "opencode-go", model_it->id,
                          "opencode-go-e2e-session");
    register_core_providers();
    auto resolution =
        ProviderRegistry::instance().resolve("opencode-go", options);
    ASSERT_TRUE(resolution.ok()) << model_id << ": " << resolution.error;
    GenerateOptions request;
    request.model = model_it->id;
    request.prompt = "Reply with exactly OK";
    request.max_tokens = 256;
    const auto result = resolution.client.generate_text(request);
    EXPECT_TRUE(result.is_success())
        << model_id << ": " << result.error_message();
    EXPECT_FALSE(result.text.empty()) << model_id;
  }
}

TEST(RegistryTest, OpenCodeZenE2E) {
  if (std::getenv("QCODE_RUN_LIVE_TESTS") == nullptr) {
    GTEST_SKIP() << "Live provider tests disabled";
  }
  const auto providers = qcode::load_providers_from_config();
  const auto it = std::find_if(
      providers.begin(), providers.end(),
      [](const auto& provider) { return provider.id == "opencode"; });
  if (it == providers.end() || it->models.empty()) {
    GTEST_SKIP() << "OpenCode provider/model not configured";
  }
  ProviderOptions options;
  options.base_url = it->api_url;
  options.api_key = it->api_key;
  options.headers = it->headers;
  options.protocol = it->models.front().protocol;
  register_core_providers();
  auto resolution = ProviderRegistry::instance().resolve("opencode", options);
  ASSERT_TRUE(resolution.ok()) << resolution.error;
  GenerateOptions request;
  request.model = it->models.front().id;
  request.prompt = "Reply with exactly OK";
  const auto result = resolution.client.generate_text(request);
  EXPECT_TRUE(result.is_success()) << result.error_message();
  EXPECT_FALSE(result.text.empty());
}

}  // namespace providers
}  // namespace qcode
