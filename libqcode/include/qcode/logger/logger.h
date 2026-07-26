#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <format>
#include <iomanip>
#include <iostream>
#include <memory>
#include <source_location>
#include <sstream>
#include <string_view>
#include <thread>

namespace qcode::logger {

enum class LogLevel {
  kLogLevelDebug = 0,
  kLogLevelInfo = 1,
  kLogLevelWarn = 2,
  kLogLevelError = 3
};

// ── Helpers ──

/// Thread-local name (set by each thread for human-readable logs).
inline thread_local std::string t_thread_name;

/// Set a human-readable name for the current thread.
inline void set_thread_name(std::string name) { t_thread_name = std::move(name); }

/// Returns the thread name if set, otherwise falls back to hex thread-id.
inline std::string thread_name_string() {
  if (!t_thread_name.empty()) return t_thread_name;
  std::ostringstream ss;
  ss << std::hex << std::this_thread::get_id();
  return ss.str();
}

/// Short file-name from full path (e.g. "src/tui/chat.cpp")
inline std::string_view short_file_name(std::string_view path) {
  auto pos = path.find_last_of("/\\");
  if (pos == std::string_view::npos) return path;
  auto pos2 = path.rfind('/', pos - 1);
  if (pos2 == std::string_view::npos) return path;
  return path.substr(pos2 + 1);
}

// ── Logger base ──

class Logger {
 public:
  virtual ~Logger() = default;

  // Core logging method with full source location
  virtual void log(LogLevel level, std::string_view message,
                   std::source_location loc) = 0;

  // Convenience methods (call log() directly; macros capture source_location at call site)
  template <typename... Args>
  void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (is_enabled(LogLevel::kLogLevelDebug)) {
      log(LogLevel::kLogLevelDebug,
          std::format(fmt, std::forward<Args>(args)...),
          std::source_location::current());
    }
  }

  template <typename... Args>
  void info(std::format_string<Args...> fmt, Args&&... args) {
    if (is_enabled(LogLevel::kLogLevelInfo)) {
      log(LogLevel::kLogLevelInfo,
          std::format(fmt, std::forward<Args>(args)...),
          std::source_location::current());
    }
  }

  template <typename... Args>
  void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (is_enabled(LogLevel::kLogLevelWarn)) {
      log(LogLevel::kLogLevelWarn,
          std::format(fmt, std::forward<Args>(args)...),
          std::source_location::current());
    }
  }

  template <typename... Args>
  void error(std::format_string<Args...> fmt, Args&&... args) {
    if (is_enabled(LogLevel::kLogLevelError)) {
      log(LogLevel::kLogLevelError,
          std::format(fmt, std::forward<Args>(args)...),
          std::source_location::current());
    }
  }

  virtual bool is_enabled(LogLevel level) const = 0;
};

// ── NullLogger ──

class NullLogger final : public Logger {
 public:
  void log(LogLevel, std::string_view,
           std::source_location) override {}
  bool is_enabled(LogLevel) const override { return false; }
};

// ── ConsoleLogger ──

class ConsoleLogger final : public Logger {
 public:
  explicit ConsoleLogger(LogLevel min_level = LogLevel::kLogLevelInfo)
      : min_level_(min_level) {}

  void log(LogLevel level, std::string_view message,
           std::source_location loc) override {
    if (!is_enabled(level)) return;

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::tm tm_buf{};
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S",
                  localtime_r(&time_t_now, &tm_buf));

    auto& stream = (level == LogLevel::kLogLevelError) ? std::cerr : std::cout;
    stream << "[" << time_buf << "." << std::setfill('0') << std::setw(3)
           << ms.count() << "]"
           << " [" << level_to_string(level) << "]"
           << " [" << short_file_name(loc.file_name()) << ":" << loc.line() << "]"
           << " [" << thread_name_string() << "]"
           << " " << message << std::endl;
    stream.flush();
  }

  bool is_enabled(LogLevel level) const override {
    return static_cast<int>(level) >= static_cast<int>(min_level_.load());
  }

  void set_min_level(LogLevel level) { min_level_.store(level); }

 private:
  static constexpr std::string_view level_to_string(LogLevel level) {
    switch (level) {
      case LogLevel::kLogLevelDebug: return "DEBUG";
      case LogLevel::kLogLevelInfo:  return "INFO";
      case LogLevel::kLogLevelWarn:  return "WARN";
      case LogLevel::kLogLevelError: return "ERROR";
    }
    return "UNKNOWN";
  }

  // Atomic so set_min_level() is safe to call from any thread while other
  // threads are logging through is_enabled()/log().
  std::atomic<LogLevel> min_level_;
};

// ── Global logger management ──

namespace detail {

inline std::atomic<std::shared_ptr<Logger>>& logger_instance() {
  static std::atomic<std::shared_ptr<Logger>> instance{std::make_shared<ConsoleLogger>()};
  return instance;
}

}  // namespace detail

inline void install_logger(std::shared_ptr<Logger> logger) {
  if (logger) {
    detail::logger_instance().store(std::move(logger));
  }
}

inline Logger& logger() {
  auto ptr = detail::logger_instance().load();
  return *ptr;
}

}  // namespace qcode::logger

// ── Macros that capture source_location at the call site ──
// These call logger().log() directly, capturing the TRUE caller location.
// They expand to a single statement (no dangling else).

#define LOG_DEBUG(...)                                                       \
  do {                                                                       \
    ::qcode::logger::Logger& _lg = ::qcode::logger::logger();                      \
    if (_lg.is_enabled(::qcode::logger::LogLevel::kLogLevelDebug)) {            \
      ::std::source_location _loc = ::std::source_location::current();       \
      _lg.log(::qcode::logger::LogLevel::kLogLevelDebug,                        \
              std::format(__VA_ARGS__), _loc);                                \
    }                                                                        \
  } while (0)

#define LOG_INFO(...)                                                        \
  do {                                                                       \
    ::qcode::logger::Logger& _lg = ::qcode::logger::logger();                      \
    if (_lg.is_enabled(::qcode::logger::LogLevel::kLogLevelInfo)) {             \
      ::std::source_location _loc = ::std::source_location::current();       \
      _lg.log(::qcode::logger::LogLevel::kLogLevelInfo,                         \
              std::format(__VA_ARGS__), _loc);                                \
    }                                                                        \
  } while (0)

#define LOG_WARN(...)                                                        \
  do {                                                                       \
    ::qcode::logger::Logger& _lg = ::qcode::logger::logger();                      \
    if (_lg.is_enabled(::qcode::logger::LogLevel::kLogLevelWarn)) {             \
      ::std::source_location _loc = ::std::source_location::current();       \
      _lg.log(::qcode::logger::LogLevel::kLogLevelWarn,                         \
              std::format(__VA_ARGS__), _loc);                                \
    }                                                                        \
  } while (0)

#define LOG_ERROR(...)                                                       \
  do {                                                                       \
    ::qcode::logger::Logger& _lg = ::qcode::logger::logger();                      \
    if (_lg.is_enabled(::qcode::logger::LogLevel::kLogLevelError)) {            \
      ::std::source_location _loc = ::std::source_location::current();       \
      _lg.log(::qcode::logger::LogLevel::kLogLevelError,                        \
              std::format(__VA_ARGS__), _loc);                                \
    }                                                                        \
  } while (0)

// Also keep inline function wrappers for convenience (without source_location capture)
namespace qcode::logger {

template <typename... Args>
inline void log_debug(std::format_string<Args...> fmt, Args&&... args) {
  logger().debug(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void log_info(std::format_string<Args...> fmt, Args&&... args) {
  logger().info(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void log_warn(std::format_string<Args...> fmt, Args&&... args) {
  logger().warn(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void log_error(std::format_string<Args...> fmt, Args&&... args) {
  logger().error(fmt, std::forward<Args>(args)...);
}

}  // namespace qcode::logger
