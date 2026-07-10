#include "ai/registry.h"

#include <cstdlib>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "ai/types/generate_options.h"

namespace ai {
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

TEST(RegistryTest, CoreProvidersRegisterAndResolveOpenAI) {
  // The "openai" resolver requires OPENAI_API_KEY (set at resolve time); provide
  // a dummy key so resolution succeeds, mirroring OpenRouterSucceedsWithKey.
  ScopedEnv key("OPENAI_API_KEY");
  key.set("dummy-token");
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("openai", "");
  EXPECT_TRUE(res.ok());
  EXPECT_TRUE(res.error.empty());
}

TEST(RegistryTest, GenericProviderResolves) {
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("opencode", "");
  EXPECT_TRUE(res.ok());
}

TEST(RegistryTest, UnknownProviderFallsBackToGeneric) {
  register_core_providers();
  // Unknown ids fall back to the "opencode" generic resolver, which succeeds.
  ClientResolution res =
      ProviderRegistry::instance().resolve("does-not-exist", "");
  EXPECT_TRUE(res.ok());
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

TEST(RegistryTest, QpilotRequiresKey) {
  ScopedEnv key("QPILOT_API_KEY");
  key.unset();
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("qpilot", "");
  EXPECT_FALSE(res.ok());
}

TEST(RegistryTest, CustomProviderCanBeRegistered) {
  register_core_providers();
  bool called = false;
  ProviderRegistry::instance().register_provider(
      "custom", [&called](const std::string&) {
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
  // Backward-compatible: opencode is the generic OpenAI-compatible fallback and
  // historically used a placeholder key for local OpenCode Zen proxies.
  register_core_providers();
  ClientResolution res = ProviderRegistry::instance().resolve("opencode", "");
  EXPECT_TRUE(res.ok());
  EXPECT_TRUE(res.error.empty());
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
  if (!ort) GTEST_SKIP() << "OPENROUTER_API_KEY not set";
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

}  // namespace providers
}  // namespace ai
