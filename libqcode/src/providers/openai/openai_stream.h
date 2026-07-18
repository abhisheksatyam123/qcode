#pragma once


#include <qcode/types/stream_result.h>

#include <atomic>
#include <concurrentqueue.h>
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
  void stop_stream() override;

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
                  const nlohmann::json& request_body);
  void parse_sse_line(const std::string& line);
  void push_event(StreamEvent event);
  void push_finish_event_if_needed();
  void mark_complete();

  // Helper functions
  StreamEvent create_error_event(const std::string& message);
  FinishReason parse_finish_reason(const std::string& reason_str);
  Usage parse_usage(const nlohmann::json& usage_json);

  moodycamel::ConcurrentQueue<StreamEvent> event_queue_;
  std::thread stream_thread_;
  std::mutex thread_mutex_;
  std::atomic<bool> is_complete_{false};
  std::atomic<bool> should_stop_{false};
  std::atomic<bool> finish_event_pushed_{false};
  StreamProtocol protocol_;
};

}  // namespace openai
}  // namespace qcode