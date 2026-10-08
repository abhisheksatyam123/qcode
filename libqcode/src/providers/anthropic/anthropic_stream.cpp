#include "anthropic_stream.h"
#include "anthropic_response_parser.h"

#include "core/http_request_handler.h"
#include <qcode/core/logger.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <random>
#include <cmath>

namespace {
std::chrono::seconds default_event_timeout() {
  if (const char* v = std::getenv("QCODE_STREAM_EVENT_TIMEOUT_SEC")) {
    try {
      const int secs = std::stoi(v);
      if (secs > 0) return std::chrono::seconds(secs);
    } catch (...) {}
  }
  return std::chrono::seconds(120);
}
constexpr auto kWakeInterval = std::chrono::milliseconds(50);
constexpr std::size_t kMaxErrorBodyBytes = 64 * 1024;
}  // namespace

namespace qcode {
namespace anthropic {

AnthropicStreamImpl::~AnthropicStreamImpl() {
  stop_stream();
}

void AnthropicStreamImpl::touch_activity() {
  last_activity_ = std::chrono::steady_clock::now().time_since_epoch().count();
}

void AnthropicStreamImpl::reset_attempt_state() {
  stream_usage_ = Usage{};
  stream_usage_seen_ = false;
  pending_tool_calls_.clear();
  finish_pushed_ = false;
  finish_reason_ = kFinishReasonStop;
  stop_reason_str_.clear();
  held_sse_error_.reset();
}

void AnthropicStreamImpl::set_active_client(httplib::ClientImpl* cli) {
  std::lock_guard<std::mutex> lock(cancel_mutex_);
  active_client_ = cli;
}

void AnthropicStreamImpl::start_stream(const std::string& url,
                                       const httplib::Headers& headers,
                                       const nlohmann::json& request_body) {
  LOG_DEBUG("Starting Anthropic stream to URL: {}", url);

  {
    std::lock_guard<std::mutex> lock(thread_mutex_);
    if (stream_thread_.joinable()) {
      LOG_DEBUG("Stream thread already running, not starting new one");
      return;  // Already running
    }
    stop_requested_ = false;
    stream_complete_ = false;
    content_pushed_ = false;
    reset_attempt_state();
    event_timeout_ = default_event_timeout();
    touch_activity();
  }

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
  if (event_queue_.try_dequeue(event)) return event;
  
  while (!event_queue_.try_dequeue(event)) {
    if (stream_complete_ && event_queue_.size_approx() == 0) {
      LOG_DEBUG("Stream complete and queue empty, returning empty event");
      return StreamEvent("");
    }

    const auto idle =
        std::chrono::steady_clock::now().time_since_epoch() -
        std::chrono::steady_clock::duration(last_activity_.load());
    if (last_activity_.load() != 0 && idle > event_timeout_) {
      LOG_ERROR("Timeout waiting for next stream event after {} seconds",
                event_timeout_.count());
      return StreamEvent(kStreamEventTypeError,
                         "Timeout waiting for next event");
    }

    std::unique_lock<std::mutex> lock(wait_mutex_);
    wait_cv_.wait_for(lock, kWakeInterval, [this] {
      return stream_complete_ || event_queue_.size_approx() > 0;
    });
  }

  return event;
}

std::optional<StreamEvent> AnthropicStreamImpl::poll_event(std::chrono::milliseconds timeout) {
  StreamEvent event("");
  if (event_queue_.try_dequeue(event)) return event;
  {
    std::unique_lock<std::mutex> lock(wait_mutex_);
    wait_cv_.wait_for(lock, timeout, [this] {
      return stream_complete_ || event_queue_.size_approx() > 0;
    });
  }
  if (event_queue_.try_dequeue(event)) return event;

  const auto idle =
      std::chrono::steady_clock::now().time_since_epoch() -
      std::chrono::steady_clock::duration(last_activity_.load());
  // last_activity_ == 0: no request started yet, nothing to time out.
  if (!stream_complete_ && last_activity_.load() != 0 && idle > event_timeout_) {
    LOG_ERROR("No stream data for {} seconds", event_timeout_.count());
    return StreamEvent(kStreamEventTypeError, "Timeout waiting for next event");
  }
  return std::nullopt;
}

bool AnthropicStreamImpl::has_more_events() const {
  return event_queue_.size_approx() > 0 || !stream_complete_;
}

void AnthropicStreamImpl::stop_stream() {
  LOG_DEBUG("Stopping Anthropic stream");
  if (!stream_complete_) {
    stop_requested_ = true;
    std::lock_guard<std::mutex> lock(cancel_mutex_);
    if (active_client_) {
      active_client_->stop();
    }
    cancel_cv_.notify_all();
  }
  if (stream_thread_.joinable()) {
    stream_thread_.join();
  }
}

void AnthropicStreamImpl::run_stream(const std::string& url,
                                     const httplib::Headers& headers,
                                     std::string body) {
  LOG_DEBUG("Performing stream request");

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

  const std::string target = std::string(use_ssl ? "https://" : "http://") + host;
  
  bool stream_ok = false;
  int final_status = 0;
  std::size_t total_chunks = 0;
  std::size_t total_bytes = 0;
  int attempts_used = 0;
  auto req_start = std::chrono::steady_clock::now();
  int ttfb_ms = -1;

  try {
    auto cli = qcode::http::acquire_stream_client(target, 30, 120);

    httplib::Request req;
    req.method = "POST";
    req.path = path;
    req.headers = headers;
    req.body = std::move(body);
    req.set_header("Content-Type", "application/json");

    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
      attempts_used = attempt + 1;
      reset_attempt_state();
      set_active_client(cli.get());

      int status = 0;
      std::string pending;
      std::string event_data;
      std::string error_body;
      std::size_t chunks = 0;
      std::size_t bytes = 0;
      bool first_byte_received = false;
      auto attempt_start = std::chrono::steady_clock::now();

      req.response_handler = [&](const httplib::Response& res) {
        status = res.status;
        return true;
      };
      req.content_receiver = [&](const char* data, size_t len, uint64_t, uint64_t) {
        touch_activity();
        if (stop_requested_) return false;
        
        if (!first_byte_received) {
            first_byte_received = true;
            ttfb_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - attempt_start).count();
        }

        ++chunks;
        bytes += len;
        total_chunks++;
        total_bytes += len;
        
        if (status != 200) {
          if (error_body.size() < kMaxErrorBodyBytes) {
            error_body.append(data, std::min(len, kMaxErrorBodyBytes - error_body.size()));
          }
          return true;
        }
        pending.append(data, len);
        std::size_t start = 0;
        for (std::size_t nl; (nl = pending.find('\n', start)) != std::string::npos; start = nl + 1) {
          if (stop_requested_) return false;
          std::string_view line(pending.data() + start, nl - start);
          if (line.ends_with('\r')) line.remove_suffix(1);
          if (line.empty()) {
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
      touch_activity();

      const bool sent = cli->send(req, res, error);
      final_status = status;

      const bool sse_overloaded = held_sse_error_.has_value();
      if (sent && status == 200 && !sse_overloaded) {
        stream_ok = !stop_requested_;
        break;
      }

      bool is_network_error = !sent;
      bool is_overloaded = sse_overloaded;
      if (sent && status != 200) {
         try {
           auto j = nlohmann::json::parse(error_body);
           if (j.contains("error") && j["error"].contains("type")) {
             auto type = j["error"]["type"].get<std::string>();
             if (type == "overloaded_error" || type == "api_error") {
               is_overloaded = true;
             }
           }
         } catch (...) {}
      }
      
      bool retryable_http = (status == 429 || status == 500 || status == 502 || status == 503 || status == 504 || status == 529 || is_overloaded);

      if ((is_network_error || retryable_http) && !content_pushed_ && attempt < max_retries_ && !stop_requested_) {
        std::chrono::milliseconds delay_ms(2000);
        if (sent && res.has_header("Retry-After")) {
           try {
             int secs = std::stoi(res.get_header_value("Retry-After"));
             if (secs > 0) delay_ms = std::min<std::chrono::milliseconds>(std::chrono::seconds(secs), std::chrono::seconds(30));
           } catch (...) {}
        } else {
           const double base = 2000.0 * std::pow(2.0, attempt);
           static thread_local std::mt19937 rng(std::random_device{}());
           std::uniform_real_distribution<double> dist(0.0, 1.0);
           delay_ms = std::chrono::milliseconds(static_cast<long long>(std::ceil(base * (1.0 + 0.25 * dist(rng)))));
           delay_ms = std::min(delay_ms, std::chrono::milliseconds(30000));
        }

        std::string err_desc = sse_overloaded ? *held_sse_error_
                               : is_network_error ? httplib::to_string(error)
                                                  : ("HTTP " + std::to_string(status));
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - req_start).count();
        LOG_WARN("[anthropic] stream attempt {}/{} failed ({}) after {} ms total; retrying in {} ms",
                 attempt + 1, max_retries_ + 1, err_desc, elapsed_ms, delay_ms.count());

        last_activity_ = (std::chrono::steady_clock::now() + delay_ms).time_since_epoch().count();
        std::unique_lock<std::mutex> lock(cancel_mutex_);
        cancel_cv_.wait_for(lock, delay_ms, [this] { return stop_requested_.load(); });
        
        continue;
      }

      if (stop_requested_) break;

      if (sse_overloaded) {
        push_event(create_error_event(*held_sse_error_));
      } else if (is_network_error) {
        std::string error_msg = "Network error: " + httplib::to_string(error);
        LOG_ERROR("Failed to send stream request: {}", error_msg);
        push_event(create_error_event(error_msg));
      } else {
        LOG_ERROR("Anthropic stream API returned status {} - body: {}", status, error_body);
        push_event(create_error_event("HTTP " + std::to_string(status) + " error: " + error_body));
      }
      break;
    }

    set_active_client(nullptr);
    if (stream_ok && !stop_requested_) {
      qcode::http::release_stream_client(target, std::move(cli));
    }
  } catch (const std::exception& e) {
    LOG_ERROR("Stream request exception: {}", e.what());
    handle_stream_error(0, std::string("Request failed: ") + e.what());
  }

  auto req_end = std::chrono::steady_clock::now();
  int total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(req_end - req_start).count();

  LOG_INFO("[anthropic] stream done status={} ttfb_ms={} total_ms={} bytes={} stop={} aborted={} out_tokens={} thinking_tokens={} attempts={}",
           final_status, ttfb_ms, total_ms, total_bytes,
           stop_reason_str_.empty() ? "-" : stop_reason_str_, stop_requested_.load(), 
           stream_usage_.completion_tokens, stream_usage_.reasoning_completion_tokens, attempts_used);

  mark_complete();
}

void AnthropicStreamImpl::process_sse_event(const std::string& data) {
  if (data == "[DONE]") {
    return;
  }

  try {
    auto json_event = nlohmann::json::parse(data);
    std::string event_type = json_event.value("type", "");

    if (event_type == "ping") {
        return;
    } else if (event_type == "error") {
        if (json_event.contains("error")) {
            auto err = json_event["error"];
            std::string msg = err.value("message", "Unknown error");
            std::string typ = err.value("type", "unknown");
            std::string text = "Anthropic stream error " + typ + ": " + msg;
            if (!content_pushed_ && (typ == "overloaded_error" || typ == "api_error")) {
                held_sse_error_ = std::move(text);  // run_stream retries
            } else {
                push_event(create_error_event(text));
            }
        }
        return;
    } else if (event_type == "message_start") {
      if (json_event.contains("message") && json_event["message"].contains("usage")) {
        const auto& usage = json_event["message"]["usage"];
        const auto in = normalize_input_usage(usage);
        stream_usage_.prompt_tokens = in.prompt_tokens;
        stream_usage_.cached_prompt_tokens = in.cached_prompt_tokens;
        stream_usage_.total_tokens = stream_usage_.prompt_tokens + stream_usage_.completion_tokens;
        stream_usage_seen_ = true;
      }
      return;
    } else if (event_type == "content_block_start") {
      if (json_event.contains("content_block") && json_event.contains("index")) {
          int index = json_event["index"].get<int>();
          auto cb = json_event["content_block"];
          if (cb.value("type", "") == "tool_use") {
              PendingToolCall pt;
              pt.id = cb.value("id", "");
              pt.name = cb.value("name", "");
              if (cb.contains("input") && cb["input"].is_object() && !cb["input"].empty()) {
                  pt.seed = cb["input"].dump();
              }
              pending_tool_calls_[index] = pt;
          } else if (cb.value("type", "") == "redacted_thinking") {
              if (cb.contains("data")) {
                  push_event(StreamEvent::reasoning("", cb["data"].get<std::string>()));
                  content_pushed_ = true;
              }
          }
      }
      return;
    } else if (event_type == "content_block_delta") {
      if (json_event.contains("delta") && json_event.contains("index")) {
        const auto& delta = json_event["delta"];
        int index = json_event["index"].get<int>();
        std::string delta_type = delta.value("type", "");

        if (delta_type == "input_json_delta") {
            if (pending_tool_calls_.count(index)) {
                pending_tool_calls_[index].arguments += delta.value("partial_json", "");
            }
        } else if (delta_type == "thinking_delta" && delta.contains("thinking")) {
          push_event(StreamEvent::reasoning(delta["thinking"].get<std::string>()));
          content_pushed_ = true;
        } else if (delta_type == "signature_delta" && delta.contains("signature")) {
          push_event(StreamEvent::reasoning("", delta["signature"].get<std::string>()));
          content_pushed_ = true;
        } else if (delta.contains("text")) {
          push_event(StreamEvent(delta["text"].get<std::string>()));
          content_pushed_ = true;
        }
      }
    } else if (event_type == "content_block_stop") {
      if (json_event.contains("index")) {
          int index = json_event["index"].get<int>();
          if (pending_tool_calls_.count(index)) {
              auto pt = pending_tool_calls_[index];
              pending_tool_calls_.erase(index);
              // Streamed deltas carry the full input; the start block's input is only
              // authoritative when no deltas came.
              std::string args = !pt.arguments.empty() ? pt.arguments
                                 : !pt.seed.empty() ? pt.seed : "{}";
              push_event(StreamEvent::tool_call(pt.id, pt.name, args));
              content_pushed_ = true;
          }
      }
      return;
    } else if (event_type == "message_delta") {
      FinishReason stop_reason = kFinishReasonStop;
      bool has_stop = false;
      if (json_event.contains("delta") && json_event["delta"].contains("stop_reason")) {
          auto sr = json_event["delta"]["stop_reason"];
          if (sr.is_string()) {
              std::string srs = sr.get<std::string>();
              stop_reason_str_ = srs;
              if (srs == "tool_use") stop_reason = kFinishReasonToolCalls;
              else if (srs == "max_tokens") stop_reason = kFinishReasonLength;
              has_stop = true;
          }
      }

      if (json_event.contains("usage")) {
        const auto& usage = json_event["usage"];
        const int out = usage.value("output_tokens", 0);
        if (out > 0) stream_usage_.completion_tokens = out;
        const auto in = normalize_input_usage(usage);
        if (in.present && in.prompt_tokens > 0) {
          stream_usage_.prompt_tokens = in.prompt_tokens;
          stream_usage_.cached_prompt_tokens = in.cached_prompt_tokens;
        }
        if (usage.contains("output_tokens_details") && usage["output_tokens_details"].is_object()) {
          const auto& det = usage["output_tokens_details"];
          const int think = det.value("reasoning_tokens", det.value("thinking_tokens", 0));
          if (think > 0) stream_usage_.reasoning_completion_tokens = think;
        }
        const int think_flat = usage.value("thinking_tokens", 0);
        if (think_flat > 0) stream_usage_.reasoning_completion_tokens = think_flat;
        stream_usage_.total_tokens = stream_usage_.prompt_tokens + stream_usage_.completion_tokens;
        stream_usage_seen_ = true;
      }
      
      if (has_stop) finish_reason_ = stop_reason;
      return;
    } else if (event_type == "message_stop") {
      // Usage is final now (message_delta carries cumulative output tokens).
      stream_usage_.total_tokens =
          stream_usage_.prompt_tokens + stream_usage_.completion_tokens;
      push_event(StreamEvent(kStreamEventTypeFinish, stream_usage_, finish_reason_));
      finish_pushed_ = true;
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
  {
    std::lock_guard<std::mutex> lock(wait_mutex_);
  }
  wait_cv_.notify_all();
}

void AnthropicStreamImpl::mark_complete() {
  // A stream that closed without message_stop still reports what it saw.
  if (!finish_pushed_ && !stop_requested_) {
    stream_usage_.total_tokens =
        stream_usage_.prompt_tokens + stream_usage_.completion_tokens;
    push_event(StreamEvent(kStreamEventTypeFinish, stream_usage_, finish_reason_));
    finish_pushed_ = true;
  }
  stream_complete_ = true;
  notify_consumer();
}

StreamEvent AnthropicStreamImpl::create_error_event(const std::string& message) {
  return StreamEvent(kStreamEventTypeError, message);
}

void AnthropicStreamImpl::handle_stream_error(int status_code, const std::string& error_body) {
  LOG_ERROR("Stream error - status: {}, body: {}", status_code, error_body);

  StreamEvent error_event(kStreamEventTypeError,
      "Stream error (" + std::to_string(status_code) + "): " + error_body);
  push_event(error_event);
}

}  // namespace anthropic
}  // namespace qcode
