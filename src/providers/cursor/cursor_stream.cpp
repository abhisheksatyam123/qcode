#include "cursor_stream.h"

namespace ai {
namespace cursor {

CursorBufferedStream::CursorBufferedStream(std::string text) {
  if (!text.empty()) {
    events_.push(StreamEvent(std::move(text)));
  }
  events_.push(StreamEvent(kStreamEventTypeFinish));
}

StreamEvent CursorBufferedStream::get_next_event() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!events_.empty()) {
    StreamEvent event = events_.front();
    events_.pop();
    return event;
  }
  return StreamEvent(kStreamEventTypeFinish);
}

bool CursorBufferedStream::has_more_events() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !events_.empty();
}

void CursorBufferedStream::stop_stream() {}

}  // namespace cursor
}  // namespace ai
