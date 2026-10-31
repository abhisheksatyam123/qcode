#include "core/http_client.h"
#include "core/zen_boringssl_transport.h"

#include <utility>
#include <vector>

namespace qcode {
namespace http {

namespace {

struct SendContext {
  httplib::Request* req = nullptr;
  httplib::Response* res = nullptr;
};

int on_response(void* user, int status) {
  auto* ctx = static_cast<SendContext*>(user);
  ctx->res->status = status;
  if (ctx->req->response_handler) {
    return ctx->req->response_handler(*ctx->res) ? 1 : 0;
  }
  return 1;
}

int on_header(void* user, const char* name, const char* value) {
  auto* ctx = static_cast<SendContext*>(user);
  ctx->res->headers.emplace(name ? name : "", value ? value : "");
  return 1;
}

int on_content(void* user, const char* data, size_t length, uint64_t offset,
               uint64_t total) {
  auto* ctx = static_cast<SendContext*>(user);
  if (ctx->req->content_receiver) {
    return ctx->req->content_receiver(data, length, offset, total) ? 1 : 0;
  }
  ctx->res->body.append(data, length);
  return 1;
}

class ZenHttpClient final : public HttpClient {
 public:
  ZenHttpClient(const std::string& host, int port, bool verify_ssl_cert,
                int connection_timeout_sec, int read_timeout_sec)
      : host_(host),
        port_(port != 0 ? port : 443),
        cli_(qcode_zen_client_new(host.c_str(), port_, verify_ssl_cert ? 1 : 0,
                                  connection_timeout_sec, read_timeout_sec)) {}

  ~ZenHttpClient() override { qcode_zen_client_free(cli_); }

  httplib::Result Post(const std::string& path,
                       const httplib::Headers& headers,
                       const std::string& body,
                       const std::string& content_type) override {
    httplib::Request req;
    req.method = "POST";
    req.path = path;
    req.headers = headers;
    req.body = body;
    if (!content_type.empty()) req.set_header("Content-Type", content_type);

    httplib::Response res;
    httplib::Error error = httplib::Error::Success;
    if (!send(req, res, error)) {
      return httplib::Result(nullptr, error);
    }
    auto response = std::make_unique<httplib::Response>(std::move(res));
    return httplib::Result(std::move(response), httplib::Error::Success,
                           std::move(req.headers));
  }

  bool send(httplib::Request& req, httplib::Response& res,
            httplib::Error& error) override {
    if (!cli_) {
      error = httplib::Error::Connection;
      return false;
    }

    std::vector<QcodeZenHeader> headers;
    headers.reserve(req.headers.size());
    for (const auto& [name, value] : req.headers) {
      headers.push_back({name.c_str(), value.c_str()});
    }

    SendContext ctx{&req, &res};
    int status = 0;
    char error_buf[256] = {};
    const QcodeZenError result = qcode_zen_client_send(
        cli_, req.method.c_str(), req.path.c_str(), headers.data(),
        headers.size(), req.body.data(), req.body.size(), nullptr, &status,
        on_response, on_header, on_content, &ctx, error_buf, sizeof(error_buf));

    if (result != QCODE_ZEN_SUCCESS) {
      error = static_cast<httplib::Error>(static_cast<int>(result));
      return false;
    }
    res.status = status;
    error = httplib::Error::Success;
    return true;
  }

  void stop() override { qcode_zen_client_stop(cli_); }

  void set_connection_timeout(time_t sec, time_t usec = 0) override {
    (void)usec;
    if (!cli_) return;
    connection_timeout_sec_ = static_cast<int>(sec);
    qcode_zen_client_set_timeouts(cli_, connection_timeout_sec_,
                                  read_timeout_sec_);
  }
  void set_read_timeout(time_t sec, time_t usec = 0) override {
    (void)usec;
    if (!cli_) return;
    read_timeout_sec_ = static_cast<int>(sec);
    qcode_zen_client_set_timeouts(cli_, connection_timeout_sec_,
                                  read_timeout_sec_);
  }
  void set_write_timeout(time_t, time_t = 0) override {}
  void set_max_timeout(std::chrono::milliseconds msec) override {
    qcode_zen_client_set_max_timeout(cli_, static_cast<int64_t>(msec.count()));
  }
  void set_keep_alive(bool on) override {
    qcode_zen_client_set_keep_alive(cli_, on ? 1 : 0);
  }

  std::string host() const override { return host_; }
  int port() const override { return port_; }

 private:
  std::string host_;
  int port_;
  int connection_timeout_sec_ = 30;
  int read_timeout_sec_ = 90;
  QcodeZenClient* cli_ = nullptr;
};

}  // namespace

std::unique_ptr<HttpClient> make_zen_http_client(
    const std::string& host, int port, bool verify_ssl_cert,
    int connection_timeout_sec, int read_timeout_sec) {
  return std::make_unique<ZenHttpClient>(host, port, verify_ssl_cert,
                                         connection_timeout_sec,
                                         read_timeout_sec);
}

}  // namespace http
}  // namespace qcode
