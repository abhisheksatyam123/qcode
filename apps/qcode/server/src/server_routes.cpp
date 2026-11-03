#include "server_routes.h"
#include "routes/routes_internal.h"

#include "http_utils.h"

#include <nlohmann/json.hpp>

#include <qcode/core/in_process_bus.h>
#include <qcode/core/logger.h>
#include <qcode/session/session_store.h>
#include <qcode/generation/generation_service.h>

namespace qcode {
namespace server {

std::unique_ptr<qcode::GenerationService> g_backend;

// Session-scoped paths answer a malformed id with 400 and an unknown one with
// 404, before any handler runs. /session/last and /session/cancel are literal
// routes, not ids.
bool rejected_session_path(const httplib::Request& req, httplib::Response& res) {
  for (const std::string prefix : {std::string("/session/"), std::string("/api/session/")}) {
    if (req.path.rfind(prefix, 0) != 0) continue;
    const std::string rest = req.path.substr(prefix.size());
    const auto slash = rest.find('/');
    const std::string raw_id = rest.substr(0, slash);
    if (raw_id.empty()) return false;
    if (slash == std::string::npos && (raw_id == "last" || raw_id == "cancel")) return false;
    const std::string id = url_decode(raw_id);
    if (!qcode::session::is_valid_session_id(id)) {
      send_error(res, 400, "invalid session_id");
      return true;
    }
    if (!qcode::session::session_exists(id)) {
      send_error(res, 404, "session not found");
      return true;
    }
    return false;
  }
  return false;
}

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
    if (rejected_session_path(req, res)) return httplib::Server::HandlerResponse::Handled;
    return httplib::Server::HandlerResponse::Unhandled;
  });

  server.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "*");
  });

  // An exception that reaches here is a server bug: log it and answer 500.
  // Bad request bodies and unknown sessions are rejected before handlers run.
  server.set_exception_handler([](const httplib::Request& req, httplib::Response& res,
                                  std::exception_ptr ep) {
    std::string what = "unknown exception";
    try {
      std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      what = e.what();
    } catch (...) {
    }
    LOG_ERROR("Unhandled exception for {} {}: {}", req.method, req.path, what);
    qcode::server::send_error(res, 500, "internal error");
  });

  register_system_routes(server, providers, options);
  register_session_routes(server, bus, providers, options);
  register_fs_routes(server);
}

}  // namespace server
}  // namespace qcode
