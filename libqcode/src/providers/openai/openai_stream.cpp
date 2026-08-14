#include "openai_stream.h"

#include <qcode/http/ssl_config.h>
#include <qcode/logger/logger.h>
#include "http/http_request_handler.h"

#include <chrono>
#include <cstdlib>
#include <mutex>

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

    if (!client.send(req, res, error)) {
      std::string error_msg = "Network error: " + httplib::to_string(error);
      LOG_ERROR("Failed to send stream request: {}", error_msg);
      push_event(create_error_event(error_msg));
    } else if (res.status != 200) {
      LOG_ERROR("OpenAI stream API returned status {} - body: {}",
                            res.status, res.body);
      push_event(create_error_event("HTTP " + std::to_string(res.status) +
                                    " error: " + res.body));
    } else {
      LOG_INFO("Stream completed successfully");
    }
  } catch (const std::exception& e) {
    LOG_ERROR("Exception in stream thread: {}", e.what());
    push_event(create_error_event(e.what()));
  }

  mark_complete();
  LOG_DEBUG("Stream thread exiting");
}

void OpenAIStreamImpl::parse_sse_line(const std::string& line) {
  if (line.starts_with("data:")) {
    auto data = line.substr(5);
    if (!data.empty() && data.front() == ' ') data.erase(0, 1);

    LOG_DEBUG("Processing SSE line - data length: {}",
                          data.length());

    if (data == "[DONE]") {
      LOG_DEBUG("Received [DONE] signal, stream ending");
      push_finish_event_if_needed();
      mark_complete();
      return;
    }


    try {
      auto json = nlohmann::json::parse(data);

      const auto event_type = json.value("type", "");
      if (event_type == "response.output_text.delta") {
        push_event(StreamEvent(json.value("delta", "")));
        return;
      }
      if (event_type == "response.reasoning_text.delta") {
        push_event(StreamEvent::reasoning(json.value("delta", "")));
        return;
      }
      if (event_type == "response.completed" ||
          event_type == "response.incomplete") {
        Usage usage;
        const auto response = json.value("response", nlohmann::json::object());
        if (response.contains("usage")) {
          const auto& raw_usage = response["usage"];
          usage.prompt_tokens = raw_usage.value("input_tokens", 0);
          usage.completion_tokens = raw_usage.value("output_tokens", 0);
          usage.total_tokens = raw_usage.value("total_tokens", 0);
        }
        finish_event_pushed_ = true;
        push_event(StreamEvent(
            kStreamEventTypeFinish, usage,
            event_type == "response.incomplete" ? kFinishReasonLength
                                                  : kFinishReasonStop));
        return;
      }
      if (event_type == "error") {
        push_event(create_error_event(json.value("message", "Provider error")));
        return;
      }

      // Antigravity wraps every SSE chunk in { response: {...}, traceId,
      // metadata }. Unwrap so the Gemini/OpenAI branches below see the inner
      // payload (candidates / usageMetadata / choices / delta).
      if (protocol_ == StreamProtocol::kGeminiEnvelope) {
        if (json.contains("response") && json["response"].is_object()) {
          json = json["response"];
        }
      }

      // Surface provider errors (e.g. Antigravity quota/rate-limit) instead of
      // silently yielding an empty response.
      if (json.contains("error")) {
        const auto& err = json["error"];
        std::string msg;
        if (err.is_string()) {
          msg = err.get<std::string>();
        } else if (err.contains("message")) {
          msg = err["message"].get<std::string>();
        }
        if (!msg.empty()) {
          LOG_ERROR("Stream error from provider: {}", msg);
          if (msg.find("429") != std::string::npos ||
              msg.find("rate_limit") != std::string::npos ||
              msg.find("Rate limit") != std::string::npos ||
              msg.find("quota") != std::string::npos ||
              msg.find("RESOURCE_EXHAUSTED") != std::string::npos) {
            msg += "\n\n💡 Tip: Rate limit hit. Switch to high-throughput models via `/model gemini-3.7-flash` or `/model poolside/laguna-s-2.1:free` or `/model nvidia/nemotron-3.5-lightning:free`.";
          }
          push_event(create_error_event(msg));
          return;
        }
      }

      // Google Gemini / Antigravity SSE parsing
      if (protocol_ == StreamProtocol::kGeminiEnvelope &&
          json.contains("candidates")) {
        auto& candidates = json["candidates"];
        if (!candidates.empty()) {
          auto& cand = candidates[0];
          if (cand.contains("content") && cand["content"].contains("parts")) {
            auto& parts = cand["content"]["parts"];
            for (const auto& part : parts) {
              if (part.contains("text")) {
                const auto content = part["text"].get<std::string>();
                if (part.value("thought", false)) {
                  std::optional<std::string> signature;
                  if (part.contains("thoughtSignature")) {
                    signature = part["thoughtSignature"].get<std::string>();
                  }
                  push_event(StreamEvent::reasoning(content, signature));
                } else {
                  push_event(StreamEvent(content));
                }
              }
            }
          }
          
          if (cand.contains("finishReason")) {
            std::string reason_str = cand["finishReason"].get<std::string>();
            FinishReason finish_reason = kFinishReasonStop;
            if (reason_str == "STOP") finish_reason = kFinishReasonStop;
            else if (reason_str == "MAX_TOKENS") finish_reason = kFinishReasonLength;
            
            finish_event_pushed_ = true;
            
            if (json.contains("usageMetadata")) {
              auto& usage_meta = json["usageMetadata"];
              Usage usage;
              usage.prompt_tokens = usage_meta.value("promptTokenCount", 0);
              usage.completion_tokens = usage_meta.value("candidatesTokenCount", 0);
              usage.total_tokens = usage_meta.value("totalTokenCount", 0);
              usage.cached_prompt_tokens = usage_meta.value("cachedContentTokenCount", 0);
              push_event(StreamEvent(kStreamEventTypeFinish, usage, finish_reason));
            } else {
              push_event(StreamEvent(kStreamEventTypeFinish));
            }
          }
        }
        return;
      } else if (protocol_ == StreamProtocol::kGeminiEnvelope &&
                 json.contains("usageMetadata")) {
        auto& usage_meta = json["usageMetadata"];
        Usage usage;
        usage.prompt_tokens = usage_meta.value("promptTokenCount", 0);
        usage.completion_tokens = usage_meta.value("candidatesTokenCount", 0);
        usage.total_tokens = usage_meta.value("totalTokenCount", 0);
        usage.cached_prompt_tokens = usage_meta.value("cachedContentTokenCount", 0);
        finish_event_pushed_ = true;
        push_event(StreamEvent(kStreamEventTypeFinish, usage, kFinishReasonStop));
        return;
      }

      if (!json.contains("choices") || !json["choices"].is_array()) return;
      auto& choices = json["choices"];

      if (!choices.empty() && choices[0].contains("delta")) {
        auto& delta = choices[0]["delta"];
        if (delta.contains("content") && !delta["content"].is_null()) {
          std::string content = delta["content"].get<std::string>();
          LOG_DEBUG("Received content chunk - length: {}",
                                content.length());
          push_event(StreamEvent(content));
        }

        // o-series reasoning tokens (reasoning or structured reasoning_details)
        if (delta.contains("reasoning") && !delta["reasoning"].is_null()) {
          std::string reasoning = delta["reasoning"].get<std::string>();
          LOG_DEBUG("Received reasoning chunk - length: {}", reasoning.length());
          push_event(StreamEvent::reasoning(reasoning));
        } else if (delta.contains("reasoning_details") &&
                   delta["reasoning_details"].is_array()) {
          for (const auto& rd : delta["reasoning_details"]) {
            if (rd.is_object() && rd.value("type", "") == "text" &&
                rd.contains("text")) {
              std::string reasoning = rd["text"].get<std::string>();
              push_event(StreamEvent::reasoning(reasoning));
            }
          }
        }
      }

      // Check for finish_reason
      if (!choices.empty() && choices[0].contains("finish_reason") &&
          !choices[0]["finish_reason"].is_null()) {
        auto finish_reason_str = choices[0]["finish_reason"].get<std::string>();
        auto finish_reason = parse_finish_reason(finish_reason_str);

        LOG_DEBUG("Stream finished with reason: {}",
                              finish_reason_str);

        finish_event_pushed_ = true;

        if (json.contains("usage")) {
          auto usage = parse_usage(json["usage"]);
          LOG_INFO(
              "Stream completed - tokens used: {} prompt, {} completion, {} "
              "total",
              usage.prompt_tokens, usage.completion_tokens, usage.total_tokens);
          push_event(StreamEvent(kStreamEventTypeFinish, usage, finish_reason));
        } else {
          push_event(StreamEvent(kStreamEventTypeFinish));
        }
      }
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to parse SSE line: {} - Line content: {}",
                            e.what(), data);
    }
  } else if (!line.empty()) {
    LOG_DEBUG("Ignoring non-data SSE line: {}", line);
  }
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

StreamEvent OpenAIStreamImpl::create_error_event(const std::string& message) {
  LOG_DEBUG("Creating error event: {}", message);
  return StreamEvent(kStreamEventTypeError, message);
}

FinishReason OpenAIStreamImpl::parse_finish_reason(
    const std::string& reason_str) {
  if (reason_str == "stop") {
    return kFinishReasonStop;
  } else if (reason_str == "length") {
    return kFinishReasonLength;
  } else if (reason_str == "content_filter") {
    return kFinishReasonContentFilter;
  }
  return kFinishReasonStop;
}

Usage OpenAIStreamImpl::parse_usage(const nlohmann::json& usage_json) {
  Usage usage;
  usage.prompt_tokens = usage_json.value("prompt_tokens", usage_json.value("input_tokens", 0));
  usage.completion_tokens = usage_json.value("completion_tokens", usage_json.value("output_tokens", 0));
  usage.total_tokens = usage_json.value("total_tokens", usage.prompt_tokens + usage.completion_tokens);
  if (usage_json.contains("prompt_tokens_details") && usage_json["prompt_tokens_details"].is_object()) {
    usage.cached_prompt_tokens = usage_json["prompt_tokens_details"].value("cached_tokens", 0);
  } else if (usage_json.contains("cache_read_input_tokens")) {
    usage.cached_prompt_tokens = usage_json.value("cache_read_input_tokens", 0);
  }
  return usage;
}

}  // namespace openai
}  // namespace qcode
