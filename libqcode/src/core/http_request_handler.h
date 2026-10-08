#pragma once

#include <qcode/core/retry_policy.h>
#include <qcode/core/generate_options.h>

#include <functional>
#include <atomic>
#include <httplib.h>
#include <memory>
#include <mutex>
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
  // Idle-between-reads. Muse Spark has completed real turns in ~90s, so
  // keep this generous; heartbeats in generation_service cover the UX.
  int read_timeout_sec = 90;
  // Hard cap on one POST. Keeps TLS/keepalive sockets from sitting "alive"
  // for minutes with no complete response (the frozen-TUI case).
  int max_timeout_sec = 150;
  bool verify_ssl_cert = true;

  // Retry configuration
  retry::RetryConfig retry_config;
};

class HttpRequestHandler {
 public:
  explicit HttpRequestHandler(const HttpConfig& config);

  // Makes a POST request and returns the raw response wrapped in GenerateResult.
  // When `abort_flag` is set, the retry schedule stops as soon as it flips
  // (Esc during generation must cancel pending retries).
  GenerateResult post(const std::string& path,
                      const httplib::Headers& headers,
                      const std::string& body,
                      const std::string& content_type = "application/json",
                      retry::RetryCallback on_retry = nullptr,
                      const std::shared_ptr<std::atomic<bool>>& abort_flag = nullptr);

  // Extracts host and SSL settings from a base URL
  static HttpConfig parse_base_url(const std::string& base_url);

 private:
  HttpConfig config_;

  // Keep-alive: idle connections are parked in a process-wide pool keyed by
  // scheme://host:port, so the next request to that host (retries, tool-loop
  // steps, later turns and subagents) skips TCP/TLS setup. A request owns its
  // client exclusively while in flight, so concurrent posts never share a
  // socket and the abort watcher's stop() only ever cancels its own request.
  std::unique_ptr<httplib::ClientImpl> acquire_client();
  void release_client(std::unique_ptr<httplib::ClientImpl> cli);

  // Handler for processing HTTP responses (may move the response body out).
  using ResponseHandler =
      std::function<GenerateResult(httplib::Result&, const std::string&)>;
  GenerateResult make_request(const std::string& path,
                              const httplib::Headers& headers,
                              const std::string& body,
                              const std::string& content_type,
                              ResponseHandler handler,
                              const std::shared_ptr<std::atomic<bool>>& abort_flag);

  // Execute a single HTTP request (used by retry logic). A set `abort_flag`
  // cancels the in-flight request, not just the retry schedule.
  GenerateResult execute_single_request(const std::string& path,
                                        const httplib::Headers& headers,
                                        const std::string& body,
                                        const std::string& content_type,
                                        const std::shared_ptr<std::atomic<bool>>& abort_flag);
};

// Keep-alive pool for streaming requests. The caller owns the borrowed client
// exclusively until it is released. Return it with release_stream_client()
// ONLY after a complete, successful response; on error or abort, let the
// unique_ptr destruct instead (its socket may hold cancelled or unread data).
// Streams always verify TLS.
std::unique_ptr<httplib::ClientImpl> acquire_stream_client(
    const std::string& scheme_host, int connection_timeout_sec,
    int read_timeout_sec);
void release_stream_client(const std::string& scheme_host,
                           std::unique_ptr<httplib::ClientImpl> cli);

}  // namespace http
}  // namespace qcode
