#pragma once

#include <qcode/core/stream_result.h>

#include <atomic>
#include <concurrentqueue.h>
#include <httplib.h>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>
#include <chrono>

namespace qcode {
namespace anthropic {

class AnthropicStreamImpl : public internal::StreamResultImpl {
 public:
  AnthropicStreamImpl() = default;
  ~AnthropicStreamImpl();

  // Non-copyable, non-movable for thread safety
  AnthropicStreamImpl(const AnthropicStreamImpl&) = delete;
  AnthropicStreamImpl& operator=(const AnthropicStreamImpl&) = delete;
  AnthropicStreamImpl(AnthropicStreamImpl&&) = delete;
  AnthropicStreamImpl& operator=(AnthropicStreamImpl&&) = delete;

  void start_stream(const std::string& url,
                    const httplib::Headers& headers,
                    const nlohmann::json& request_body);

  StreamEvent get_next_event() override;
  bool has_more_events() const override;
  void stop_stream() override;

  // Override the per-event stream timeout (defaults to 30s; env
  // QCODE_STREAM_EVENT_TIMEOUT_SEC overrides at stream start).
  void set_event_timeout(std::chrono::seconds t) { event_timeout_ = t; }

 private:
  std::chrono::seconds event_timeout_{30};
  void run_stream(const std::string& url,
                  const httplib::Headers& headers,
                  const nlohmann::json& request_body);
  void parse_sse_response(const std::string& response);
  void process_sse_event(const std::string& data);
  void push_event(const StreamEvent& event);
  void mark_complete();

  // Accumulated usage across message_start / message_delta events.
  Usage stream_usage_{};
  bool stream_usage_seen_ = false;

  // Helper functions
  StreamEvent create_error_event(const std::string& message);
  void handle_stream_error(int status_code, const std::string& error_body);

  moodycamel::ConcurrentQueue<StreamEvent> event_queue_;
  std::thread stream_thread_;
  std::mutex thread_mutex_;  // serializes start_stream / double-start guard
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> stream_complete_{false};
};

}  // namespace anthropic
}  // namespace qcode