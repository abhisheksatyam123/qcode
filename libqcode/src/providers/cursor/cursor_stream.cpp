#include "cursor_stream.h"

namespace ai {
namespace cursor {

CursorBufferedStream::CursorBufferedStream() = default;

CursorBufferedStream::~CursorBufferedStream() {
  stop_stream();
  if (thread_.joinable()) {
    thread_.join();
  }
}

StreamEvent CursorBufferedStream::get_next_event() {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this]() { return !events_.empty() || done_; });
  if (!events_.empty()) {
    StreamEvent event = std::move(events_.front());
    events_.pop();
    return event;
  }
  return StreamEvent(kStreamEventTypeFinish);
}

bool CursorBufferedStream::has_more_events() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !events_.empty() || !done_;
}

void CursorBufferedStream::stop_stream() {
  std::lock_guard<std::mutex> lock(mutex_);
  stopped_ = true;
  done_ = true;
  cv_.notify_all();
}

void CursorBufferedStream::push_event(StreamEvent event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stopped_) {
    events_.push(std::move(event));
    cv_.notify_all();
  }
}

void CursorBufferedStream::mark_done() {
  std::lock_guard<std::mutex> lock(mutex_);
  done_ = true;
  cv_.notify_all();
}

bool CursorBufferedStream::is_stopped() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stopped_;
}

void CursorBufferedStream::start_thread(std::thread thread) {
  std::lock_guard<std::mutex> lock(mutex_);
  thread_ = std::move(thread);
}

}  // namespace cursor
}  // namespace ai
