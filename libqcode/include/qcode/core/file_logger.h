#pragma once

#include <qcode/core/logger.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>

namespace qcode {

/// File logger that writes to a specified path with timestamps,
/// source location, and thread-id metadata.
///
/// Size-capped with automatic rotation: when the active file exceeds
/// max_bytes it is renamed to "<path>.old" (replacing any previous .old)
/// and a fresh file is started. This keeps a runaway debug flood from
/// filling the disk during long sessions.
class FileLogger final : public logger::Logger {
 public:
  static constexpr std::uintmax_t kDefaultMaxBytes = 16ull << 20;  // 16 MiB

  explicit FileLogger(const std::string& path,
                      logger::LogLevel min_level = logger::LogLevel::kLogLevelDebug,
                      std::uintmax_t max_bytes = kDefaultMaxBytes)
      : path_(path), min_level_(min_level), max_bytes_(max_bytes) {
    open_locked();
  }

  ~FileLogger() override {
    if (log_file_.is_open()) log_file_.close();
  }

  void log(logger::LogLevel level, std::string_view message,
           std::source_location loc) override {
    if (!is_enabled(level)) return;
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&time_t_now));

    std::string line;
    line.reserve(message.size() + 96);
    line += "[";
    line += time_buf;
    line += ".";
    char ms_buf[8];
    std::snprintf(ms_buf, sizeof(ms_buf), "%03d", static_cast<int>(ms.count()));
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

    rotate_if_needed(line.size());
    if (!log_file_.is_open()) return;
    log_file_ << line;
    log_file_.flush();
    bytes_written_ += line.size();
  }

  bool is_enabled(logger::LogLevel level) const override {
    return static_cast<int>(level) >= static_cast<int>(min_level_);
  }

 private:
  static constexpr std::string_view level_to_string(logger::LogLevel level) {
    switch (level) {
      case logger::LogLevel::kLogLevelDebug: return "DEBUG";
      case logger::LogLevel::kLogLevelInfo:  return "INFO";
      case logger::LogLevel::kLogLevelWarn:  return "WARN";
      case logger::LogLevel::kLogLevelError: return "ERROR";
    }
    return "UNKNOWN";
  }

  // Caller must hold mutex_. Reuses/creates the active file and seeds the
  // byte counter so rotation triggers correctly across restarts.
  void open_locked() {
    namespace fs = std::filesystem;
    std::error_code ec;
    bytes_written_ = fs::exists(path_, ec) && !ec ? fs::file_size(path_, ec) : 0;
    if (ec) bytes_written_ = 0;
    log_file_.close();
    log_file_.clear();
    log_file_.open(path_, std::ios::app);
  }

  // Caller must hold mutex_. Renames the active file to "<path>.old" once
  // it would exceed the cap, then starts a fresh file.
  void rotate_if_needed(std::size_t incoming) {
    if (bytes_written_ == 0 || max_bytes_ == 0) return;
    if (bytes_written_ + incoming <= max_bytes_) return;

    namespace fs = std::filesystem;
    log_file_.close();
    std::error_code ec;
    const fs::path rotated = path_ + ".old";
    fs::remove(rotated, ec);
    fs::rename(path_, rotated, ec);
    open_locked();
  }

  std::string path_;
  std::ofstream log_file_;
  std::mutex mutex_;
  logger::LogLevel min_level_;
  std::uintmax_t max_bytes_ = kDefaultMaxBytes;
  std::uintmax_t bytes_written_ = 0;
};

/// Install a file logger at the given path
inline void install_file_logger(
    const std::string& path,
    logger::LogLevel level = logger::LogLevel::kLogLevelDebug,
    std::uintmax_t max_bytes = FileLogger::kDefaultMaxBytes) {
  auto file_logger = std::make_shared<FileLogger>(path, level, max_bytes);
  logger::install_logger(file_logger);
  qcode::logger::log_info("File logger installed: {} (cap {} bytes)", path,
                          max_bytes);
}

}  // namespace qcode
