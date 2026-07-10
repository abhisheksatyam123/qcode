#include "ai/tui/provider_registry_init.h"

#include "ai/antigravity.h"
#include "ai/tui/config.h"
#include "ai/registry.h"
#include <mutex>

namespace ai {
namespace providers {

namespace {
std::once_flag g_tui_providers_registered;
}  // namespace

void register_tui_providers() {
  // Register exactly once, even if called from multiple generations or threads.
  std::call_once(g_tui_providers_registered, [] {
    register_core_providers();
    // Antigravity auth lives in the TUI (keyring + oauth token refresh), so it is
    // registered here rather than in the core registry.
    ProviderRegistry::instance().register_provider(
        "antigravity", [](const std::string&) {
          std::string token = ai::tui::get_antigravity_token();
          if (token.empty()) {
            return ClientResolution::fail("Antigravity token failed.");
          }
          return ClientResolution{ai::antigravity::create_client(
              token, "https://daily-cloudcode-pa.googleapis.com/v1internal")};
        });
  });
}

}  // namespace providers
}  // namespace ai
