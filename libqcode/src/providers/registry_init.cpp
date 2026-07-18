#include <qcode/providers/provider_registry_init.h>

#include <qcode/providers/antigravity.h>
#include <qcode/providers/cursor.h>
#include <qcode/config/config.h>
#include <qcode/providers/registry.h>
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
        "antigravity", [](const ProviderOptions& options) {
          std::string token = ai::tui::get_antigravity_token();
          if (token.empty()) {
            return ClientResolution::fail("Antigravity token failed.");
          }
          ai::antigravity::Options client_options;
          client_options.base_url =
              options.base_url.empty()
                  ? "https://daily-cloudcode-pa.sandbox.googleapis.com/"
                    "v1internal"
                  : options.base_url;
          client_options.project_id = options.project_id;
          return ClientResolution{
              ai::antigravity::create_client(token, client_options)};
        });

    // Cursor auth (access token) lives in the TUI (reads
    // ~/.config/cursor/auth.json), so it is registered here rather than in the
    // core registry -- mirroring the Antigravity pattern.
    ProviderRegistry::instance().register_provider(
        "cursor", [](const ProviderOptions& options) {
          std::string token = options.api_key.empty()
                                  ? ai::tui::get_cursor_access_token()
                                  : options.api_key;
          if (token.empty()) {
            return ClientResolution::fail("Cursor access token failed.");
          }
          std::string aiserver = "https://api2.cursor.sh";
          std::string agent =
              options.base_url.empty()
                  ? "https://agentn.global.api5.cursor.sh"
                  : options.base_url;
          ai::cursor::Options cursor_options;
          cursor_options.aiserver_base_url = aiserver;
          cursor_options.agent_base_url = agent;
          return ClientResolution{
              ai::cursor::create_client(token, cursor_options)};
        });
  });
}

}  // namespace providers
}  // namespace ai
