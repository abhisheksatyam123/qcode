#include <qcode/core/perf.h>
#include "core/http_request_handler.h"

#include <qcode/core/ssl_config.h>
#include <qcode/core/logger.h>
#include <qcode/core/retry_policy.h>
#include "providers/internal/opencode_zen_headers.h"
#include "core/response_utils.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace qcode {
namespace http {

namespace {

// httplib has no cancel token for a blocking Post. Client::stop() is its
// thread-safe abort: it shuts the in-flight socket so the read fails at once.
// Without this, Esc only cut the retry schedule and the turn kept waiting on
// the model call for up to max_timeout_sec. stop() is repeated while aborted
// because a call that lands before the socket connects is a no-op. The flag
// is polled (its owners store() without notifying); the cv only ends the
// watcher the moment the POST returns.
httplib::Result post_abortable(httplib::ClientImpl& cli, const std::string& path,
                               const httplib::Headers& headers,
                               const std::string& body,
                               const std::string& content_type,
                               const std::shared_ptr<std::atomic<bool>>& abort_flag) {
  if (!abort_flag) return cli.Post(path, headers, body, content_type);
  // Already stopped (e.g. Esc during a retry back-off): skip the round trip.
  if (abort_flag->load()) return httplib::Result{nullptr, httplib::Error::Canceled};

  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  std::thread watcher([&] {
    std::unique_lock<std::mutex> lock(mu);
    while (!cv.wait_for(lock, std::chrono::milliseconds(50), [&] { return done; })) {
      if (abort_flag->load()) {
        lock.unlock();
        cli.stop();
        lock.lock();
      }
    }
  });
  auto finish = [&] {
    {
      std::lock_guard<std::mutex> lock(mu);
      done = true;
    }
    cv.notify_one();
    watcher.join();
  };
  try {
    auto res = cli.Post(path, headers, body, content_type);
    finish();
    return res;
  } catch (...) {
    finish();  // a joinable std::thread would std::terminate on unwind
    throw;
  }
}

// Builds a fresh client (TCP+TLS not yet connected; httplib connects lazily).
std::unique_ptr<httplib::ClientImpl> new_client(bool use_ssl,
                                                const std::string& host,
                                                int port, bool verify_ssl_cert,
                                                int connection_timeout_sec,
                                                int read_timeout_sec) {
  std::unique_ptr<httplib::ClientImpl> cli;
  if (use_ssl) {
    auto ssl = std::make_unique<httplib::SSLClient>(
        host, port != 0 ? port : 443);
    configure_client_tls(*ssl, verify_ssl_cert);
    cli = std::move(ssl);
  } else {
    cli = std::make_unique<httplib::ClientImpl>(host, port != 0 ? port : 80);
  }
  cli->set_connection_timeout(connection_timeout_sec, 0);
  cli->set_read_timeout(read_timeout_sec, 0);
  // httplib checks a parked socket is still alive before reusing it.
  cli->set_keep_alive(true);
  return cli;
}

// Keep-alive pools: idle clients keyed by "scheme://host:port". They are
// process-wide because handlers and streams are created per turn/subagent, so
// a per-owner slot would redo the TCP/TLS handshake on every turn. A borrowed
// client is owned exclusively until released: concurrent requests never share
// a socket, and an abort watcher's stop() only cancels its own request.
class IdlePool {
 public:
  std::unique_ptr<httplib::ClientImpl> take(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    // Newest first: the most recently used socket is the likeliest alive.
    for (std::size_t i = idle_.size(); i-- > 0;) {
      if (idle_[i].key != key) continue;
      auto cli = std::move(idle_[i].cli);
      idle_.erase(idle_.begin() + static_cast<std::ptrdiff_t>(i));
      return cli;
    }
    return nullptr;
  }

  void put(const std::string& key, std::unique_ptr<httplib::ClientImpl> cli) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto same_key = std::count_if(
        idle_.begin(), idle_.end(), [&](const Idle& e) { return e.key == key; });
    if (same_key >= kMaxIdlePerKey) return;  // cli closes
    idle_.push_back({key, std::move(cli)});
  }

 private:
  static constexpr std::ptrdiff_t kMaxIdlePerKey = 4;
  struct Idle {
    std::string key;
    std::unique_ptr<httplib::ClientImpl> cli;
  };
  std::mutex mu_;
  std::vector<Idle> idle_;
};

// Separate pools because the clients differ: streams always verify TLS and
// have no max_timeout cap (a stream can run long); posts carry their
// handler's cap.
IdlePool g_stream_pool;
IdlePool g_post_pool;

std::string pool_key(bool use_ssl, const std::string& host, int port) {
  return std::string(use_ssl ? "https://" : "http://") + host + ":" +
         std::to_string(port != 0 ? port : (use_ssl ? 443 : 80));
}

// TLS verification is part of a client's setup, so it partitions the pool.
std::string post_pool_key(const HttpConfig& config) {
  return pool_key(config.use_ssl, config.host, config.port) +
         (config.verify_ssl_cert ? "" : "#noverify");
}

struct StreamTarget {
  bool use_ssl = true;
  std::string host;
  int port = 0;  // 0 = scheme default
  std::string key;
};

// Accepts "https://host[:port][/...]", "http://host[:port]" or a bare host.
StreamTarget parse_stream_target(const std::string& scheme_host) {
  StreamTarget t;
  std::string rest = scheme_host;
  if (rest.starts_with("http://")) {
    t.use_ssl = false;
    rest.erase(0, 7);
  } else if (rest.starts_with("https://")) {
    rest.erase(0, 8);
  }
  if (auto slash = rest.find('/'); slash != std::string::npos) {
    rest.resize(slash);
  }
  // Skip port parsing for IPv6 literals ("[::1]:8080"), as parse_base_url does.
  if (auto colon = rest.find(':');
      colon != std::string::npos && rest.front() != '[') {
    try {
      t.port = std::stoi(rest.substr(colon + 1));
    } catch (...) {
      t.port = 0;
    }
    rest.resize(colon);
  }
  t.host = rest;
  t.key = pool_key(t.use_ssl, t.host, t.port);
  return t;
}

}  // namespace

HttpRequestHandler::HttpRequestHandler(const HttpConfig& config)
    : config_(config) {
  LOG_DEBUG(
      "HttpRequestHandler initialized - host: {}, use_ssl: {}", config_.host,
      config_.use_ssl);
}

std::unique_ptr<httplib::ClientImpl> HttpRequestHandler::acquire_client() {
  auto cli = g_post_pool.take(post_pool_key(config_));
  PERF_LOG("http connection={} host={}", cli ? "reused" : "new", config_.host);
  if (cli) {
    // Another handler for this host may have parked it with other timeouts.
    cli->set_connection_timeout(config_.connection_timeout_sec, 0);
    cli->set_read_timeout(config_.read_timeout_sec, 0);
  } else {
    cli = new_client(config_.use_ssl, config_.host, config_.port,
                     config_.verify_ssl_cert, config_.connection_timeout_sec,
                     config_.read_timeout_sec);
  }
  // 0 = no cap.
  cli->set_max_timeout(std::chrono::milliseconds(
      static_cast<long long>(std::max(config_.max_timeout_sec, 0)) * 1000));
  return cli;
}

void HttpRequestHandler::release_client(std::unique_ptr<httplib::ClientImpl> cli) {
  g_post_pool.put(post_pool_key(config_), std::move(cli));
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

GenerateResult HttpRequestHandler::post(
    const std::string& path,
    const httplib::Headers& headers,
    const std::string& body,
    const std::string& content_type,
    retry::RetryCallback on_retry,
    const std::shared_ptr<std::atomic<bool>>& abort_flag) {
  // Create a retry policy with the configured settings
  retry::RetryPolicy retry_policy(config_.retry_config);
  if (abort_flag) {
    std::weak_ptr<std::atomic<bool>> weak_abort = abort_flag;
    retry_policy.config().should_stop = [weak_abort]() -> bool {
      if (auto flag = weak_abort.lock()) return flag->load();
      return true;  // request context died — stop scheduling
    };
  }

  // Define the function to execute with retry
  auto execute_request = [this, &path, &headers, &body, &content_type,
                          &abort_flag]() -> GenerateResult {
    return execute_single_request(path, headers, body, content_type, abort_flag);
  };

  // Define the function to check if a result is retryable
  std::function<bool(const GenerateResult&)> is_retryable =
      [](const GenerateResult& result) -> bool {
    return result.is_retryable.value_or(false);
  };

  try {
    return retry_policy.execute_with_retry(execute_request, is_retryable, on_retry);
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
    const std::string& content_type,
    const std::shared_ptr<std::atomic<bool>>& abort_flag) {
  auto handler = [&abort_flag](httplib::Result& res,
                               const std::string& protocol) -> GenerateResult {
    if (!res && abort_flag && abort_flag->load()) {
      // Our own cli.stop() — report the sentinel generation_service treats
      // as a clean user stop, not a retryable network error.
      LOG_INFO("{} request cancelled by abort", protocol);
      GenerateResult stopped("Aborted by user");
      stopped.is_retryable = false;
      return stopped;
    }
    if (!res) {
      LOG_ERROR("{} request failed - no response ({})", protocol,
                httplib::to_string(res.error()));
      GenerateResult result("Network error: Failed to connect to API (" +
                            httplib::to_string(res.error()) + ")");
      result.is_retryable = true;  // Network errors are retryable
      return result;
    }

    LOG_DEBUG("Got response: status={}, body_size={}", res->status,
                          res->body.size());

    if (res->status == 200) {
      // The drop check needs native_finish_reason == "network_error"; skip
      // the extra full-body parse (the caller parses it again) otherwise.
      if (res->body.find("network_error") != std::string::npos) {
        try {
          const auto json = nlohmann::json::parse(res->body);
          if (qcode::utils::is_empty_upstream_network_drop(json)) {
            LOG_WARN(
                "HTTP 200 empty completion with native_finish_reason=network_error");
            GenerateResult dropped("Upstream network error: empty completion");
            dropped.is_retryable = true;
            return dropped;
          }
        } catch (const nlohmann::json::exception&) {
        }
      }

      GenerateResult result;
      result.text = std::move(res->body);
      result.finish_reason = kFinishReasonStop;  // HTTP request succeeded
      return result;
    }

    LOG_WARN("HTTP {} from provider (status={}): {}", protocol, res->status,
             res->body.size() > 240 ? res->body.substr(0, 240) + "…"
                                    : res->body);

    // For non-200 responses, return error with full body for parsing
    GenerateResult error_result;
    error_result.error = res->body;
    error_result.finish_reason = kFinishReasonError;
    error_result.provider_metadata = std::to_string(res->status);
    // Context overflow must NOT be retried (needs compaction), even though
    // some messages contain retryable substrings like "overloaded".
    if (is_context_overflow_error(res->status, res->body)) {
      LOG_WARN("Context overflow detected (status={}), not retrying", res->status);
      error_result.is_retryable = false;
    } else if (is_permanent_not_found(res->status, res->body)) {
      LOG_WARN("Permanent 404 (entity not found), not retrying");
      error_result.is_retryable = false;
    } else {
      error_result.is_retryable =
          is_status_code_retryable(res->status) ||
          is_error_message_retryable(res->body);
    }

    // Honor Retry-After / retry-after-ms response headers when present — the
    // retry policy uses this hint verbatim (upstream SessionRetry.delay()).
    // httplib header lookup is case-insensitive.
    if (res->has_header("retry-after-ms")) {
      try {
        long long ms = std::stoll(res->get_header_value("retry-after-ms"));
        if (ms > 0) {
          LOG_INFO("Provider requested retry delay of {} ms", ms);
          error_result.retry_after_ms = ms;
        }
      } catch (...) {}
    } else if (res->has_header("retry-after")) {
      const auto value = res->get_header_value("retry-after");
      try {
        long long sec = std::stoll(value);
        if (sec > 0) {
          const long long ms = sec * 1000;
          LOG_INFO("Provider requested Retry-After delay of {} s", sec);
          error_result.retry_after_ms = ms;
        }
      } catch (...) {
        // Not a plain integer — try HTTP-date format.
        std::tm tm{};
        std::istringstream ss(value);
        std::time_t parsed = -1;
        if (ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT"); !ss.fail()) {
#ifdef _WIN32
          parsed = _mkgmtime(&tm);
#else
          parsed = timegm(&tm);
#endif
        }
        if (parsed > 0) {
          const auto now = std::time(nullptr);
          const long long secs_until = static_cast<long long>(parsed - now);
          if (secs_until > 0) {
            LOG_INFO("Provider requested Retry-After until HTTP date ({} s)",
                     secs_until);
            error_result.retry_after_ms = secs_until * 1000;
          }
        }
      }
    }

    return error_result;
  };

  return make_request(path, headers, body, content_type, handler, abort_flag);
}

GenerateResult HttpRequestHandler::make_request(const std::string& path,
                                                const httplib::Headers& headers,
                                                const std::string& body,
                                                const std::string& content_type,
                                                ResponseHandler handler,
                                                const std::shared_ptr<std::atomic<bool>>& abort_flag) {
  try {
    // Combine base_path with the endpoint path
    std::string full_path = config_.base_path + path;
    httplib::Headers request_headers = headers;
    if (qcode::providers::is_opencode_zen_url(config_.host)) {
      qcode::providers::apply_opencode_zen_headers(request_headers);
    }

    const char* protocol = config_.use_ssl ? "HTTPS" : "HTTP";
    LOG_DEBUG("Making {} request to {}:{} with body size: {}", protocol,
              config_.host, full_path, body.size());

    auto cli = acquire_client();
    const qcode::perf::Stopwatch post_watch;
    auto res = post_abortable(*cli, full_path, request_headers, body,
                              content_type, abort_flag);
    PERF_LOG("http post_ms={:.1f} status={} bytes_out={} bytes_in={} aborted={}",
             post_watch.ms(), res ? res->status : 0, body.size(),
             res ? res->body.size() : 0, abort_flag && abort_flag->load());
    // The watcher has joined, so nothing can stop() this client any more.
    // Only a clean 200 keeps its socket for reuse; errors and aborts drop it.
    const bool aborted = abort_flag && abort_flag->load();
    if (res && res->status == 200 && !aborted) {
      release_client(std::move(cli));
    }
    return handler(res, protocol);
  } catch (const std::exception& e) {
    LOG_ERROR("Exception in make_request: {}", e.what());
    GenerateResult error_result("Exception: " + std::string(e.what()));
    error_result.is_retryable = true;  // Exceptions (like network issues) are retryable
    return error_result;
  }
}

std::unique_ptr<httplib::ClientImpl> acquire_stream_client(
    const std::string& scheme_host, int connection_timeout_sec,
    int read_timeout_sec) {
  const StreamTarget target = parse_stream_target(scheme_host);
  if (auto cli = g_stream_pool.take(target.key)) {
    // Timeouts may differ per call site; re-apply them to the reused client.
    cli->set_connection_timeout(connection_timeout_sec, 0);
    cli->set_read_timeout(read_timeout_sec, 0);
    return cli;
  }
  return new_client(target.use_ssl, target.host, target.port,
                    /*verify_ssl_cert=*/true, connection_timeout_sec,
                    read_timeout_sec);
}

void release_stream_client(const std::string& scheme_host,
                           std::unique_ptr<httplib::ClientImpl> cli) {
  if (!cli) return;
  g_stream_pool.put(parse_stream_target(scheme_host).key, std::move(cli));
}

}  // namespace http
}  // namespace qcode
