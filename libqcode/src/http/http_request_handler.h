#pragma once

#include <qcode/retry/retry_policy.h>
#include <qcode/types/generate_options.h>

#include <functional>
#include <httplib.h>
#include <string>

#include <nlohmann/json.hpp>

namespace qcode {
namespace http {

struct HttpConfig {
  std::string host;
  int port = 0;  // explicit port from host:port (0 = default 443/80)
  std::string base_path;  // Base path from URL (e.g., "/api" from
                          // "https://openrouter.ai/api")
  bool use_ssl = true;
  int connection_timeout_sec = 30;
  int read_timeout_sec = 120;
  bool verify_ssl_cert = true;

  // Retry configuration
  retry::RetryConfig retry_config;
};

class HttpRequestHandler {
 public:
  explicit HttpRequestHandler(const HttpConfig& config);

  // Makes a POST request and returns the raw response wrapped in GenerateResult
  GenerateResult post(const std::string& path,
                      const httplib::Headers& headers,
                      const std::string& body,
                      const std::string& content_type = "application/json",
                      retry::RetryCallback on_retry = nullptr);

  // Extracts host and SSL settings from a base URL
  static HttpConfig parse_base_url(const std::string& base_url);

 private:
  HttpConfig config_;

  // Handler for processing HTTP responses
  using ResponseHandler =
      std::function<GenerateResult(const httplib::Result&, const std::string&)>;
  GenerateResult make_request(const std::string& path,
                              const httplib::Headers& headers,
                              const std::string& body,
                              const std::string& content_type,
                              ResponseHandler handler);

  // Execute a single HTTP request (used by retry logic)
  GenerateResult execute_single_request(const std::string& path,
                                        const httplib::Headers& headers,
                                        const std::string& body,
                                        const std::string& content_type);
};

}  // namespace http
}  // namespace qcode
