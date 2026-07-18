#pragma once

#include <qcode/logger/logger.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>

namespace ai {

/// File logger that writes to a specified path with timestamps,
/// source location, and thread-id metadata
class FileLogger final : public logger::Logger {
 public:
  explicit FileLogger(const std::string& path,
                      logger::LogLevel min_level = logger::LogLevel::kLogLevelDebug)
      : min_level_(min_level) {
    log_file_.open(path, std::ios::app);
    if (!log_file_.is_open()) {
      log_file_.open(path, std::ios::app);
    }
  }

  ~FileLogger() override {
    if (log_file_.is_open()) log_file_.close();
  }

  void log(logger::LogLevel level, std::string_view message,
           std::source_location loc) override {
    if (!is_enabled(level) || !log_file_.is_open()) return;
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&time_t_now));

    log_file_ << "[" << time_buf << "." << std::setfill('0') << std::setw(3)
              << ms.count() << "]"
              << " [" << level_to_string(level) << "]"
              << " [" << logger::short_file_name(loc.file_name()) << ":" << loc.line() << "]"
              << " [" << logger::thread_name_string() << "]"
              << " " << message << std::endl;
    log_file_.flush();
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

  std::ofstream log_file_;
  std::mutex mutex_;
  logger::LogLevel min_level_;
};

/// Install a file logger at the given path
inline void install_file_logger(const std::string& path,
                                logger::LogLevel level = logger::LogLevel::kLogLevelDebug) {
  auto file_logger = std::make_shared<FileLogger>(path, level);
  logger::install_logger(file_logger);
  ai::logger::log_info("File logger installed: {}", path);
}

}  // namespace ai
