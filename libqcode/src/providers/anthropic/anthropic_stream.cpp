#include "anthropic_stream.h"

#include "core/http_request_handler.h"
#include <qcode/core/logger.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>

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
constexpr auto kWakeInterval = std::chrono::milliseconds(50);
constexpr std::size_t kMaxErrorBodyBytes = 64 * 1024;
}  // namespace

namespace qcode {
namespace anthropic {

AnthropicStreamImpl::~AnthropicStreamImpl() {
  stop_stream();
}

void AnthropicStreamImpl::start_stream(const std::string& url,
                                       const httplib::Headers& headers,
                                       const nlohmann::json& request_body) {
  LOG_DEBUG("Starting Anthropic stream to URL: {}", url);

  // Mirror OpenAIStreamImpl: serialize start and refuse a second concurrent
  // start. Also clear the stop/complete flags left set by a prior stop_stream()
  // so a fresh stream does not immediately bail out.
  {
    std::lock_guard<std::mutex> lock(thread_mutex_);
    if (stream_thread_.joinable()) {
      LOG_DEBUG("Stream thread already running, not starting new one");
      return;  // Already running
    }
    stop_requested_ = false;
    stream_complete_ = false;
    stream_usage_ = Usage{};
    stream_usage_seen_ = false;
    event_timeout_ = default_event_timeout();
  }

  // Start streaming in a separate thread. The body is serialized here so the
  // thread owns one string instead of a deep copy of the request JSON.
  auto sid = qcode::logger::thread_session_id();
  stream_thread_ = std::thread([this, url, headers, body = request_body.dump(),
                                sid]() mutable {
    qcode::logger::ScopedThreadSession bind(sid);
    try {
      run_stream(url, headers, std::move(body));
    } catch (const std::exception& e) {
      LOG_ERROR("Stream thread exception: {}", e.what());
      StreamEvent error_event(kStreamEventTypeError,
                              std::string("Stream error: ") + e.what());
      push_event(error_event);
      mark_complete();
    }
  });
}

StreamEvent AnthropicStreamImpl::get_next_event() {
  StreamEvent event("");
  auto start_time = std::chrono::steady_clock::now();

  while (!event_queue_.try_dequeue(event)) {
    if (stream_complete_ && event_queue_.size_approx() == 0) {
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
      return stream_complete_ || event_queue_.size_approx() > 0;
    });
  }

  return event;
}

bool AnthropicStreamImpl::has_more_events() const {
  return event_queue_.size_approx() > 0 || !stream_complete_;
}

void AnthropicStreamImpl::stop_stream() {
  LOG_DEBUG("Stopping Anthropic stream");
  stop_requested_ = true;
  if (stream_thread_.joinable()) {
    stream_thread_.join();
  }
}

void AnthropicStreamImpl::run_stream(const std::string& url,
                                     const httplib::Headers& headers,
                                     std::string body) {
  LOG_DEBUG("Performing stream request");

  // Parse URL to extract host and path
  std::string host, path;
  bool use_ssl = true;

  if (url.starts_with("https://")) {
    host = url.substr(8);
    use_ssl = true;
  } else if (url.starts_with("http://")) {
    host = url.substr(7);
    use_ssl = false;
  }

  if (auto pos = host.find('/'); pos != std::string::npos) {
    path = host.substr(pos);
    host = host.substr(0, pos);
  } else {
    path = "/v1/messages";
  }

  LOG_DEBUG("Stream host: {}, path: {}, SSL: {}", host, path,
                        use_ssl);

  const std::string target =
      std::string(use_ssl ? "https://" : "http://") + host;
  try {
    auto cli = qcode::http::acquire_stream_client(target, 30, 120);

    httplib::Request req;
    req.method = "POST";
    req.path = path;
    req.headers = headers;
    req.body = std::move(body);
    req.set_header("Content-Type", "application/json");

    // Parse SSE as it arrives so each delta reaches the consumer at once
    // instead of after the whole generation. A non-200 body is an error
    // payload, not SSE: keep it (capped) for the error event. Returning false on stop
    // cancels the read, so stop_stream() does not wait for the response.
    int status = 0;
    std::string pending;     // bytes after the last complete line
    std::string event_data;  // payload of the event being read
    std::string error_body;
    std::size_t chunks = 0;
    std::size_t bytes = 0;
    req.response_handler = [&status](const httplib::Response& res) {
      status = res.status;
      return true;
    };
    req.content_receiver = [&](const char* data, size_t len,
                               uint64_t /*offset*/, uint64_t /*total*/) {
      if (stop_requested_) return false;
      ++chunks;
      bytes += len;
      if (status != 200) {
        if (error_body.size() < kMaxErrorBodyBytes) {
          error_body.append(
              data, std::min(len, kMaxErrorBodyBytes - error_body.size()));
        }
        return true;
      }
      pending.append(data, len);
      std::size_t start = 0;
      for (std::size_t nl; (nl = pending.find('\n', start)) != std::string::npos;
           start = nl + 1) {
        if (stop_requested_) return false;
        std::string_view line(pending.data() + start, nl - start);
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (line.empty()) {
          // Empty line signals end of event
          if (!event_data.empty()) {
            process_sse_event(event_data);
            event_data.clear();
          }
        } else if (line.starts_with("data: ")) {
          event_data.assign(line.substr(6));
        }
      }
      pending.erase(0, start);
      return true;
    };

    httplib::Response res;
    httplib::Error error = httplib::Error::Success;
    const bool sent = cli->send(req, res, error);
    LOG_DEBUG("Anthropic stream done status={} chunks={} bytes={} stopped={}",
              status, chunks, bytes, stop_requested_.load());

    const bool ok = sent && res.status == 200;
    if (!ok && !stop_requested_) {
      handle_stream_error(sent ? res.status : 0,
                          sent ? error_body
                               : "Connection failed (" +
                                     httplib::to_string(error) + ")");
    }
    // Pool only a complete 200 that was not aborted; everything else closes.
    if (ok && !stop_requested_) {
      qcode::http::release_stream_client(target, std::move(cli));
    }
  } catch (const std::exception& e) {
    LOG_ERROR("Stream request exception: {}", e.what());
    handle_stream_error(0, std::string("Request failed: ") + e.what());
  }

  mark_complete();
}

void AnthropicStreamImpl::process_sse_event(const std::string& data) {
  if (data == "[DONE]") {
    LOG_DEBUG("Received SSE [DONE] event");
    return;
  }

  try {
    auto json_event = nlohmann::json::parse(data);
    std::string event_type = json_event.value("type", "");

    if (event_type == "message_start") {
      // message_start.message.usage carries input_tokens (+ cache hits).
      if (json_event.contains("message") && json_event["message"].contains("usage")) {
        const auto& usage = json_event["message"]["usage"];
        stream_usage_.prompt_tokens = usage.value("input_tokens", stream_usage_.prompt_tokens);
        const int cached = usage.value("cache_read_input_tokens", 0);
        if (cached > 0) stream_usage_.cached_prompt_tokens = cached;
        stream_usage_.total_tokens =
            stream_usage_.prompt_tokens + stream_usage_.completion_tokens;
        stream_usage_seen_ = true;
      }
      return;
    } else if (event_type == "content_block_start") {
      // Start of content block
      return;
    } else if (event_type == "content_block_delta") {
      // Text and/or thinking deltas
      if (json_event.contains("delta")) {
        const auto& delta = json_event["delta"];
        std::string delta_type = delta.value("type", "");
        if (delta_type == "thinking_delta" && delta.contains("thinking")) {
          push_event(StreamEvent::reasoning(delta["thinking"].get<std::string>()));
        } else if (delta_type == "signature_delta" &&
                   delta.contains("signature")) {
          push_event(StreamEvent::reasoning("", delta["signature"].get<std::string>()));
        } else if (delta.contains("text")) {
          push_event(StreamEvent(delta["text"].get<std::string>()));
        }
      }
    } else if (event_type == "content_block_stop") {
      // End of content block
      return;
    } else if (event_type == "message_delta") {
      // message_delta.usage carries output_tokens (+ delta stop reason).
      if (json_event.contains("usage")) {
        const auto& usage = json_event["usage"];
        const int out = usage.value("output_tokens", 0);
        if (out > 0) stream_usage_.completion_tokens = out;
        if (usage.contains("output_tokens_details") && usage["output_tokens_details"].is_object()) {
          const auto& det = usage["output_tokens_details"];
          const int think = det.value("reasoning_tokens", det.value("thinking_tokens", 0));
          if (think > 0) stream_usage_.reasoning_completion_tokens = think;
        }
        const int think_flat = usage.value("thinking_tokens", 0);
        if (think_flat > 0) stream_usage_.reasoning_completion_tokens = think_flat;
        stream_usage_.total_tokens =
            stream_usage_.prompt_tokens + stream_usage_.completion_tokens;
        stream_usage_seen_ = true;
      }
      return;
    } else if (event_type == "message_stop") {
      // End of message: report accumulated usage so thinking/cache badges work.
      stream_usage_.total_tokens =
          stream_usage_.prompt_tokens + stream_usage_.completion_tokens;
      StreamEvent event(kStreamEventTypeFinish, stream_usage_, kFinishReasonStop);
      push_event(event);

      LOG_DEBUG("Enqueued finish event prompt={} cached={} completion={} thinking={}",
                stream_usage_.prompt_tokens, stream_usage_.cached_prompt_tokens,
                stream_usage_.completion_tokens,
                stream_usage_.reasoning_completion_tokens);
    }
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to parse SSE event: {}", e.what());
  }
}

void AnthropicStreamImpl::push_event(const StreamEvent& event) {
  event_queue_.enqueue(event);
  notify_consumer();
}

void AnthropicStreamImpl::notify_consumer() {
  // Taking the mutex (even briefly) orders this notify after any waiter's
  // predicate check, so the wakeup cannot be lost.
  {
    std::lock_guard<std::mutex> lock(wait_mutex_);
  }
  wait_cv_.notify_all();
}


void AnthropicStreamImpl::mark_complete() {
  stream_complete_ = true;

  // message_stop already pushed a finish with usage; only synthesize one
  // when the stream closed without it (avoids a duplicate empty usage).
  if (!stream_usage_seen_) {
    StreamEvent finish_event(kStreamEventTypeFinish);
    push_event(finish_event);
  }
  notify_consumer();
}

StreamEvent AnthropicStreamImpl::create_error_event(
    const std::string& message) {
  return StreamEvent(kStreamEventTypeError, message);
}

void AnthropicStreamImpl::handle_stream_error(int status_code,
                                              const std::string& error_body) {
  LOG_ERROR("Stream error - status: {}, body: {}", status_code,
                        error_body);

  StreamEvent error_event(
      kStreamEventTypeError,
      "Stream error (" + std::to_string(status_code) + "): " + error_body);
  push_event(error_event);
}

}  // namespace anthropic
}  // namespace qcode