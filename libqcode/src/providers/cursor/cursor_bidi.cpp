#include "cursor_bidi.h"

#include "cursor_http2.h"
#include "cursor_proto.h"

#include <qcode/core/logger.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace qcode {
namespace cursor {
namespace {

constexpr const char* kBidiAppendPath = "/aiserver.v1.BidiService/BidiAppend";

std::string join_url(std::string base, const char* path) {
  if (!base.empty() && base.back() == '/') base.pop_back();
  return base + path;
}

}  // namespace

struct CursorBidi::Impl {
  struct Item {
    std::string body;
    bool done = false;
    bool ok = false;
  };

  Impl(std::string url, std::string token, std::string request_id,
       std::string client_version)
      : url_(std::move(url)),
        token_(std::move(token)),
        request_id_(std::move(request_id)),
        client_version_(std::move(client_version)),
        worker_([this] { run(); }) {}

  ~Impl() { shutdown(); }

  bool append(std::string client_message, bool wait) {
    auto item = std::make_shared<Item>();
    item->body = std::move(client_message);
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (failed_ || stop_) return false;
      q_.push_back(item);
    }
    cv_.notify_one();
    if (!wait) return true;
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [item] { return item->done; });
    return item->ok;
  }

  bool flush() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return q_.empty() || failed_; });
    return !failed_;
  }

  bool is_ok() const { return !failed_; }

  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  void fail_remaining() {
    for (auto& item : q_) {
      item->ok = false;
      item->done = true;
    }
    q_.clear();
    failed_ = true;
  }

  bool post(Http2Client& http, const std::string& client_message) {
    const uint64_t seq = seqno_.fetch_add(1);
    const auto bidi_request_id = proto::bytes_field(1, request_id_);
    const auto body = proto::bytes_field(2, bidi_request_id) +
                      proto::varint_field(3, seq) +
                      proto::bytes_field(4, client_message);

    const std::vector<std::pair<std::string, std::string>> headers = {
        {"Authorization", "Bearer " + token_},
        {"connect-protocol-version", "1"},
        {"x-cursor-client-type", "cli"},
        {"x-cursor-client-version", client_version_},
        {"x-ghost-mode", "false"},
        {"x-request-id", request_id_},
    };

    const auto start = std::chrono::steady_clock::now();
    const auto res =
        http.post(url_, headers, body, "application/proto", /*timeout_sec=*/30);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    if (!res.ok()) {
      LOG_ERROR("Cursor BidiAppend failed seq={}: {}", seq,
                res.error.empty() ? ("HTTP " + std::to_string(res.status))
                                  : res.error);
      return false;
    }
    if (ms >= 200) {
      LOG_INFO("Cursor BidiAppend ok seq={} bytes={} ms={}", seq,
               client_message.size(), ms);
    } else {
      LOG_DEBUG("Cursor BidiAppend ok seq={} bytes={} ms={}", seq,
                client_message.size(), ms);
    }
    return true;
  }

  void run() {
    Http2Client http;
    for (;;) {
      std::shared_ptr<Item> item;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this] { return stop_ || !q_.empty(); });
        if (q_.empty()) return;  // stop_ and drained
        item = q_.front();
      }

      const bool ok = post(http, item->body);
      {
        std::lock_guard<std::mutex> lock(mu_);
        item->ok = ok;
        item->done = true;
        if (!q_.empty() && q_.front() == item) q_.pop_front();
        if (!ok) fail_remaining();
      }
      cv_.notify_all();
      if (!ok) return;
    }
  }

  const std::string url_;
  const std::string token_;
  const std::string request_id_;
  const std::string client_version_;
  std::atomic<uint64_t> seqno_{0};
  std::atomic<bool> failed_{false};
  bool stop_ = false;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::shared_ptr<Item>> q_;
  std::thread worker_;
};

CursorBidi::CursorBidi(std::string aiserver_base_url, std::string access_token,
                       std::string request_id, std::string client_version)
    : impl_(std::make_unique<Impl>(
          join_url(std::move(aiserver_base_url), kBidiAppendPath),
          std::move(access_token), std::move(request_id),
          std::move(client_version))) {}

CursorBidi::~CursorBidi() = default;

bool CursorBidi::append(std::string client_message, bool wait) {
  return impl_->append(std::move(client_message), wait);
}

bool CursorBidi::flush() { return impl_->flush(); }

bool CursorBidi::is_ok() const { return impl_->is_ok(); }

}  // namespace cursor
}  // namespace qcode
