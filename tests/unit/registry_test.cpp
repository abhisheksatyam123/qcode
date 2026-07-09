#include "ai/registry.h"

#include <cstdlib>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
}  // namespace providers
}  // namespace ai
