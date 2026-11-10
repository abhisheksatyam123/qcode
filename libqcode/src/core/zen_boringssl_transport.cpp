// Compiled with BoringSSL's prefixed headers and static libraries. Do not
// include system OpenSSL or the default httplib namespace from this file.

#include "core/zen_boringssl_transport.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#define httplib qcode_bssl
#include <httplib.h>
#undef httplib

struct QcodeZenClient {
  std::unique_ptr<qcode_bssl::SSLClient> cli;
};

namespace {

void set_error(char* buf, size_t size, const std::string& message) {
  if (!buf || size == 0) return;
  std::snprintf(buf, size, "%s", message.c_str());
}

}  // namespace

extern "C" {

QcodeZenClient* qcode_zen_client_new(const char* host, int port,
                                     int verify_ssl_cert,
                                     int connection_timeout_sec,
                                     int read_timeout_sec,
                                     int address_family) {
  if (!host || !*host) return nullptr;
  auto client = std::make_unique<QcodeZenClient>();
  client->cli = std::make_unique<qcode_bssl::SSLClient>(
      host, port != 0 ? port : 443);
  client->cli->enable_server_certificate_verification(verify_ssl_cert != 0);
  // Pin the family that a probe showed to answer, so a machine whose IPv6
  // route is black-holed uses IPv4 instead of stalling on every request.
  if (address_family != AF_UNSPEC) {
    client->cli->set_address_family(address_family);
  }
  const char* cert_file = std::getenv("SSL_CERT_FILE");
  const char* cert_dir = std::getenv("SSL_CERT_DIR");
  if ((cert_file && *cert_file) || (cert_dir && *cert_dir)) {
    client->cli->set_ca_cert_path(cert_file ? cert_file : "",
                                  cert_dir ? cert_dir : "");
  }
  client->cli->set_connection_timeout(connection_timeout_sec, 0);
  client->cli->set_read_timeout(read_timeout_sec, 0);
  client->cli->set_keep_alive(true);
  return client.release();
}

void qcode_zen_client_free(QcodeZenClient* client) { delete client; }

void qcode_zen_client_stop(QcodeZenClient* client) {
  if (client && client->cli) client->cli->stop();
}

void qcode_zen_client_set_timeouts(QcodeZenClient* client,
                                   int connection_timeout_sec,
                                   int read_timeout_sec) {
  if (!client || !client->cli) return;
  client->cli->set_connection_timeout(connection_timeout_sec, 0);
  client->cli->set_read_timeout(read_timeout_sec, 0);
}

void qcode_zen_client_set_max_timeout(QcodeZenClient* client, int64_t msec) {
  if (!client || !client->cli) return;
  client->cli->set_max_timeout(static_cast<time_t>(msec));
}

void qcode_zen_client_set_keep_alive(QcodeZenClient* client, int on) {
  if (client && client->cli) client->cli->set_keep_alive(on != 0);
}

QcodeZenError qcode_zen_client_send(
    QcodeZenClient* client, const char* method, const char* path,
    const QcodeZenHeader* headers, size_t header_count, const char* body,
    size_t body_length, const char* content_type, int* out_status,
    QcodeZenResponseCallback response_cb, QcodeZenHeaderCallback header_cb,
    QcodeZenContentCallback content_cb, void* user, char* error_buf,
    size_t error_buf_size) {
  if (!client || !client->cli || !out_status) {
    set_error(error_buf, error_buf_size, "invalid Zen transport client");
    return QCODE_ZEN_UNKNOWN;
  }

  qcode_bssl::Request req;
  req.method = method && *method ? method : "GET";
  req.path = path && *path ? path : "/";
  for (size_t i = 0; i < header_count; ++i) {
    if (headers && headers[i].name && headers[i].value) {
      req.headers.emplace(headers[i].name, headers[i].value);
    }
  }
  if (body && body_length > 0) req.body.assign(body, body_length);
  if (content_type && *content_type) req.set_header("Content-Type", content_type);

  req.response_handler = [&](const auto& response) {
    *out_status = response.status;
    if (response_cb && !response_cb(user, response.status)) return false;
    if (!header_cb) return true;
    for (const auto& header : response.headers) {
      if (!header_cb(user, header.first.c_str(), header.second.c_str()))
        return false;
    }
    return true;
  };
  if (content_cb) {
    req.content_receiver = [&](const char* data, size_t length, uint64_t offset,
                               uint64_t total) {
      return content_cb(user, data, length, offset, total) != 0;
    };
  }

  qcode_bssl::Response response;
  qcode_bssl::Error error = qcode_bssl::Error::Success;
  const bool ok = client->cli->send(req, response, error);
  if (!ok) {
    set_error(error_buf, error_buf_size, qcode_bssl::to_string(error));
    const int code = static_cast<int>(error);
    if (code < 0 || code > QCODE_ZEN_PROXY_CONNECTION) {
      return QCODE_ZEN_UNKNOWN;
    }
    return static_cast<QcodeZenError>(code);
  }
  *out_status = response.status;
  return QCODE_ZEN_SUCCESS;
}

}  // extern "C"
