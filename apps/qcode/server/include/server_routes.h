#pragma once

#include <qcode/core/in_process_bus.h>
#include <qcode/config/config.h>
#include <qcode/generation/generation_service.h>

#include <httplib.h>

#include <memory>
#include <string>
#include <vector>

namespace qcode {
namespace server {

// Who may reach the server. The default is loopback only: a Host header that
// is not a loopback name is refused, which also blocks DNS rebinding.
struct SecurityPolicy {
  bool loopback_bind = true;
  // When non-empty, every request except the health probes must carry
  // "Authorization: Bearer <token>", or the session cookie. Required when binding
  // beyond loopback.
  std::string token;
  // Value of the session cookie that the token is exchanged for. Random per
  // start, so the token itself never sits in a browser cookie.
  std::string cookie_value;
};

struct ServerSetupOptions {
  std::string webui_dir;
  std::string default_workspace;
  SecurityPolicy security;
};

// Owns the GenerationService used by HTTP generation routes.
extern std::unique_ptr<qcode::GenerationService> g_backend;

void setup_server_routes(
    httplib::Server& server,
    std::shared_ptr<qcode::bus::BusRuntime> bus,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers,
    const ServerSetupOptions& options);

void shutdown_active_sessions();

}  // namespace server
}  // namespace qcode
