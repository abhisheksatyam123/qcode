#include "ai/registry.h"
#include <ai/logger.h>

#include "ai/openai.h"

#include <cstdlib>

namespace ai {
namespace providers {
namespace {

constexpr const char* kGenericId = "opencode";

}  // namespace

ProviderRegistry& ProviderRegistry::instance() {
  static ProviderRegistry inst;
  return inst;
}

void ProviderRegistry::register_provider(const std::string& id,
                                         ProviderResolver resolver) {
  resolvers_[id] = std::move(resolver);
}

ClientResolution ProviderRegistry::resolve(const std::string& id,
                                            const std::string& api_url) const {
  auto it = resolvers_.find(id);
  if (it == resolvers_.end()) {
    if (id != kGenericId) {
      LOG_WARN("ProviderRegistry: unknown provider '{}'; falling back to "
               "generic '{}' resolver", id, kGenericId);
    }
    it = resolvers_.find(kGenericId);
  }
  if (it == resolvers_.end()) {
    return ClientResolution::fail("Unknown provider: " + id);
  }
  return it->second(api_url);
}

void register_core_providers() {
  auto& reg = ProviderRegistry::instance();

  auto generic = [](const std::string& api_url) {
    // The generic "opencode" provider is OpenAI-compatible. It defaults to the
    // OpenCode Zen endpoint with a placeholder key (local Zen proxies handle
    // auth themselves), but honors OPENCODE_API_KEY when set so it can target
    // any OpenAI-compatible endpoint that requires authentication.
    const char* key = std::getenv("OPENCODE_API_KEY");
    std::string api_key = key ? std::string(key) : "unused";
    std::string url = api_url.empty() ? "https://opencode.ai/zen/v1" : api_url;
    return ClientResolution{ai::openai::create_client(api_key, url)};
  };
  reg.register_provider("openai", [](const std::string&) {
    char* key = std::getenv("OPENAI_API_KEY");
    if (!key) return ClientResolution::fail("OPENAI_API_KEY not set.");
    return ClientResolution{
        ai::openai::create_client(key, "https://api.openai.com/v1")};
  });
  reg.register_provider("opencode", generic);

  reg.register_provider("openrouter", [](const std::string&) {
    char* key = std::getenv("OPENROUTER_API_KEY");
    if (!key) return ClientResolution::fail("OPENROUTER_API_KEY not set.");
    return ClientResolution{
        ai::openai::create_client(key, "https://openrouter.ai/api")};
  });

  reg.register_provider("qpilot", [](const std::string&) {
    char* key = std::getenv("QPILOT_API_KEY");
    if (!key) return ClientResolution::fail("QPILOT_API_KEY not set.");
    return ClientResolution{
        ai::openai::create_client(key, "https://qpilot-api.qualcomm.com")};
  });

  reg.register_provider("qgenie", [](const std::string&) {
    char* key = std::getenv("QPILOT_API_KEY");
    if (!key) return ClientResolution::fail("QPILOT_API_KEY not set.");
    return ClientResolution{
        ai::openai::create_client(key, "https://qgenie-api.qualcomm.com")};
  });
}

}  // namespace providers
}  // namespace ai
