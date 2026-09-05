#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qcode {
namespace cursor {

// Minimal HTTP/2 POST helper. Cursor's agent RunSSE endpoint rejects HTTP/1.1
// (cpp-httplib), so agent calls go through libcurl with HTTP/2 enabled.
struct Http2PostResult {
  long status = 0;
  std::string body;
  std::string error;
  // True when the caller aborted the transfer from on_chunk (CURLE_ABORTED_BY_CALLBACK
  // / write-error). Treat as success if useful payload was already collected.
  bool aborted = false;

  bool ok() const { return error.empty() && status == 200; }
};

Http2PostResult http2_post(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& body,
    const std::string& content_type,
    int timeout_sec = 120);

// Streaming variant: on_chunk is invoked for each received body chunk.
// Return false from on_chunk to abort the transfer early (e.g. after turn_ended).
Http2PostResult http2_post_stream(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& body,
    const std::string& content_type,
    const std::function<bool(std::string_view chunk)>& on_chunk,
    int timeout_sec = 120);

// Reuses one curl easy handle so TLS/HTTP2 connections stay warm across
// BidiAppend calls (a new handshake per KV ack was ~1.2s).
class Http2Client {
 public:
  Http2Client();
  ~Http2Client();
  Http2Client(const Http2Client&) = delete;
  Http2Client& operator=(const Http2Client&) = delete;

  Http2PostResult post(
      const std::string& url,
      const std::vector<std::pair<std::string, std::string>>& headers,
      const std::string& body,
      const std::string& content_type,
      int timeout_sec = 30);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cursor
}  // namespace qcode
