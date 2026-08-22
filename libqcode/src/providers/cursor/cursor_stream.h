#pragma once

#include <qcode/core/stream_result.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace qcode {
namespace cursor {

class CursorBufferedStream : public internal::StreamResultImpl {
 public:
  CursorBufferedStream();
  ~CursorBufferedStream() override;

  StreamEvent get_next_event() override;
  bool has_more_events() const override;
  void stop_stream() override;

  void push_event(StreamEvent event);
  void mark_done();
  bool is_stopped() const;

  void start_thread(std::thread thread);

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  std::queue<StreamEvent> events_;
  bool done_ = false;
  bool stopped_ = false;
  std::thread thread_;
};

}  // namespace cursor
}  // namespace qcode
