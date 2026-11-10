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
//
// `address_family` pins the client to AF_INET or AF_INET6 when the caller
// already knows which family reaches the host; AF_UNSPEC lets httplib pick.
// See core/net_dial.h for why the family has to be probed.
std::unique_ptr<HttpClient> make_httplib_http_client(
    bool use_ssl, const std::string& host, int port, bool verify_ssl_cert,
    int connection_timeout_sec, int read_timeout_sec, int address_family);

// BoringSSL httplib transport for OpenCode Zen. Its ClientHello matches the
// Bun/BoringSSL stack used by the official OpenCode client, which the free
// tier currently requires.
std::unique_ptr<HttpClient> make_zen_http_client(
    const std::string& host, int port, bool verify_ssl_cert,
    int connection_timeout_sec, int read_timeout_sec, int address_family);

// Pins `cli` to the address family that actually reaches `host:port` on this
// machine. httplib takes the first address getaddrinfo returns and abandons
// the rest of the list when that connect times out, so on a host whose IPv6
// route is black-holed every request stalls for the full connect timeout
// instead of falling back to IPv4. A no-op when the family is undetermined.
//
// Callers that build an httplib::Client themselves (OAuth token refreshes,
// the CLI's call into the local server) use this so they get the same
// protection as the provider transports.
void pin_working_address_family(httplib::Client& cli, const std::string& host,
                                int port);

// Same, for a URL of the form "scheme://host[:port]".
void pin_working_address_family(httplib::Client& cli,
                                const std::string& scheme_host_port);

}  // namespace http
}  // namespace qcode
