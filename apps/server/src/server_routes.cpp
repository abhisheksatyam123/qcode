#include "server_routes.h"
#include "routes/routes_internal.h"

#include <qcode/core/in_process_bus.h>
#include <qcode/generation/generation_service.h>

namespace qcode {
namespace server {

std::unique_ptr<qcode::GenerationService> g_backend;

void setup_server_routes(
    httplib::Server& server,
    std::shared_ptr<qcode::bus::BusRuntime> bus,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers,
    const ServerSetupOptions& options) {
  if (bus && providers) {
    g_backend = std::make_unique<qcode::GenerationService>(*bus, *providers);
  }

  // Enable CORS for LAN and browser clients
  server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
    if (req.method == "OPTIONS") {
      res.set_header("Access-Control-Allow-Origin", "*");
      res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, HEAD");
      res.set_header("Access-Control-Allow-Headers", "*");
      res.set_header("Access-Control-Max-Age", "86400");
      res.status = 204;
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  server.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "*");
  });

  register_system_routes(server, providers, options);
  register_session_routes(server, bus, providers, options);
  register_terminal_routes(server);
  register_fs_routes(server);
  register_study_routes(server, providers);
  register_vision_routes(server, providers);
}

}  // namespace server
}  // namespace qcode
