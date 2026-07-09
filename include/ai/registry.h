#pragma once

#ifndef AI_SDK_HAS_REGISTRY
#error \
    "Provider registry not available. Link with ai::registry or ai::sdk to use provider dispatch."
#endif

#include "ai/types/client.h"

#include <functional>
#include <map>
#include <string>

namespace ai {
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

using ProviderResolver =
    std::function<ClientResolution(const std::string& api_url)>;

// Central registry mapping a provider id to a factory that resolves a Client,
// performing any provider-specific auth (reading an API-key env var, fetching a
// token, ...). Replaces the if/else dispatch previously duplicated in
// chat.cpp / chat_bus.cpp.
class ProviderRegistry {
 public:
  static ProviderRegistry& instance();

  // Register (or override) the resolver for a provider id.
  void register_provider(const std::string& id, ProviderResolver resolver);

  // Resolve a provider by id. Unknown ids fall back to the "opencode" generic
  // resolver. Returns ClientResolution::fail(...) if nothing can handle it.
  ClientResolution resolve(const std::string& id,
                           const std::string& api_url) const;

 private:
  std::map<std::string, ProviderResolver> resolvers_;
};

// Register the core providers that need no UI-specific auth: openai, opencode
// (generic fallback), openrouter, qpilot, qgenie.
void register_core_providers();

}  // namespace providers
}  // namespace ai
