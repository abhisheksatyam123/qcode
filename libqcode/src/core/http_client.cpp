#include "core/http_client.h"

#include <qcode/core/ssl_config.h>

#include "core/net_dial.h"

#include <utility>

namespace qcode {
namespace http {

namespace {

class HttplibHttpClient final : public HttpClient {
 public:
  HttplibHttpClient(std::unique_ptr<httplib::ClientImpl> cli,
                    std::string host, int port)
      : cli_(std::move(cli)), host_(std::move(host)), port_(port) {}

  httplib::Result Post(const std::string& path,
                       const httplib::Headers& headers,
                       const std::string& body,
                       const std::string& content_type) override {
    return cli_->Post(path, headers, body, content_type);
  }

  bool send(httplib::Request& req, httplib::Response& res,
            httplib::Error& error) override {
    return cli_->send(req, res, error);
  }

  void stop() override { cli_->stop(); }

  void set_connection_timeout(time_t sec, time_t usec = 0) override {
    cli_->set_connection_timeout(sec, usec);
  }
  void set_read_timeout(time_t sec, time_t usec = 0) override {
    cli_->set_read_timeout(sec, usec);
  }
  void set_write_timeout(time_t sec, time_t usec = 0) override {
    cli_->set_write_timeout(sec, usec);
  }
  void set_max_timeout(std::chrono::milliseconds msec) override {
    cli_->set_max_timeout(msec);
  }
  void set_keep_alive(bool on) override { cli_->set_keep_alive(on); }

  std::string host() const override { return host_; }
  int port() const override { return port_; }

 private:
  std::unique_ptr<httplib::ClientImpl> cli_;
  std::string host_;
  int port_;
};

}  // namespace

std::unique_ptr<HttpClient> make_httplib_http_client(
    bool use_ssl, const std::string& host, int port, bool verify_ssl_cert,
    int connection_timeout_sec, int read_timeout_sec, int address_family) {
  std::unique_ptr<httplib::ClientImpl> cli;
  if (use_ssl) {
    auto ssl = std::make_unique<httplib::SSLClient>(host, port != 0 ? port : 443);
    configure_client_tls(*ssl, verify_ssl_cert);
    cli = std::move(ssl);
  } else {
    cli = std::make_unique<httplib::ClientImpl>(host, port != 0 ? port : 80);
  }
  // Pin the family that a probe showed to answer. Without this, httplib takes
  // the first address getaddrinfo returns and gives up on the whole list when
  // that connect times out — a machine whose IPv6 route is black-holed then
  // stalls on every provider request instead of using IPv4.
  if (address_family != AF_UNSPEC) {
    cli->set_address_family(address_family);
  }
  cli->set_connection_timeout(connection_timeout_sec, 0);
  cli->set_read_timeout(read_timeout_sec, 0);
  // httplib checks a parked socket is still alive before reusing it.
  cli->set_keep_alive(true);
  return std::make_unique<HttplibHttpClient>(
      std::move(cli), host, port != 0 ? port : (use_ssl ? 443 : 80));
}

void pin_working_address_family(httplib::Client& cli, const std::string& host,
                                int port) {
  const int family = net::probe_address_family(host, port);
  if (family != AF_UNSPEC) {
    cli.set_address_family(family);
  }
}

void pin_working_address_family(httplib::Client& cli,
                                const std::string& scheme_host_port) {
  std::string rest = scheme_host_port;
  if (const auto sep = rest.find("://"); sep != std::string::npos) {
    rest.erase(0, sep + 3);
  }
  if (const auto slash = rest.find('/'); slash != std::string::npos) {
    rest.resize(slash);
  }
  const bool default_tls = !scheme_host_port.starts_with("http://");
  std::string host = rest;
  int port = default_tls ? 443 : 80;
  // Skip port parsing for IPv6 literals ("[::1]:8080").
  if (rest.front() != '[') {
    if (const auto colon = rest.find(':'); colon != std::string::npos) {
      host = rest.substr(0, colon);
      try {
        port = std::stoi(rest.substr(colon + 1));
      } catch (...) {
        port = default_tls ? 443 : 80;
      }
    }
  } else if (const auto close = rest.find(']'); close != std::string::npos) {
    host = rest.substr(0, close + 1);
    if (const auto colon = rest.find(':', close); colon != std::string::npos) {
      try {
        port = std::stoi(rest.substr(colon + 1));
      } catch (...) {
        port = default_tls ? 443 : 80;
      }
    }
  }
  if (host.empty()) return;
  pin_working_address_family(cli, host, port);
}

}  // namespace http
}  // namespace qcode
