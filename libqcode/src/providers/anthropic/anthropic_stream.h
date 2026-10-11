#pragma once

#include <qcode/core/stream_result.h>

#include <atomic>
#include <concurrentqueue.h>
#include <condition_variable>
#include <httplib.h>
#include <mutex>
#include <thread>
#include <map>
#include <optional>

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
  std::optional<StreamEvent> poll_event(std::chrono::milliseconds timeout) override;
  StreamDiagnostics get_diagnostics() const override;


  // Override the idle stream timeout (defaults to 120s; env
  // QCODE_STREAM_EVENT_TIMEOUT_SEC overrides at stream start).
  void set_event_timeout(std::chrono::seconds t) { event_timeout_ = t; }
  // Retries of a failed stream start (before any content). Default 4.
  void set_max_retries(int n) { max_retries_ = n < 0 ? 0 : n; }

  // Test hook: parse one SSE `data:` payload as if it came off the wire.
  void feed_sse_for_test(const std::string& data) { process_sse_event(data); }

 private:

  std::chrono::seconds event_timeout_{30};
  void run_stream(const std::string& url,
                  const httplib::Headers& headers,
                  std::string body);
  void process_sse_event(const std::string& data);
  void push_event(const StreamEvent& event);
  void mark_complete();
  // Wakes a consumer blocked in get_next_event(). Called after every enqueue,
  // on completion and on stop.
  void notify_consumer();

  // Accumulated usage across message_start / message_delta events.
  Usage stream_usage_{};
  bool stream_usage_seen_ = false;
  bool content_pushed_ = false;
  int max_retries_ = 4;
  bool finish_pushed_ = false;
  FinishReason finish_reason_ = kFinishReasonStop;
  std::string stop_reason_str_;
  // Retryable in-stream error (overloaded_error/api_error) seen before any
  // content: held back so run_stream can retry instead of surfacing it.
  std::optional<std::string> held_sse_error_;
  void reset_attempt_state();

  // Pending tool calls by block index
  struct PendingToolCall {
    std::string id;
    std::string name;
    std::string seed;       // content_block_start input (usually {})
    std::string arguments;  // concatenated input_json_delta chunks
  };
  std::map<int, PendingToolCall> pending_tool_calls_;

  // Retry backoff and cancellation
  std::mutex cancel_mutex_;
  std::condition_variable cancel_cv_;
  httplib::ClientImpl* active_client_ = nullptr;
  void set_active_client(httplib::ClientImpl* cli);

  // When the stream last received bytes or started an attempt
  std::atomic<std::chrono::steady_clock::rep> last_activity_{0};
  void touch_activity();

  // Helper functions
  StreamEvent create_error_event(const std::string& message);
  void handle_stream_error(int status_code, const std::string& error_body);

  moodycamel::ConcurrentQueue<StreamEvent> event_queue_;
  std::mutex wait_mutex_;  // guards only the sleep/wake handshake
  std::condition_variable wait_cv_;
  std::thread stream_thread_;
  std::mutex thread_mutex_;  // serializes start_stream / double-start guard
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> stream_complete_{false};

  // Diagnostic state
  mutable std::mutex diag_mutex_;
  std::string diag_endpoint_;
  std::string diag_model_;
  size_t diag_request_bytes_ = 0;
  std::atomic<int> diag_attempt_{1};
  std::atomic<int> diag_max_attempts_{5};
  std::atomic<int> diag_status_{0};
  std::atomic<bool> diag_in_backoff_{false};
  std::atomic<long long> diag_backoff_ms_{0};
  std::atomic<std::chrono::steady_clock::rep> diag_backoff_until_{0};
  std::atomic<bool> diag_headers_received_{false};
  std::atomic<size_t> diag_chunks_{0};
  std::atomic<size_t> diag_bytes_{0};
  std::atomic<std::chrono::steady_clock::rep> diag_first_chunk_time_{0};
  std::atomic<std::chrono::steady_clock::rep> diag_last_chunk_time_{0};
  std::atomic<std::chrono::steady_clock::rep> diag_send_start_time_{0};
};

}  // namespace anthropic
}  // namespace qcode