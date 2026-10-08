#pragma once

#include <qcode/core/logger.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace qcode {

// ---------------------------------------------------------------------------
// SessionFileLogger: stores logs for each session in a separate file
// ---------------------------------------------------------------------------
// The logger is a process-wide singleton (see qcode::logger::detail), so
// installing a distinct FileLogger per session would make concurrent sessions
// overwrite each other. SessionFileLogger instead keeps a registry of sinks
// keyed by session id and routes every line by the CALLING THREAD's session id
// (qcode::logger::thread_session_id()).
//
// Each session therefore gets its own dedicated log file:
//   "<dir>/<stem>-<session_id>.log"
// and its own independent rotation budget (default 16 MiB), so one session's
// debug flood can never truncate another session's history.
//
// Lines emitted by threads that never bound a session id (server startup,
// request routing, background housekeepers) go to the unscoped stem file:
//   "<dir>/<stem>.log"
class SessionFileLogger final : public logger::Logger {
 public:
  static constexpr std::uintmax_t kDefaultMaxBytes = 16ull << 20;  // 16 MiB

  // dir  : directory that holds the per-session log files
  // stem : file name stem, e.g. "qcode-server" -> "<dir>/<stem>-<session_id>.log"
  explicit SessionFileLogger(std::string dir, std::string stem,
                             logger::LogLevel min_level =
                                 logger::LogLevel::kLogLevelDebug,
                             std::uintmax_t max_bytes = kDefaultMaxBytes)
      : dir_(std::move(dir)),
        stem_(std::move(stem)),
        min_level_(min_level),
        max_bytes_(max_bytes) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    flusher_ = std::thread([this] { flush_loop(); });
  }

  void log(logger::LogLevel level, std::string_view message,
           std::source_location loc) override {
    if (!is_enabled(level)) return;

    const std::string line = format_line(level, message, loc);
    const std::string session = logger::thread_session_id();

    std::lock_guard<std::mutex> lock(mutex_);
    Sink& sink = sink_for_locked(session);
    sink.bytes += line.size();
    sink.stream << line;
    rotate_if_needed_locked(session, sink);

    // Pending lines are tracked across all session streams. WARN/ERROR and
    // every kFlushMaxLines-th line flush here; anything else is flushed by
    // flush_loop() within kFlushInterval, so a quiet tail still reaches disk.
    ++pending_lines_;
    const bool urgent = level == logger::LogLevel::kLogLevelWarn ||
                        level == logger::LogLevel::kLogLevelError;
    if (urgent || pending_lines_ >= kFlushMaxLines) {
      flush_all_locked();
    } else if (pending_lines_ == 1) {
      flush_cv_.notify_one();
    }
  }

  bool is_enabled(logger::LogLevel level) const override {
    return static_cast<int>(level) >= static_cast<int>(min_level_);
  }

  // Directory holding all session log files.
  const std::string& dir() const { return dir_; }

  // File name stem shared by every session file.
  const std::string& stem() const { return stem_; }

  // Absolute path of the file backing session_id ("" = unscoped stem file).
  std::string path_for(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return path_for_locked(session_id);
  }

  // Session keys currently open in memory (used by tests and diagnostics).
  std::vector<std::string> open_session_keys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> keys;
    keys.reserve(sinks_.size());
    for (const auto& [key, _] : sinks_) keys.push_back(key);
    return keys;
  }

  // Check whether a sink is currently active for the given session_id.
  bool has_open_sink(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sinks_.find(session_id) != sinks_.end();
  }

  // Release in-memory handles for sessions that no longer exist. Files stay
  // on disk; only the ofstream handles are closed and the sinks erased.
  void prune(const std::function<bool(const std::string& sid)>& is_live) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Flush before closing any handle so a finished session's buffered tail
    // is on disk before its stream goes away.
    flush_all_locked();
    for (auto it = sinks_.begin(); it != sinks_.end();) {
      if (it->first.empty() || is_live(it->first)) {
        ++it;
        continue;
      }
      if (it->second.stream.is_open()) {
        it->second.stream.flush();
        it->second.stream.close();
      }
      it = sinks_.erase(it);
    }
  }

  // Push buffered lines to disk. Readers of the log files call this first so
  // they never see a stale tail.
  void flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_all_locked();
  }

  // Flush and close every sink. Call before shutdown or directory removal.
  void close_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_all_locked();
    for (auto& [key, sink] : sinks_) {
      if (sink.stream.is_open()) sink.stream.close();
    }
    sinks_.clear();
  }

  // Stop the flusher, then flush and close every sink so no tail is lost.
  ~SessionFileLogger() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    flush_cv_.notify_one();
    if (flusher_.joinable()) flusher_.join();
    close_all();
  }

 private:
  struct Sink {
    std::ofstream stream;
    std::uintmax_t bytes = 0;
  };

  static std::string format_line(logger::LogLevel level,
                                 std::string_view message,
                                 std::source_location loc) {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) %
        1000;

    std::tm tm_buf{};
    char time_buf[32];
    logger::portable_localtime(&time_t_now, &tm_buf);
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    char ms_buf[8];
    std::snprintf(ms_buf, sizeof(ms_buf), "%03d",
                  static_cast<int>(ms.count()));

    std::string line;
    line.reserve(message.size() + 96);
    line += "[";
    line += time_buf;
    line += ".";
    line += ms_buf;
    line += "] [";
    line += level_to_string(level);
    line += "] [";
    line += logger::short_file_name(loc.file_name());
    line += ":";
    line += std::to_string(loc.line());
    line += "] [";
    line += logger::thread_name_string();
    line += "] ";
    line += message;
    line += "\n";
    return line;
  }

  std::string path_for_locked(const std::string& session_id) const {
    if (session_id.empty()) {
      return dir_ + "/" + (stem_.empty() ? "server" : stem_) + ".log";
    }
    if (stem_.empty()) {
      return dir_ + "/" + session_id + ".log";
    }
    return dir_ + "/" + stem_ + "-" + session_id + ".log";
  }

  // Caller must hold mutex_.
  Sink& sink_for_locked(const std::string& session_id) {
    auto it = sinks_.find(session_id);
    if (it != sinks_.end()) return it->second;

    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);

    Sink sink;
    const std::string path = path_for_locked(session_id);
    // Reuse size from a previous run so the cap still triggers on restart.
    sink.bytes = (std::filesystem::exists(path, ec) && !ec)
                     ? std::filesystem::file_size(path, ec)
                     : 0;
    if (ec) sink.bytes = 0;
    sink.stream.open(path, std::ios::app);
    return sinks_.emplace(session_id, std::move(sink)).first->second;
  }

  // Caller must hold mutex_. Flushes every open session stream and resets the
  // logger-wide pending count.
  void flush_all_locked() {
    for (auto& [key, sink] : sinks_) {
      if (sink.stream.is_open()) sink.stream.flush();
    }
    pending_lines_ = 0;
  }

  // Sleeps until a line is pending, lets the burst batch up for
  // kFlushInterval, then flushes. No wakeups while the log is idle.
  void flush_loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
      flush_cv_.wait(lock, [this] { return stopping_ || pending_lines_ > 0; });
      if (stopping_) return;
      flush_cv_.wait_for(lock, kFlushInterval, [this] { return stopping_; });
      if (pending_lines_ > 0) flush_all_locked();
    }
  }

  // Caller must hold mutex_. Rotates only this session's file to
  // "<path>.old", so every other session keeps its full history.
  void rotate_if_needed_locked(const std::string& session_id, Sink& sink) {
    if (sink.bytes == 0 || max_bytes_ == 0) return;
    if (sink.bytes <= max_bytes_) return;

    const std::string path = path_for_locked(session_id);
    if (sink.stream.is_open()) sink.stream.close();
    std::error_code ec;
    const std::string rotated = path + ".old";
    std::filesystem::remove(rotated, ec);
    std::filesystem::rename(path, rotated, ec);
    sink.stream.clear();
    sink.stream.open(path, std::ios::app);
    sink.bytes = 0;
  }

  static std::string_view level_to_string(logger::LogLevel level) {
    switch (level) {
      case logger::LogLevel::kLogLevelDebug: return "DEBUG";
      case logger::LogLevel::kLogLevelInfo:  return "INFO";
      case logger::LogLevel::kLogLevelWarn:  return "WARN";
      case logger::LogLevel::kLogLevelError: return "ERROR";
    }
    return "UNKNOWN";
  }

  static constexpr std::size_t kFlushMaxLines = 64;
  static constexpr std::chrono::milliseconds kFlushInterval{200};

  std::string dir_;
  std::string stem_;
  logger::LogLevel min_level_;
  std::uintmax_t max_bytes_ = kDefaultMaxBytes;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Sink> sinks_;
  // Guarded by mutex_. Counts lines written across all session streams since
  // the last flush of every stream.
  std::size_t pending_lines_ = 0;
  bool stopping_ = false;
  std::condition_variable flush_cv_;
  std::thread flusher_;  // last: started after every other member exists
};

// Install the per-session file logger. Returns the instance so callers can
// prune finished sessions and close sinks at shutdown.
inline std::shared_ptr<SessionFileLogger> install_session_file_logger(
    const std::string& dir, const std::string& stem,
    logger::LogLevel level = logger::LogLevel::kLogLevelDebug,
    std::uintmax_t max_bytes = SessionFileLogger::kDefaultMaxBytes) {
  auto instance =
      std::make_shared<SessionFileLogger>(dir, stem, level, max_bytes);
  logger::install_logger(instance);
  qcode::logger::log_info("Session file logger installed: {}/{}-<session>.log",
                          dir, stem);
  return instance;
}

}  // namespace qcode
