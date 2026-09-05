#include "openai_stream.h"

#include "openai_response_parser.h"
#include <qcode/core/ssl_config.h>
#include <qcode/core/logger.h>
#include "core/http_request_handler.h"
#include "providers/internal/opencode_zen_headers.h"
#include "core/response_utils.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <random>

namespace {
std::chrono::seconds default_event_timeout() {
  if (const char* v = std::getenv("QCODE_STREAM_EVENT_TIMEOUT_SEC")) {
    try {
      const int secs = std::stoi(v);
      if (secs > 0) return std::chrono::seconds(secs);
    } catch (...) {}
  }
  return std::chrono::seconds(30);
}
constexpr auto kSleepInterval = std::chrono::milliseconds(1);
constexpr auto kConnectionTimeout = 30;  // seconds
constexpr auto kReadTimeout = 300;       // 5 minutes for long generations
}  // namespace

namespace qcode {
namespace openai {

OpenAIStreamImpl::~OpenAIStreamImpl() {
  stop_stream();
}

void OpenAIStreamImpl::start_stream(const std::string& url,
                                    const httplib::Headers& headers,
                                    const nlohmann::json& request_body) {
  LOG_DEBUG("Starting OpenAI stream - URL: {}", url);

  std::lock_guard<std::mutex> lock(thread_mutex_);

  if (stream_thread_.joinable()) {
    LOG_DEBUG(
        "Stream thread already running, not starting new one");
    return;  // Already running
  }

  // Reset state for new stream
  should_stop_ = false;
  is_complete_ = false;
  finish_event_pushed_ = false;
  event_timeout_ = default_event_timeout();

  LOG_INFO("Launching stream thread for OpenAI API");

  stream_thread_ = std::thread([this, url, headers, request_body]() {
    run_stream(url, headers, request_body);
  });
}

StreamEvent OpenAIStreamImpl::get_next_event() {
  StreamEvent event("");
  auto start_time = std::chrono::steady_clock::now();

  while (!event_queue_.try_dequeue(event)) {
    if (is_complete_ && event_queue_.size_approx() == 0) {
      // Stream is complete and queue is empty
      LOG_DEBUG(
          "Stream complete and queue empty, returning empty event");
      return StreamEvent("");
    }

    // Check for timeout
    if (std::chrono::steady_clock::now() - start_time > event_timeout_) {
      LOG_ERROR(
          "Timeout waiting for next stream event after {} seconds",
          event_timeout_.count());
      return StreamEvent(kStreamEventTypeError,
                         "Timeout waiting for next event");
    }

    std::this_thread::sleep_for(kSleepInterval);
  }

  LOG_DEBUG("Dequeued event type: {}",
                        static_cast<int>(event.type));
  return event;
}

bool OpenAIStreamImpl::has_more_events() const {
  // No locks needed - these are atomic operations
  return event_queue_.size_approx() > 0 || !is_complete_;
}

void OpenAIStreamImpl::stop_stream() {
  LOG_DEBUG("Stopping OpenAI stream");

  should_stop_ = true;  // Atomic write

  std::lock_guard<std::mutex> lock(thread_mutex_);
  if (stream_thread_.joinable()) {
    LOG_DEBUG("Waiting for stream thread to finish");
    stream_thread_.join();
    LOG_INFO("OpenAI stream stopped successfully");
  }
}

void OpenAIStreamImpl::run_stream(const std::string& url,
                                  const httplib::Headers& headers,
                                  const nlohmann::json& request_body) {
  // Extract host and path from URL
  std::string_view url_view(url);
  const bool use_ssl = url_view.starts_with("https://");

  // Skip protocol
  if (auto pos = url_view.find("://"); pos != std::string_view::npos) {
    url_view.remove_prefix(pos + 3);
  }

  // Split host and path
  auto slash_pos = url_view.find('/');
  std::string host(url_view.substr(0, slash_pos));
  std::string path = (slash_pos != std::string_view::npos)
                         ? std::string(url_view.substr(slash_pos))
                         : "/v1/chat/completions";

  LOG_DEBUG(
      "Stream thread started - connecting to {} with path: {}", host, path);

  try {
    httplib::Client client(
        std::string(use_ssl ? "https://" : "http://") + host);
    if (use_ssl) {
      qcode::http::configure_client_tls(client, true);
    }
    client.set_connection_timeout(kConnectionTimeout);
    client.set_read_timeout(kReadTimeout);

    LOG_DEBUG(
        "SSL client created with connection_timeout: {}s, read_timeout: {}s",
        kConnectionTimeout, kReadTimeout);

    std::string accumulated_data;

    // Create request
    httplib::Request req;
    req.method = "POST";
    req.path = path;
    req.headers = headers;
    req.body = request_body.dump();
    req.set_header("Content-Type", "application/json");
    if (qcode::providers::is_opencode_zen_url(host)) {
      qcode::providers::apply_opencode_zen_headers(req.headers);
    }

    LOG_DEBUG(
        "Stream request prepared - path: {}, body size: {} bytes", path,
        req.body.length());

    // Set content receiver for streaming response
    req.content_receiver = [this, &accumulated_data](
                               const char* data, size_t data_length,
                               uint64_t /*offset*/, uint64_t /*total_length*/) {
      // Accumulate data and process complete lines
      accumulated_data.append(data, data_length);

      LOG_DEBUG("Received {} bytes of stream data", data_length);

      // Process complete lines
      size_t pos = 0;
      while ((pos = accumulated_data.find('\n')) != std::string::npos) {
        std::string line = accumulated_data.substr(0, pos);
        accumulated_data.erase(0, pos + 1);

        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }

        // Check if we should stop - atomic read, no lock needed
        if (should_stop_) {
          LOG_DEBUG(
              "Stream stop requested, ending content receiver");
          return false;
        }

        parse_sse_line(line);
      }

      return true;  // Continue receiving
    };

    httplib::Response res;
    httplib::Error error;

    LOG_INFO("Sending stream request to OpenAI API");

    // Retry the initial connection with the upstream SessionRetry policy:
    // up to 5 retries for transient status codes (429, 5xx), retryable error
    // bodies, or network drops before any stream payload is received.
    // Backoff is exponential (2s base, x2) with one-sided jitter, honoring
    // Retry-After hints when the provider sends them.
    constexpr int kMaxStreamRetries = 5;
    bool send_success = false;

    auto stream_retry_after_ms = [&res, &send_success]() -> std::optional<long long> {
      if (send_success && res.has_header("retry-after-ms")) {
        try {
          long long ms = std::stoll(res.get_header_value("retry-after-ms"));
          if (ms > 0) return ms;
        } catch (...) {}
      }
      if (send_success && res.has_header("retry-after")) {
        try {
          long long sec = std::stoll(res.get_header_value("retry-after"));
          if (sec > 0) return sec * 1000;
        } catch (...) {}
      }
      return std::nullopt;
    };

    for (int attempt = 1; attempt <= kMaxStreamRetries + 1; ++attempt) {
      if (should_stop_) break;

      accumulated_data.clear();
      error = httplib::Error::Success;

      send_success = client.send(req, res, error);

      if (send_success && res.status == 200) {
        LOG_INFO("Stream completed successfully");
        break;
      }

      const bool is_network_error = !send_success;
      const bool is_overflow = send_success &&
          qcode::is_context_overflow_error(res.status, res.body);
      const bool is_retryable_status = send_success && !is_overflow &&
          (qcode::is_status_code_retryable(res.status) ||
           qcode::is_error_message_retryable(res.body));

      if ((is_network_error || is_retryable_status) &&
          attempt <= kMaxStreamRetries && !should_stop_) {
        // Upstream delay(): Retry-After hint wins verbatim; otherwise
        // exponential backoff with one-sided jitter capped at 30s.
        std::chrono::milliseconds delay_ms(2000);
        if (const auto hint = stream_retry_after_ms()) {
          delay_ms = std::chrono::milliseconds(std::min(*hint, 2147483647LL));
        } else {
          const double base =
              2000.0 * std::pow(2.0, attempt - 1);
          static thread_local std::mt19937 rng(std::random_device{}());
          std::uniform_real_distribution<double> dist(0.0, 1.0);
          delay_ms = std::chrono::milliseconds(static_cast<long long>(
              std::ceil(base * (1.0 + 0.25 * dist(rng)))));
          delay_ms = std::min(delay_ms, std::chrono::milliseconds(30000));
        }
        std::string err_desc = is_network_error
                                  ? httplib::to_string(error)
                                  : ("HTTP " + std::to_string(res.status));
        LOG_WARN(
            "Stream initial connection failed ({}), retrying attempt {}/{} in {} ms...",
            err_desc, attempt + 1, kMaxStreamRetries + 1, delay_ms.count());
        std::this_thread::sleep_for(delay_ms);
        continue;
      }

      // Permanent failure or exhausted retries
      if (is_network_error) {
        std::string error_msg = "Network error: " + httplib::to_string(error);
        LOG_ERROR("Failed to send stream request: {}", error_msg);
        push_event(create_error_event(error_msg));
      } else {
        LOG_ERROR("OpenAI stream API returned status {} - body: {}",
                  res.status, res.body);
        push_event(create_error_event("HTTP " + std::to_string(res.status) +
                                      " error: " + res.body));
      }
      break;
    }
  } catch (const std::exception& e) {
    LOG_ERROR("Exception in stream thread: {}", e.what());
    push_event(create_error_event(e.what()));
  }

  mark_complete();
  LOG_DEBUG("Stream thread exiting");
}

void OpenAIStreamImpl::push_event(StreamEvent event) {
  event_queue_.enqueue(std::move(event));
}

void OpenAIStreamImpl::push_finish_event_if_needed() {
  bool expected = false;
  if (finish_event_pushed_.compare_exchange_strong(expected, true)) {
    LOG_DEBUG("Pushing finish event to queue");
    event_queue_.enqueue(StreamEvent(kStreamEventTypeFinish));
  } else {
    LOG_DEBUG("Finish event already pushed, skipping");
  }
}

void OpenAIStreamImpl::mark_complete() {
  is_complete_ = true;  // Atomic write
}

}  // namespace openai
}  // namespace qcode
