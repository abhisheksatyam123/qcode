#pragma once

#include <qcode/core/stream_event.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

namespace qcode {

struct StreamDiagnostics {
  std::string endpoint;
  std::string model;
  size_t request_bytes = 0;
  int attempt = 1;
  int max_attempts = 1;
  int last_http_status = 0;
  bool in_backoff = false;
  long long backoff_remaining_ms = 0;
  bool connected = false;
  bool headers_received = false;
  size_t chunks_received = 0;
  size_t bytes_received = 0;
  double ttft_ms = -1.0;
  double idle_ms = 0.0;
};

namespace internal {
class StreamResultImpl {
 public:
  virtual ~StreamResultImpl() = default;
  virtual StreamEvent get_next_event() = 0;
  virtual bool has_more_events() const = 0;
  virtual void stop_stream() = 0;
  // Waits at most `timeout` for the next event; nullopt when none arrived.
  // This default blocks in get_next_event() for impls that cannot time out.
  virtual std::optional<StreamEvent> poll_event(
      std::chrono::milliseconds timeout) {
    (void)timeout;
    if (!has_more_events()) return std::nullopt;
    return get_next_event();
  }
  virtual StreamDiagnostics get_diagnostics() const { return {}; }
};
}  // namespace internal

class StreamResult {
 public:
  class iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type = StreamEvent;
    using difference_type = std::ptrdiff_t;
    using pointer = const StreamEvent*;
    using reference = const StreamEvent&;

    explicit iterator(const StreamResult* stream, bool end = false);

    reference operator*() const { return current_event_; }
    pointer operator->() const { return &current_event_; }
    iterator& operator++();
    iterator operator++(int);
    bool operator==(const iterator& other) const;
    bool operator!=(const iterator& other) const;

   private:
    const StreamResult* stream_;
    StreamEvent current_event_;
    bool is_end_;

    void advance();
  };

  StreamResult();

  explicit StreamResult(std::unique_ptr<internal::StreamResultImpl> impl);

  ~StreamResult();

  StreamResult(StreamResult&& other) noexcept;

  StreamResult& operator=(StreamResult&& other) noexcept;

  StreamResult(const StreamResult&) = delete;
  StreamResult& operator=(const StreamResult&) = delete;

  iterator begin() const;

  iterator end() const;

  // Consumes remaining events. Do not call before iterating the stream.
  bool has_error() const;

  // Consumes remaining events. Do not call before iterating the stream.
  std::string error_message() const;

  bool is_complete() const;

  // Abort-aware read: the next event, or nullopt when none arrived within
  // `timeout` (is_complete() then tells an idle stream from an ended one).
  std::optional<StreamEvent> poll(std::chrono::milliseconds timeout);

  // Cooperative cancel for in-flight HTTP streams (Esc / abort).
  void stop();

  // Diagnostics for in-flight stream (endpoint, attempt, backoff, chunks, idle time).
  StreamDiagnostics get_diagnostics() const;

 private:
  std::unique_ptr<internal::StreamResultImpl> stream_result_impl_;
};

}  // namespace qcode
