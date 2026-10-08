#include <qcode/providers/registry.h>
#include <qcode/config/config.h>
#include <qcode/core/logger.h>

#include <qcode/providers/anthropic.h>
#include <qcode/providers/openai.h>
#include <qcode/providers/provider_profile.h>

#include <cstdlib>
#include <string>

namespace qcode {
namespace providers {
namespace {

std::string env_value(const char* name) {
  const char* value = std::getenv(name);
  return (value && *value) ? std::string{value} : std::string{};
}

std::string first_key(const std::string& configured, const char* env_name) {
  return !configured.empty() ? configured : env_value(env_name);
}

bool allows_keyless_zen(const std::string& base_url) {
  return base_url.empty() ||
         base_url.find("opencode.ai/zen") != std::string::npos;
}

openai::CompatibleOptions to_openai(const ProviderOptions& options,
                                    std::string default_base) {
  openai::CompatibleOptions compatible;
  compatible.base_url =
      options.base_url.empty() ? std::move(default_base) : options.base_url;
  compatible.protocol = options.protocol.empty()
                            ? wire_protocol_id(WireProtocol::kChatCompletions)
                            : options.protocol;
  compatible.completions_path = options.completions_path;
  compatible.headers = options.headers;
  return compatible;
}

ClientResolution resolve_openrouter(const ProviderOptions& options) {
  const auto api_key = first_key(options.api_key, "OPENROUTER_API_KEY");
  if (api_key.empty())
    return ClientResolution::fail("OPENROUTER_API_KEY not set.");
  return ClientResolution{openai::create_client(
      api_key, to_openai(options, "https://openrouter.ai/api"))};
}

ClientResolution resolve_opencode(const ProviderOptions& options) {
  auto api_key = first_key(options.api_key, "OPENCODE_API_KEY");
  if (api_key.empty()) {
    if (!allows_keyless_zen(options.base_url)) {
      return ClientResolution::fail(
          "OpenCode Zen API key not configured (set options.apiKey or "
          "OPENCODE_API_KEY).");
    }
    api_key = "__EMPTY__";
  }
  const auto base_url = options.base_url.empty() ? "https://opencode.ai/zen/v1"
                                                 : options.base_url;
  const auto protocol = parse_wire_protocol(options.protocol);
  if (protocol == WireProtocol::kMessages) {
    anthropic::CompatibleOptions compatible;
    compatible.base_url = base_url;
    compatible.completions_path = options.completions_path;
    compatible.headers = options.headers;
    if (!compatible.headers.contains("User-Agent")) {
      compatible.headers["User-Agent"] = "opencode/1.18.18";
    }
    if (!compatible.headers.contains("x-opencode-client")) {
      compatible.headers["x-opencode-client"] = "cli";
    }
    compatible.bearer_auth = true;
    return ClientResolution{anthropic::create_client(api_key, compatible)};
  }
  auto compatible = to_openai(options, base_url);
  if (!compatible.headers.contains("User-Agent")) {
    compatible.headers["User-Agent"] = "opencode/1.18.18";
  }
  if (!compatible.headers.contains("x-opencode-client")) {
    compatible.headers["x-opencode-client"] = "cli";
  }
  return ClientResolution{openai::create_client(api_key, compatible)};
}

ClientResolution resolve_anthropic(const ProviderOptions& options) {
  std::string api_key = options.api_key;
  if (api_key.empty()) {
    api_key = env_value("ANTHROPIC_API_KEY");
  }
  bool is_oauth = false;
  if (api_key.empty()) {
    api_key = qcode::get_anthropic_token();
    if (!api_key.empty()) {
      is_oauth = true;
    }
  } else if (api_key.starts_with("sk-ant-oat")) {
    is_oauth = true;
  }

  if (api_key.empty()) {
    return ClientResolution::fail(
        "Anthropic API key or Claude Code login not found (set ANTHROPIC_API_KEY "
        "or login with Claude Code ~/.claude/.credentials.json).");
  }

  anthropic::CompatibleOptions compatible;
  compatible.base_url =
      options.base_url.empty() ? "https://api.anthropic.com" : options.base_url;
  compatible.completions_path = options.completions_path;
  compatible.headers = options.headers;
  compatible.bearer_auth = is_oauth;
  compatible.is_oauth = is_oauth;

  return ClientResolution{anthropic::create_client(api_key, compatible)};
}

ClientResolution resolve_openai(const ProviderOptions& options) {
  const auto api_key = first_key(options.api_key, "OPENAI_API_KEY");
  if (api_key.empty() && (options.base_url.empty() || !allows_keyless_zen(options.base_url))) {
    return ClientResolution::fail("OPENAI_API_KEY not set.");
  }
  const auto base_url =
      options.base_url.empty() ? "https://api.openai.com/v1" : options.base_url;
  return ClientResolution{openai::create_client(
      api_key.empty() ? "__EMPTY__" : api_key,
      to_openai(options, base_url))};
}

}  // namespace

ProviderRegistry& ProviderRegistry::instance() {
  static ProviderRegistry inst;
  return inst;
}

void ProviderRegistry::register_provider(const std::string& id,
                                         ProviderResolver resolver) {
  resolvers_[id] = std::move(resolver);
}

ClientResolution ProviderRegistry::resolve(
    const std::string& id, const ProviderOptions& options) const {
  auto it = resolvers_.find(id);
  if (it != resolvers_.end()) {
    return it->second(options);
  }
  if (!options.base_url.empty()) {
    if (options.protocol == "messages") {
      return resolve_anthropic(options);
    }
    return resolve_openai(options);
  }
  return ClientResolution::fail(
      "Unsupported provider: " + id +
      ". Supported providers are: cursor, opencode, openrouter, antigravity, anthropic, openai.");
}

ClientResolution ProviderRegistry::resolve(const std::string& id,
                                           const std::string& api_url) const {
  ProviderOptions options;
  options.base_url = api_url;
  return resolve(id, options);
}

void register_core_providers() {
  auto& reg = ProviderRegistry::instance();
  reg.register_provider("opencode", resolve_opencode);
  reg.register_provider("openrouter", resolve_openrouter);
  reg.register_provider("anthropic", resolve_anthropic);
  reg.register_provider("openai", resolve_openai);
}

}  // namespace providers
}  // namespace qcode
