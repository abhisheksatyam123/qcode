#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

namespace qcode {
namespace cursor {

// In-memory blob store for AgentService KvServerMessage frames.
// The server set_blobs context/tool payloads and later get_blobs them.
// An unanswered KV frame stalls the turn the same way an unanswered exec does.
class CursorKv {
 public:
  // KvServerMessage bytes -> AgentClientMessage { 3: KvClientMessage }.
  std::string handle(const std::string& kv_server_message);

 private:
  std::mutex mu_;
  std::unordered_map<std::string, std::string> blobs_;
};

}  // namespace cursor
}  // namespace qcode
