#include <qcode/providers/registry.h>

#include <cstdlib>
#include <algorithm>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <qcode/core/generate_options.h>
#include <qcode/config/config.h>

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
  EXPECT_FALSE(res.error.empty());
}

TEST(RegistryTest, OpenRouterSucceedsWithKey) {
  ScopedEnv key("OPENROUTER_API_KEY");
  key.set("dummy-token");
  register_core_providers();
  ClientResolution res =
      ProviderRegistry::instance().resolve("openrouter", "");
  EXPECT_TRUE(res.ok());
}

TEST(RegistryTest, RemovedProvidersRejected) {
  register_core_providers();
  // openai, anthropic, qpilot, qgenie are removed in favor of the 4 supported providers
  EXPECT_FALSE(ProviderRegistry::instance().resolve("openai", "").ok());
  EXPECT_FALSE(ProviderRegistry::instance().resolve("anthropic", "").ok());
  EXPECT_FALSE(ProviderRegistry::instance().resolve("qpilot", "").ok());
  EXPECT_FALSE(ProviderRegistry::instance().resolve("qgenie", "").ok());
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
