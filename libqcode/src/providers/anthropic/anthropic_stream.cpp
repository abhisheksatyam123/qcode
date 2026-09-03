#include "anthropic_stream.h"

#include <qcode/core/ssl_config.h>
#include <qcode/core/logger.h>

#include <chrono>
#include <cstdlib>
#include <sstream>
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
constexpr auto kSleepInterval = std::chrono::milliseconds(1);
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

  // Start streaming in a separate thread
  stream_thread_ = std::thread([this, url, headers, request_body]() {
    try {
      run_stream(url, headers, request_body);
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

    std::this_thread::sleep_for(kSleepInterval);
  }

  LOG_DEBUG("Dequeued event type: {}",
                        static_cast<int>(event.type));
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
                                     const nlohmann::json& request_body) {
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

  try {
    if (use_ssl) {
      httplib::SSLClient client(host);
      qcode::http::configure_client_tls(client, true);
      client.set_connection_timeout(30, 0);
      client.set_read_timeout(120, 0);

      auto result =
          client.Post(path, headers, request_body.dump(), "application/json");

      if (result && result->status == 200) {
        parse_sse_response(result->body);
      } else {
        handle_stream_error(result ? result->status : 0,
                            result ? result->body : "Connection failed");
      }
    } else {
      httplib::Client client(host);
      client.set_connection_timeout(30, 0);
      client.set_read_timeout(120, 0);

      auto result =
          client.Post(path, headers, request_body.dump(), "application/json");

      if (result && result->status == 200) {
        parse_sse_response(result->body);
      } else {
        handle_stream_error(result ? result->status : 0,
                            result ? result->body : "Connection failed");
      }
    }
  } catch (const std::exception& e) {
    LOG_ERROR("Stream request exception: {}", e.what());
    handle_stream_error(0, std::string("Request failed: ") + e.what());
  }

  mark_complete();
}

void AnthropicStreamImpl::parse_sse_response(const std::string& response) {
  LOG_DEBUG("Processing SSE response, size: {}", response.size());

  std::istringstream stream(response);
  std::string line;
  std::string event_data;

  while (std::getline(stream, line) && !stop_requested_) {
    if (line.empty()) {
      // Empty line signals end of event
      if (!event_data.empty()) {
        process_sse_event(event_data);
        event_data.clear();
      }
    } else if (line.starts_with("data: ")) {
      event_data = line.substr(6);
    }
  }

  LOG_DEBUG("SSE processing complete");
}

void AnthropicStreamImpl::process_sse_event(const std::string& data) {
  if (data == "[DONE]") {
    LOG_DEBUG("Received SSE [DONE] event");
    return;
  }

  try {
    auto json_event = nlohmann::json::parse(data);
    std::string event_type = json_event.value("type", "");

    LOG_DEBUG("Processing SSE event type: {}", event_type);

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
          std::string thinking = delta["thinking"].get<std::string>();
          push_event(StreamEvent::reasoning(thinking));
          LOG_DEBUG("Enqueued thinking delta: '{}'", thinking);
        } else if (delta_type == "signature_delta" &&
                   delta.contains("signature")) {
          std::string sig = delta["signature"].get<std::string>();
          push_event(StreamEvent::reasoning("", sig));
          LOG_DEBUG("Enqueued thinking signature");
        } else if (delta.contains("text")) {
          std::string text = delta["text"].get<std::string>();
          StreamEvent event(text);
          push_event(event);
          LOG_DEBUG("Enqueued text delta: '{}'", text);
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
}

void AnthropicStreamImpl::mark_complete() {
  stream_complete_ = true;

  // message_stop already pushed a finish with usage; only synthesize one
  // when the stream closed without it (avoids a duplicate empty usage).
  if (!stream_usage_seen_) {
    StreamEvent finish_event(kStreamEventTypeFinish);
    push_event(finish_event);
  }
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