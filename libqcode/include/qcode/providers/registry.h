#pragma once

#ifndef QCODE_HAS_REGISTRY
#error \
    "Provider registry not available. Link with qcode::registry or qcode::sdk to use provider dispatch."
#endif

#include <qcode/core/client.h>

#include <functional>
#include <map>
#include <string>

namespace qcode {
namespace providers {

// Result of resolving a provider: either a ready Client, or a non-empty error
// describing why resolution failed (missing key, token fetch failure, etc.).
struct ClientResolution {
  Client client;
  std::string error;

  bool ok() const { return error.empty(); }
  static ClientResolution fail(std::string msg) {
    return ClientResolution{Client{}, std::move(msg)};
  }
};

struct ProviderOptions {
  std::string base_url;
  std::string api_key;
  std::map<std::string, std::string> headers;
  std::string protocol = "chat_completions";
  std::string project_id;
};

using ProviderResolver =
    std::function<ClientResolution(const ProviderOptions& options)>;

// Central registry mapping a provider id to a factory that resolves a Client,
// performing any provider-specific auth (reading an API-key env var, fetching a
// token, ...). Replaces the if/else dispatch previously duplicated in
// chat.cpp / chat_bus.cpp.
class ProviderRegistry {
 public:
  static ProviderRegistry& instance();

  // Register (or override) the resolver for a provider id.
  void register_provider(const std::string& id, ProviderResolver resolver);

  // Resolve a configured provider by id. Unknown providers fail explicitly.
  ClientResolution resolve(const std::string& id,
                           const ProviderOptions& options) const;

  // Compatibility overload for callers that only supply a base URL.
  ClientResolution resolve(const std::string& id,
                           const std::string& api_url) const;

 private:
  std::map<std::string, ProviderResolver> resolvers_;
};

// Register the core providers that need no UI-specific auth: openai, opencode
// (generic fallback), openrouter, qpilot, qgenie.
void register_core_providers();

}  // namespace providers
}  // namespace qcode
