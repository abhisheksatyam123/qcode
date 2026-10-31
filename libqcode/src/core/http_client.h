#pragma once

#include <httplib.h>

#include <chrono>
#include <memory>
#include <string>

namespace qcode {
namespace http {

// Small client interface shared by the OpenSSL-backed httplib client and the
// BoringSSL-backed Zen client. Only the operations qcode's HTTP call sites use
// are exposed here.
class HttpClient {
 public:
  virtual ~HttpClient() = default;

  virtual httplib::Result Post(const std::string& path,
                               const httplib::Headers& headers,
                               const std::string& body,
                               const std::string& content_type) = 0;
  virtual bool send(httplib::Request& req, httplib::Response& res,
                    httplib::Error& error) = 0;
  virtual void stop() = 0;

  virtual void set_connection_timeout(time_t sec, time_t usec = 0) = 0;
  virtual void set_read_timeout(time_t sec, time_t usec = 0) = 0;
  virtual void set_write_timeout(time_t sec, time_t usec = 0) = 0;
  virtual void set_max_timeout(std::chrono::milliseconds msec) = 0;
  virtual void set_keep_alive(bool on) = 0;

  virtual std::string host() const = 0;
  virtual int port() const = 0;
};

// OpenSSL httplib transport, used for every provider except OpenCode Zen over
// TLS.
std::unique_ptr<HttpClient> make_httplib_http_client(
    bool use_ssl, const std::string& host, int port, bool verify_ssl_cert,
    int connection_timeout_sec, int read_timeout_sec);

// BoringSSL httplib transport for OpenCode Zen. Its ClientHello matches the
// Bun/BoringSSL stack used by the official OpenCode client, which the free
// tier currently requires.
std::unique_ptr<HttpClient> make_zen_http_client(
    const std::string& host, int port, bool verify_ssl_cert,
    int connection_timeout_sec, int read_timeout_sec);

}  // namespace http
}  // namespace qcode
