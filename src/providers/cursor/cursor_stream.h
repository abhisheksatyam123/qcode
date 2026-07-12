#pragma once

#include "ai/types/stream_result.h"

#include <mutex>
#include <queue>
#include <string>

namespace ai {
namespace cursor {

// Minimal buffered stream: feeds the whole (already-decoded) assistant text as a
// single text delta followed by a finish event. True incremental SSE streaming is
// deferred -- the Cursor client currently reads the full RunSSE body and decodes it.
class CursorBufferedStream : public internal::StreamResultImpl {
 public:
  explicit CursorBufferedStream(std::string text);

  StreamEvent get_next_event() override;
  bool has_more_events() const override;
  void stop_stream() override;

 private:
  mutable std::mutex mutex_;
  std::queue<StreamEvent> events_;
};

}  // namespace cursor
}  // namespace ai
