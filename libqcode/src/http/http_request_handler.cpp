#include "http_request_handler.h"

#include <qcode/logger/logger.h>
#include <qcode/retry/retry_policy.h>
#include "utils/response_utils.h"

namespace qcode {
namespace http {

HttpRequestHandler::HttpRequestHandler(const HttpConfig& config)
    : config_(config) {
  LOG_DEBUG(
      "HttpRequestHandler initialized - host: {}, use_ssl: {}", config_.host,
      config_.use_ssl);
}

HttpConfig HttpRequestHandler::parse_base_url(const std::string& base_url) {
  HttpConfig config;
  std::string url = base_url;

  // Extract protocol
  if (url.starts_with("https://")) {
    url = url.substr(8);
    config.use_ssl = true;
  } else if (url.starts_with("http://")) {
    url = url.substr(7);
    config.use_ssl = false;
  } else {
    config.use_ssl = true;
  }

  // Extract host (and optional explicit :port) and path
  auto pos = url.find('/');
  std::string host_part = (pos != std::string::npos) ? url.substr(0, pos) : url;
  config.base_path = (pos != std::string::npos) ? url.substr(pos) : "";

  // Split an explicit host:port so the port is applied explicitly via the
  // (host, port) constructor instead of relying on httplib parsing it from the
  // host string.
  auto colon = host_part.find(':');
  // Skip port parsing for IPv6 literals (e.g. "[::1]:8080"); httplib handles those via the host string.
  if (colon != std::string::npos && host_part.front() != '[') {
    try {
      config.port = std::stoi(host_part.substr(colon + 1));
    } catch (...) {
      config.port = 0;
    }
    host_part = host_part.substr(0, colon);
  }
  config.host = host_part;

  if (config.host.empty()) {
    LOG_ERROR("Http: base URL '{}' has no host; requests will fail", base_url);
  }

  // Remove trailing slash from base_path if present
  if (!config.base_path.empty() && config.base_path.back() == '/') {
    config.base_path.pop_back();
  }

  return config;
}

GenerateResult HttpRequestHandler::post(const std::string& path,
                                        const httplib::Headers& headers,
                                        const std::string& body,
                                        const std::string& content_type) {
  // Create a retry policy with the configured settings
  retry::RetryPolicy retry_policy(config_.retry_config);

  // Define the function to execute with retry
  auto execute_request = [this, &path, &headers, &body,
                          &content_type]() -> GenerateResult {
    return execute_single_request(path, headers, body, content_type);
  };

  // Define the function to check if a result is retryable
  std::function<bool(const GenerateResult&)> is_retryable =
      [](const GenerateResult& result) -> bool {
    return result.is_retryable.value_or(false);
  };

  try {
    return retry_policy.execute_with_retry(execute_request, is_retryable);
  } catch (const retry::RetryError& e) {
    LOG_ERROR("Request failed after retries: {}", e.what());
    GenerateResult error_result(e.what());
    error_result.is_retryable = false;  // Already retried
    return error_result;
  }
}

GenerateResult HttpRequestHandler::execute_single_request(
    const std::string& path,
    const httplib::Headers& headers,
    const std::string& body,
    const std::string& content_type) {
  auto handler = [](const httplib::Result& res,
                    const std::string& protocol) -> GenerateResult {
    if (!res) {
      LOG_ERROR("{} request failed - no response", protocol);
      GenerateResult result("Network error: Failed to connect to API");
      result.is_retryable = true;  // Network errors are retryable
      return result;
    }

    LOG_DEBUG("Got response: status={}, body_size={}", res->status,
                          res->body.size());

    if (res->status == 200) {
      GenerateResult result;
      result.text = res->body;
      result.finish_reason = kFinishReasonStop;  // HTTP request succeeded
      return result;
    }

    // For non-200 responses, return error with full body for parsing
    GenerateResult error_result;
    error_result.error = res->body;
    error_result.finish_reason = kFinishReasonError;
    error_result.provider_metadata = std::to_string(res->status);
    error_result.is_retryable = is_status_code_retryable(res->status);
    return error_result;
  };

  return make_request(path, headers, body, content_type, handler);
}

GenerateResult HttpRequestHandler::make_request(const std::string& path,
                                                const httplib::Headers& headers,
                                                const std::string& body,
                                                const std::string& content_type,
                                                ResponseHandler handler) {
  try {
    // Combine base_path with the endpoint path
    std::string full_path = config_.base_path + path;

    LOG_DEBUG("Making {} request to {}:{}{}",
                          config_.use_ssl ? "HTTPS" : "HTTP", config_.host,
                          full_path,
                          " with body size: " + std::to_string(body.size()));

    if (config_.use_ssl) {
      httplib::SSLClient cli(config_.host, config_.port != 0 ? config_.port : 443);
      cli.set_connection_timeout(config_.connection_timeout_sec, 0);
      cli.set_read_timeout(config_.read_timeout_sec, 0);
      cli.enable_server_certificate_verification(config_.verify_ssl_cert);

      auto res = cli.Post(full_path, headers, body, content_type);
      return handler(res, "HTTPS");
    } else {
      httplib::Client cli(config_.host, config_.port != 0 ? config_.port : 80);
      cli.set_connection_timeout(config_.connection_timeout_sec, 0);
      cli.set_read_timeout(config_.read_timeout_sec, 0);

      auto res = cli.Post(full_path, headers, body, content_type);
      return handler(res, "HTTP");
    }
  } catch (const std::exception& e) {
    LOG_ERROR("Exception in make_request: {}", e.what());
    return GenerateResult(std::string("Request failed: ") + e.what());
  } catch (...) {
    LOG_ERROR("Unknown exception in make_request");
    return GenerateResult("Request failed: Unknown error");
  }
}

}  // namespace http
}  // namespace qcode