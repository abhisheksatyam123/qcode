#pragma once

#include <memory>
#include <string>

namespace qcode {
namespace cursor {

// Ordered BidiAppend client. Reuses one HTTP/2 connection and can enqueue
// KV acks without blocking the RunSSE reader.
class CursorBidi {
 public:
  CursorBidi(std::string aiserver_base_url, std::string access_token,
             std::string request_id, std::string client_version);
  ~CursorBidi();

  CursorBidi(const CursorBidi&) = delete;
  CursorBidi& operator=(const CursorBidi&) = delete;

  // Enqueue an AgentClientMessage. If wait, block until this append finishes.
  bool append(std::string client_message, bool wait = true);
  bool flush();
  bool is_ok() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cursor
}  // namespace qcode
