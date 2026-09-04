#include <qcode/core/ssl_config.h>

#include <cstdlib>

namespace qcode {
namespace http {

std::string ca_cert_file_from_env() {
  const char* file = std::getenv("SSL_CERT_FILE");
  return (file != nullptr && *file != '\0') ? std::string(file) : std::string{};
}

std::string ca_cert_dir_from_env() {
  const char* dir = std::getenv("SSL_CERT_DIR");
  return (dir != nullptr && *dir != '\0') ? std::string(dir) : std::string{};
}

namespace {

template <typename ClientT>
void apply_tls(ClientT& client, bool verify) {
  client.enable_server_certificate_verification(verify);
  if (!verify) {
    return;
  }
  const auto file = ca_cert_file_from_env();
  const auto dir = ca_cert_dir_from_env();
  if (!file.empty() || !dir.empty()) {
    client.set_ca_cert_path(file, dir);
  }
}

}  // namespace

void configure_client_tls(httplib::Client& client, bool verify) {
  apply_tls(client, verify);
}

void configure_client_tls(httplib::SSLClient& client, bool verify) {
  apply_tls(client, verify);
}

}  // namespace http
}  // namespace qcode
