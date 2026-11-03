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

constexpr const char* kCookieName = "qcode_auth";

constexpr const char* kLoginPage = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>qcode sign in</title>
<style>
body { font: 16px system-ui, sans-serif; margin: 0; min-height: 100vh; display: grid; place-items: center; background: #f5f5f5; color: #222; }
form { background: #fff; padding: 24px; border-radius: 8px; box-shadow: 0 1px 4px rgba(0,0,0,.15); width: min(90vw, 360px); box-sizing: border-box; }
h1 { font-size: 20px; margin: 0 0 8px; }
p { margin: 0 0 12px; }
input { width: 100%; box-sizing: border-box; padding: 10px; font-size: 16px; margin: 0 0 12px; }
button { padding: 10px 16px; font-size: 16px; }
@media (prefers-color-scheme: dark) { body { background: #1b1b1d; color: #eee; } form { background: #2a2a2d; } }
</style></head>
<body>
<form method="post" action="/login">
  <h1>qcode</h1>
  <p>Paste the token printed when the server started.</p>
  <label>Token <input name="token" type="password" autocomplete="off" autofocus required></label>
  <button type="submit">Sign in</button>
</form>
</body></html>
)HTML";

// Value of one cookie from a Cookie header ("a=1; b=2"), or empty.
std::string cookie_named(std::string_view header, std::string_view name) {
  while (!header.empty()) {
    const auto semi = header.find(';');
    std::string_view part = header.substr(0, semi);
    header = semi == std::string_view::npos ? std::string_view{} : header.substr(semi + 1);
    while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
    const auto eq = part.find('=');
    if (eq != std::string_view::npos && part.substr(0, eq) == name) {
      return std::string(part.substr(eq + 1));
    }
  }
  return {};
}

// One field from an application/x-www-form-urlencoded body, percent-decoded.
std::string form_field(std::string_view body, std::string_view name) {
  while (!body.empty()) {
    const auto amp = body.find('&');
    const std::string_view part = body.substr(0, amp);
    body = amp == std::string_view::npos ? std::string_view{} : body.substr(amp + 1);
    const auto eq = part.find('=');
    if (eq != std::string_view::npos && part.substr(0, eq) == name) {
      return url_decode(std::string(part.substr(eq + 1)));
    }
  }
  return {};
}

void set_session_cookie(httplib::Response& res, const SecurityPolicy& policy) {
  res.set_header("Set-Cookie", std::string(kCookieName) + "=" + policy.cookie_value +
                                   "; Path=/; HttpOnly; SameSite=Strict");
}

// Bearer header or session cookie.
bool authorized(const httplib::Request& req, const SecurityPolicy& policy) {
  const std::string auth = req.get_header_value("Authorization");
  const std::string prefix = "Bearer ";
  if (auth.rfind(prefix, 0) == 0 &&
      constant_time_equal(std::string_view(auth).substr(prefix.size()), policy.token)) {
    return true;
  }
  const std::string cookie = cookie_named(req.get_header_value("Cookie"), kCookieName);
  return !cookie.empty() && !policy.cookie_value.empty() &&
         constant_time_equal(cookie, policy.cookie_value);
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

  if (!policy.token.empty()) {
    // The login page and its POST only hand out the session cookie.
    if (req.path == "/login") {
      if (req.method == "GET" || req.method == "HEAD") {
        res.set_content(kLoginPage, "text/html; charset=utf-8");
        return true;
      }
      // POST /login reads its form body in a route; pre-routing runs before the body is read.
      if (req.method == "POST") return false;
      send_error(res, 405, "method not allowed");
      return true;
    }

    // First visit with ?token=...: trade the token for the session cookie and
    // redirect to the same path without the token in the URL.
    if (req.method == "GET" && req.has_param("token") &&
        constant_time_equal(req.get_param_value("token"), policy.token)) {
      set_session_cookie(res, policy);
      res.status = 302;
      res.set_header("Location", req.path);
      return true;
    }

    if (!is_health_probe(req) && !authorized(req, policy)) {
      if ((req.method == "GET" || req.method == "HEAD") &&
          req.get_header_value("Accept").find("text/html") != std::string::npos) {
        res.status = 302;
        res.set_header("Location", "/login");
        return true;
      }
      res.set_header("WWW-Authenticate", "Bearer");
      send_error(res, 401, "missing or wrong token: open the URL printed at startup, or sign in at /login");
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
  // Sign-in form. Reached through pre-routing with no token yet, so the body is read here.
  server.Post("/login", [policy = options.security](const httplib::Request& req, httplib::Response& res) {
    if (policy.token.empty()) {
      send_error(res, 404, "not found");
      return;
    }
    if (!constant_time_equal(form_field(req.body, "token"), policy.token)) {
      send_error(res, 401, "wrong token");
      return;
    }
    set_session_cookie(res, policy);
    res.status = 303;
    res.set_header("Location", "/");
  });

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
