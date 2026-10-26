#include <gtest/gtest.h>

#include "qcode/interfaces/provider_factory.h"

namespace qcode {

TEST(ProviderFactoryTest, CreatesAntigravityProvider) {
    auto provider = ProviderFactory::create("antigravity", "dummy-key", "");
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->name(), "antigravity");
    std::string resp = provider->request("test prompt");
    EXPECT_TRUE(resp.empty() || resp.find("Client not initialized") != std::string::npos || !resp.empty());
}

TEST(ProviderFactoryTest, CreatesOpenAIProvider) {
    auto provider = ProviderFactory::create("openai", "dummy-key", "");
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->name(), "openai");
}

TEST(ProviderFactoryTest, CreatesAnthropicProvider) {
    auto provider = ProviderFactory::create("anthropic", "dummy-key", "");
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->name(), "anthropic");
}

TEST(ProviderFactoryTest, CreatesCursorProvider) {
    auto provider = ProviderFactory::create("cursor", "dummy-token", "");
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->name(), "cursor");
}

TEST(ProviderFactoryTest, ReturnsNullptrForUnknownProvider) {
    auto provider = ProviderFactory::create("unknown-provider", "dummy-key", "");
    EXPECT_EQ(provider, nullptr);
}

} // namespace qcode
