#include "server_routes.h"
#include "routes/routes_internal.h"

#include "http_utils.h"

#include <nlohmann/json.hpp>
#include <string_view>

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

namespace {

// "localhost", "127.0.0.1", "[::1]", each with an optional :port.
bool is_loopback_authority(std::string_view authority) {
  std::string_view host = authority;
  if (!host.empty() && host.front() == '[') {
    const auto close = host.find(']');
    if (close == std::string_view::npos) return false;
    host = host.substr(0, close + 1);
  } else {
    const auto colon = host.rfind(':');
    if (colon != std::string_view::npos) host = host.substr(0, colon);
  }
  return host == "localhost" || host == "127.0.0.1" || host == "[::1]" || host == "::1";
}

// "scheme://authority" -> "authority", or empty when there is no scheme.
std::string_view origin_authority(std::string_view origin) {
  const auto sep = origin.find("://");
  if (sep == std::string_view::npos) return {};
  return origin.substr(sep + 3);
}

bool constant_time_equal(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  }
  return diff == 0;
}

bool is_health_probe(const httplib::Request& req) {
  return (req.method == "GET" || req.method == "HEAD") &&
         (req.path == "/health" || req.path == "/api/health");
}

}  // namespace

bool security_gate(const httplib::Request& req, httplib::Response& res,
                   const SecurityPolicy& policy) {
  // Host: on a loopback bind, only loopback names reach the API. A DNS name
  // that resolves to 127.0.0.1 (DNS rebinding) carries its own Host and fails here.
  const std::string host = req.get_header_value("Host");
  if (policy.loopback_bind && !host.empty() && !is_loopback_authority(host)) {
    send_error(res, 403, "host not allowed");
    return true;
  }

  // Origin: browsers send it on cross-site requests. Loopback origins (local
  // pages, the Vite dev server) and this server's own origin are allowed. Any
  // other origin, including "null", is refused. Allowed origins are echoed,
  // never "*".
  const std::string origin = req.get_header_value("Origin");
  if (!origin.empty()) {
    const auto authority = origin_authority(origin);
    const bool allowed = !authority.empty() &&
                         (is_loopback_authority(authority) || authority == host);
    if (!allowed) {
      send_error(res, 403, "cross-origin request refused");
      return true;
    }
    res.set_header("Access-Control-Allow-Origin", origin);
    res.set_header("Vary", "Origin");
  }

  if (req.method == "OPTIONS") {
    if (!origin.empty()) {
      res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, HEAD");
      res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
      res.set_header("Access-Control-Max-Age", "86400");
    }
    res.status = 204;
    return true;
  }

  // Token: beyond loopback every request needs it, except the health probes.
  if (!policy.token.empty() && !is_health_probe(req)) {
    const std::string auth = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    const bool ok = auth.rfind(prefix, 0) == 0 &&
                    constant_time_equal(std::string_view(auth).substr(prefix.size()), policy.token);
    if (!ok) {
      res.set_header("WWW-Authenticate", "Bearer");
      send_error(res, 401, "missing or wrong bearer token");
      return true;
    }
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

  // Security gate first, then the session-path checks. The web UI hook in
  // register_system_routes replaces this one when a web UI directory exists, so
  // it runs the same gate.
  server.set_pre_routing_handler([policy = options.security](const httplib::Request& req,
                                                             httplib::Response& res) {
    if (security_gate(req, res, policy)) return httplib::Server::HandlerResponse::Handled;
    if (rejected_session_path(req, res)) return httplib::Server::HandlerResponse::Handled;
    return httplib::Server::HandlerResponse::Unhandled;
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
