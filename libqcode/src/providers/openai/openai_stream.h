#pragma once


#include <qcode/core/stream_result.h>

#include <atomic>
#include <concurrentqueue.h>
#include <condition_variable>
#include <httplib.h>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>
#include <chrono>

namespace qcode {
namespace openai {

enum class StreamProtocol { kOpenAI, kGeminiEnvelope };

class OpenAIStreamImpl : public internal::StreamResultImpl {
 public:
  explicit OpenAIStreamImpl(
      StreamProtocol protocol = StreamProtocol::kOpenAI)
      : protocol_(protocol) {}
  ~OpenAIStreamImpl();

  // Non-copyable, non-movable for thread safety
  OpenAIStreamImpl(const OpenAIStreamImpl&) = delete;
  OpenAIStreamImpl& operator=(const OpenAIStreamImpl&) = delete;
  OpenAIStreamImpl(OpenAIStreamImpl&&) = delete;
  OpenAIStreamImpl& operator=(OpenAIStreamImpl&&) = delete;

  void start_stream(const std::string& url,
                    const httplib::Headers& headers,
                    const nlohmann::json& request_body);

  StreamEvent get_next_event() override;
  bool has_more_events() const override;
  // Cancels an unfinished request (its socket is shut down, a retry backoff
  // ends) and joins the stream thread.
  void stop_stream() override;
  std::optional<StreamEvent> poll_event(
      std::chrono::milliseconds timeout) override;

  // Override the per-event stream timeout (defaults to 30s; env
  // QCODE_STREAM_EVENT_TIMEOUT_SEC overrides at stream start).
  void set_event_timeout(std::chrono::seconds t) { event_timeout_ = t; }

#ifdef QCODE_TESTING
  // Allow unit tests to drive the SSE parser directly.
  void test_parse_sse_line(const std::string& line) { parse_sse_line(line); }
#endif

 private:
  std::chrono::seconds event_timeout_{30};
  void run_stream(const std::string& url,
                  const httplib::Headers& headers,
                  std::string body);
  void parse_sse_line(const std::string& line);
  void push_event(StreamEvent event);
  void push_pending_tool_calls();
  void push_finish_event_if_needed();
  void mark_complete();
  void set_active_client(httplib::ClientImpl* cli);
  void touch_activity();
  // Wakes a consumer blocked in get_next_event(). Called after every enqueue,
  // on completion and on stop.
  void notify_consumer();

  // Helper functions
  StreamEvent create_error_event(const std::string& message);
  FinishReason parse_finish_reason(const std::string& reason_str);
  Usage parse_usage(const nlohmann::json& usage_json);

  moodycamel::ConcurrentQueue<StreamEvent> event_queue_;
  std::mutex wait_mutex_;  // guards only the sleep/wake handshake
  std::condition_variable wait_cv_;
  std::thread stream_thread_;
  std::mutex thread_mutex_;
  std::atomic<bool> is_complete_{false};
  std::atomic<bool> should_stop_{false};
  std::atomic<bool> finish_event_pushed_{false};
  bool usage_reported_ = false;  // stream thread only
  // The in-flight request's client, for stop_stream() to cancel. Guarded by
  // cancel_mutex_, which also backs the abortable retry backoff wait.
  std::mutex cancel_mutex_;
  std::condition_variable cancel_cv_;
  httplib::ClientImpl* active_client_ = nullptr;
  // When the stream last received bytes or started an attempt (steady_clock
  // ticks). poll_event() times out against it.
  std::atomic<std::chrono::steady_clock::rep> last_activity_{0};
  struct PendingToolCall {
    std::string id;
    std::string name;
    std::string arguments;
    std::string thought_signature;
  };
  std::vector<PendingToolCall> pending_tool_calls_;
  StreamProtocol protocol_;
  // Responses reasoning: once deltas have streamed, the finished item's
  // summary is not repeated (it would double the visible thinking text).
  bool reasoning_seen_ = false;  // stream thread only
};

}  // namespace openai
}  // namespace qcode