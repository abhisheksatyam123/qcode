#include "openai_stream.h"

#include "openai_response_parser.h"
#include <qcode/core/ssl_config.h>
#include <qcode/core/logger.h>
#include "core/http_request_handler.h"
#include "providers/internal/opencode_zen_headers.h"
#include "core/response_utils.h"

#include <algorithm>
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
  return std::chrono::seconds(90);
}
constexpr auto kWakeInterval = std::chrono::milliseconds(50);
constexpr auto kConnectionTimeout = 30;  // seconds
constexpr auto kReadTimeout = 300;       // 5 minutes for long generations
constexpr std::size_t kMaxErrorBodyBytes = 64 * 1024;
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
  touch_activity();

  LOG_INFO("Launching stream thread for OpenAI API");

  // Serialize here so the thread owns one string instead of a deep copy of
  // the request JSON.
  auto sid = qcode::logger::thread_session_id();
  stream_thread_ = std::thread([this, url, headers, body = request_body.dump(),
                                sid]() mutable {
    qcode::logger::ScopedThreadSession bind(sid);
    run_stream(url, headers, std::move(body));
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

    // Block until a producer pushes, the stream completes, or the wake slice
    // elapses (the slice keeps timeout checks responsive). The predicate is
    // evaluated under wait_mutex_, and producers take that mutex before
    // notifying, so a push or completion cannot slip between the checks above
    // and the wait.
    std::unique_lock<std::mutex> lock(wait_mutex_);
    wait_cv_.wait_for(lock, kWakeInterval, [this] {
      return is_complete_ || event_queue_.size_approx() > 0;
    });
  }

  return event;
}

std::optional<StreamEvent> OpenAIStreamImpl::poll_event(
    std::chrono::milliseconds timeout) {
  StreamEvent event("");
  if (event_queue_.try_dequeue(event)) return event;
  {
    // Same handshake as get_next_event(): producers notify under wait_mutex_.
    std::unique_lock<std::mutex> lock(wait_mutex_);
    wait_cv_.wait_for(lock, timeout, [this] {
      return is_complete_ || event_queue_.size_approx() > 0;
    });
  }
  if (event_queue_.try_dequeue(event)) return event;

  // get_next_event()'s watchdog, measured from the last bytes received so
  // keep-alive comments and retry attempts count as progress.
  const auto idle =
      std::chrono::steady_clock::now().time_since_epoch() -
      std::chrono::steady_clock::duration(last_activity_.load());
  if (!is_complete_ && idle > event_timeout_) {
    LOG_ERROR("No stream data for {} seconds", event_timeout_.count());
    return StreamEvent(kStreamEventTypeError, "Timeout waiting for next event");
  }
  return std::nullopt;
}

bool OpenAIStreamImpl::has_more_events() const {
  // No locks needed - these are atomic operations
  return event_queue_.size_approx() > 0 || !is_complete_;
}

void OpenAIStreamImpl::stop_stream() {
  LOG_DEBUG("Stopping OpenAI stream");

  // A finished stream only needs joining (and may return its connection to
  // the pool). An unfinished one is cancelled now: shutting the socket down
  // unblocks a read that would otherwise wait out the read timeout.
  if (!is_complete_) {
    should_stop_ = true;  // Atomic write
    std::lock_guard<std::mutex> lock(cancel_mutex_);
    if (active_client_) active_client_->stop();
    cancel_cv_.notify_all();
  }

  std::lock_guard<std::mutex> lock(thread_mutex_);
  if (stream_thread_.joinable()) {
    LOG_DEBUG("Waiting for stream thread to finish");
    stream_thread_.join();
    LOG_INFO("OpenAI stream stopped successfully");
  }
}

void OpenAIStreamImpl::run_stream(const std::string& url,
                                  const httplib::Headers& headers,
                                  std::string body) {
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

  const std::string target =
      std::string(use_ssl ? "https://" : "http://") + host;
  try {
    auto cli = qcode::http::acquire_stream_client(target, kConnectionTimeout,
                                                  kReadTimeout);
    // stop_stream() cancels through active_client_; withdraw it on every
    // exit path before `cli` is pooled or destroyed.
    set_active_client(cli.get());
    struct ActiveClientGuard {
      OpenAIStreamImpl* self;
      ~ActiveClientGuard() { self->set_active_client(nullptr); }
    } active_client_guard{this};

    LOG_DEBUG(
        "Stream client ready with connection_timeout: {}s, read_timeout: {}s",
        kConnectionTimeout, kReadTimeout);

    std::string accumulated_data;
    int status = 0;          // of the current attempt
    std::string error_body;  // non-200 payload of the current attempt
    bool streamed = false;   // the current attempt delivered SSE data
    std::size_t chunks = 0;
    std::size_t bytes = 0;

    // Create request
    httplib::Request req;
    req.method = "POST";
    req.path = path;
    req.headers = headers;
    req.body = std::move(body);
    req.set_header("Content-Type", "application/json");
    if (qcode::providers::is_opencode_zen_url(host)) {
      qcode::providers::apply_opencode_zen_headers(req.headers);
    }

    LOG_DEBUG(
        "Stream request prepared - path: {}, body size: {} bytes", path,
        req.body.length());

    // Set content receiver for streaming response. A content receiver also
    // gets non-200 bodies (res.body stays empty), so the status is captured
    // first and an error payload is kept, capped, for the retry checks and
    // the error event instead of being parsed as SSE.
    req.response_handler = [&status](const httplib::Response& r) {
      status = r.status;
      return true;
    };
    req.content_receiver = [this, &accumulated_data, &status, &error_body,
                            &streamed, &chunks, &bytes](
                               const char* data, size_t data_length,
                               uint64_t /*offset*/, uint64_t /*total_length*/) {
      ++chunks;
      bytes += data_length;
      touch_activity();
      if (status != 200) {
        if (error_body.size() < kMaxErrorBodyBytes) {
          error_body.append(
              data, std::min(data_length, kMaxErrorBodyBytes - error_body.size()));
        }
        return !should_stop_;
      }

      // Accumulate data and process complete lines
      streamed = true;
      accumulated_data.append(data, data_length);

      // Scan complete lines by offset; drop the consumed prefix once per chunk.
      size_t start = 0;
      for (size_t pos; (pos = accumulated_data.find('\n', start)) != std::string::npos;
           start = pos + 1) {
        // Check if we should stop - atomic read, no lock needed
        if (should_stop_) {
          LOG_DEBUG(
              "Stream stop requested, ending content receiver");
          return false;
        }

        size_t end = pos;
        if (end > start && accumulated_data[end - 1] == '\r') --end;
        parse_sse_line(accumulated_data.substr(start, end - start));
      }
      accumulated_data.erase(0, start);

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
    bool stream_ok = false;  // complete 200 that was not aborted: reusable

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
      status = 0;
      error_body.clear();
      streamed = false;
      error = httplib::Error::Success;
      touch_activity();

      send_success = cli->send(req, res, error);

      if (send_success && res.status == 200) {
        LOG_INFO("Stream completed successfully");
        stream_ok = !should_stop_;
        break;
      }

      const bool is_network_error = !send_success;
      const bool is_overflow = send_success &&
          qcode::is_context_overflow_error(res.status, error_body);
      const bool is_retryable_status = send_success && !is_overflow &&
          (qcode::is_status_code_retryable(res.status) ||
           qcode::is_error_message_retryable(error_body));

      // A drop after SSE data arrived is not retried: its events are already
      // out, and a replay would repeat them.
      if ((is_network_error || is_retryable_status) && !streamed &&
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
        // The backoff counts as activity for poll_event()'s watchdog, and
        // stop_stream() ends it early.
        last_activity_ = (std::chrono::steady_clock::now() + delay_ms)
                             .time_since_epoch()
                             .count();
        std::unique_lock<std::mutex> lock(cancel_mutex_);
        cancel_cv_.wait_for(lock, delay_ms, [this] { return should_stop_.load(); });
        continue;
      }

      // Nobody reads an error after a cancel, and a drop after the finish
      // chunk loses at most the usage chunk.
      if (should_stop_ || finish_event_pushed_) break;

      // Permanent failure or exhausted retries
      if (is_network_error) {
        std::string error_msg = "Network error: " + httplib::to_string(error);
        LOG_ERROR("Failed to send stream request: {}", error_msg);
        push_event(create_error_event(error_msg));
      } else {
        LOG_ERROR("OpenAI stream API returned status {} - body: {}",
                  res.status, error_body);
        push_event(create_error_event("HTTP " + std::to_string(res.status) +
                                      " error: " + error_body));
      }
      break;
    }

    LOG_DEBUG("OpenAI stream done status={} chunks={} bytes={}", res.status,
              chunks, bytes);

    // Errors and aborts leave `cli` unpooled; its socket closes on destruction.
    set_active_client(nullptr);
    if (stream_ok && !should_stop_) {
      qcode::http::release_stream_client(target, std::move(cli));
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
  notify_consumer();
}

void OpenAIStreamImpl::notify_consumer() {
  // Taking the mutex (even briefly) orders this notify after any waiter's
  // predicate check, so the wakeup cannot be lost.
  {
    std::lock_guard<std::mutex> lock(wait_mutex_);
  }
  wait_cv_.notify_all();
}


void OpenAIStreamImpl::set_active_client(httplib::ClientImpl* cli) {
  std::lock_guard<std::mutex> lock(cancel_mutex_);
  active_client_ = cli;
}

void OpenAIStreamImpl::touch_activity() {
  last_activity_ = std::chrono::steady_clock::now().time_since_epoch().count();
}

void OpenAIStreamImpl::push_pending_tool_calls() {
  for (auto& tc : pending_tool_calls_) {
    if (tc.name.empty()) continue;
    std::optional<std::string> signature;
    if (!tc.thought_signature.empty()) signature = std::move(tc.thought_signature);
    push_event(StreamEvent::tool_call(
        std::move(tc.id), std::move(tc.name),
        tc.arguments.empty() ? "{}" : std::move(tc.arguments),
        std::move(signature)));
  }
  pending_tool_calls_.clear();
}

void OpenAIStreamImpl::push_finish_event_if_needed() {
  bool expected = false;
  if (finish_event_pushed_.compare_exchange_strong(expected, true)) {
    push_pending_tool_calls();
    LOG_DEBUG("Pushing finish event to queue");
    event_queue_.enqueue(StreamEvent(kStreamEventTypeFinish));
    notify_consumer();
  } else {
    LOG_DEBUG("Finish event already pushed, skipping");
  }
}

void OpenAIStreamImpl::mark_complete() {
  is_complete_ = true;  // Atomic write
  notify_consumer();
}

}  // namespace openai
}  // namespace qcode
