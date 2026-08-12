#pragma once

#include <httplib.h>

#include <string>

namespace qcode {
namespace http {

// Returns SSL_CERT_FILE if set, else empty.
std::string ca_cert_file_from_env();

// Returns SSL_CERT_DIR if set, else empty.
std::string ca_cert_dir_from_env();

// Apply CA paths from the environment to an httplib TLS client.
// Safe to call for both SSLClient and Client (https:// scheme).
void configure_client_tls(httplib::Client& client, bool verify = true);
void configure_client_tls(httplib::SSLClient& client, bool verify = true);

}  // namespace http
}  // namespace qcode
