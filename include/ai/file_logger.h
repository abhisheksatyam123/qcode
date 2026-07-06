#pragma once

#include "ai/logger.h"

#include <fstream>
#include <mutex>
#include <string>

namespace ai {

/// File logger that writes to a specified path
class FileLogger final : public logger::Logger {
 public:
  explicit FileLogger(const std::string& path, logger::LogLevel min_level = logger::LogLevel::kLogLevelDebug)
      : min_level_(min_level) {
    log_file_.open(path, std::ios::app);
    if (!log_file_.is_open()) {
      // Fallback: try to create the file
      log_file_.open(path, std::ios::app);
    }
  }

  ~FileLogger() override {
    if (log_file_.is_open()) log_file_.close();
  }

  void log(logger::LogLevel level, std::string_view message) override {
    if (!is_enabled(level) || !log_file_.is_open()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    log_file_ << "[" << level_to_string(level) << "] " << message << std::endl;
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
inline void install_file_logger(const std::string& path, logger::LogLevel level = logger::LogLevel::kLogLevelDebug) {
  auto file_logger = std::make_shared<FileLogger>(path, level);
  logger::install_logger(file_logger);
  ai::logger::log_info("File logger installed: {}", path);
}

}  // namespace ai
