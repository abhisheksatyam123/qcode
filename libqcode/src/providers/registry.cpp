#include <qcode/providers/registry.h>
#include <qcode/logger/logger.h>

#include <qcode/providers/openai.h>

#include <cstdlib>

namespace qcode {
namespace providers {

ProviderRegistry& ProviderRegistry::instance() {
  static ProviderRegistry inst;
  return inst;
}

void ProviderRegistry::register_provider(const std::string& id,
                                         ProviderResolver resolver) {
  resolvers_[id] = std::move(resolver);
}

ClientResolution ProviderRegistry::resolve(const std::string& id,
                                            const ProviderOptions& options) const {
  auto it = resolvers_.find(id);
  if (it == resolvers_.end()) {
    if (options.base_url.empty()) {
      return ClientResolution::fail("Unknown provider: " + id);
    }
    qcode::openai::CompatibleOptions compatible;
    compatible.base_url = options.base_url;
    compatible.protocol = options.protocol;
    compatible.headers = options.headers;
    if (options.api_key.empty()) {
      return ClientResolution::fail("No API key configured for provider: " + id);
    }
    return ClientResolution{
        qcode::openai::create_client(options.api_key, compatible)};
  }
  return it->second(options);
}

ClientResolution ProviderRegistry::resolve(const std::string& id,
                                            const std::string& api_url) const {
  ProviderOptions options;
  options.base_url = api_url;
  return resolve(id, options);
}

void register_core_providers() {
  auto& reg = ProviderRegistry::instance();

  auto generic = [](const ProviderOptions& options) {
    const char* env_key = std::getenv("OPENCODE_API_KEY");
    auto api_key =
        !options.api_key.empty() ? options.api_key
                                 : (env_key ? std::string(env_key) : "");
    if (api_key.empty()) {
      if (options.base_url.empty() || options.base_url.find("opencode.ai/zen") != std::string::npos) {
        api_key = "__EMPTY__";
      } else {
        return ClientResolution::fail(
            "OpenCode Zen API key not configured (set options.apiKey or "
            "OPENCODE_API_KEY).");
      }
    }
    qcode::openai::CompatibleOptions compatible;
    compatible.base_url = options.base_url.empty()
                              ? "https://opencode.ai/zen/v1"
                              : options.base_url;
    compatible.protocol = options.protocol;
    compatible.headers = options.headers;
    compatible.headers["User-Agent"] = "opencode/1.18.18";
    compatible.headers["x-opencode-client"] = "cli";
    return ClientResolution{qcode::openai::create_client(api_key, compatible)};
  };
  reg.register_provider("openai", [](const ProviderOptions& options) {
    const char* key = std::getenv("OPENAI_API_KEY");
    const auto api_key = !options.api_key.empty()
                             ? options.api_key
                             : (key ? std::string(key) : "");
    if (api_key.empty())
      return ClientResolution::fail("OPENAI_API_KEY not set.");
    qcode::openai::CompatibleOptions compatible;
    compatible.base_url = options.base_url.empty()
                              ? "https://api.openai.com"
                              : options.base_url;
    compatible.protocol = options.protocol;
    compatible.headers = options.headers;
    return ClientResolution{qcode::openai::create_client(api_key, compatible)};
  });
  reg.register_provider("opencode", generic);

  reg.register_provider("openrouter", [](const ProviderOptions& options) {
    const char* key = std::getenv("OPENROUTER_API_KEY");
    const auto api_key = !options.api_key.empty()
                             ? options.api_key
                             : (key ? std::string(key) : "");
    if (api_key.empty())
      return ClientResolution::fail("OPENROUTER_API_KEY not set.");
    qcode::openai::CompatibleOptions compatible;
    compatible.base_url = options.base_url.empty()
                              ? "https://openrouter.ai/api"
                              : options.base_url;
    compatible.protocol = options.protocol;
    compatible.headers = options.headers;
    return ClientResolution{qcode::openai::create_client(api_key, compatible)};
  });

  reg.register_provider("qpilot", [](const ProviderOptions&) {
    char* key = std::getenv("QPILOT_API_KEY");
    if (!key) return ClientResolution::fail("QPILOT_API_KEY not set.");
    return ClientResolution{
        qcode::openai::create_client(key, "https://qpilot-api.qualcomm.com")};
  });

  reg.register_provider("qgenie", [](const ProviderOptions&) {
    char* key = std::getenv("QPILOT_API_KEY");
    if (!key) return ClientResolution::fail("QPILOT_API_KEY not set.");
    return ClientResolution{
        qcode::openai::create_client(key, "https://qgenie-api.qualcomm.com")};
  });
}

}  // namespace providers
}  // namespace qcode
