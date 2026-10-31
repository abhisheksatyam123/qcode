#include "core/http_client.h"

#include <qcode/core/ssl_config.h>

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
    int connection_timeout_sec, int read_timeout_sec) {
  std::unique_ptr<httplib::ClientImpl> cli;
  if (use_ssl) {
    auto ssl = std::make_unique<httplib::SSLClient>(host, port != 0 ? port : 443);
    configure_client_tls(*ssl, verify_ssl_cert);
    cli = std::move(ssl);
  } else {
    cli = std::make_unique<httplib::ClientImpl>(host, port != 0 ? port : 80);
  }
  cli->set_connection_timeout(connection_timeout_sec, 0);
  cli->set_read_timeout(read_timeout_sec, 0);
  // httplib checks a parked socket is still alive before reusing it.
  cli->set_keep_alive(true);
  return std::make_unique<HttplibHttpClient>(
      std::move(cli), host, port != 0 ? port : (use_ssl ? 443 : 80));
}

}  // namespace http
}  // namespace qcode
