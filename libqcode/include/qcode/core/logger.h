#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <format>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <source_location>
#include <sstream>
#include <string_view>
#include <thread>

// NDK libc++ (r26) does not yet provide std::atomic<std::shared_ptr<T>>.
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
#define QCODE_LOGGER_ATOMIC_SHARED_PTR 1
#else
#define QCODE_LOGGER_ATOMIC_SHARED_PTR 0
#endif

namespace qcode::logger {

enum class LogLevel {
  kLogLevelDebug = 0,
  kLogLevelInfo = 1,
  kLogLevelWarn = 2,
  kLogLevelError = 3
};

// ── Helpers ──


/* local name for the current thread */
inline thread_local std::string t_thread_name;

/* local session id for the current thread.
 *
 * Logging is a process-wide singleton, so the sink cannot be swapped per
 * session. Instead the sink stays global and reads this thread-local to
 * decide WHICH per-session file a line belongs to. Threads that serve one
 * agent session (generation workers, tool executors) publish their session
 * id once at thread start; every log line they emit is then routed to that
 * session's own file. See qcode::SessionFileLogger.
 */
inline thread_local std::string t_session_id;

/* Set a human-readable name for the current thread.
 * Here std::move move the pointer  of name string to t_thread_name
 *
 */
inline void set_thread_name(std::string name) { t_thread_name = std::move(name); }

/* Bind the current thread to a session id for session-scoped logging. */
inline void set_thread_session_id(std::string id) { t_session_id = std::move(id); }

/* Current thread's session id, or empty for unscoped (server) threads. */
inline const std::string& thread_session_id() { return t_session_id; }

/* Binds the current thread to `id` for the lifetime of the object and restores
 * the previous binding after. Use it at the top of every thread body that
 * works for a session; a new thread starts unbound, so capture
 * thread_session_id() before spawning and pass it in:
 *
 *   std::thread([sid = logger::thread_session_id()] {
 *     logger::ScopedThreadSession bind(sid);
 *     ...
 *   });
 */
class ScopedThreadSession {
 public:
  explicit ScopedThreadSession(std::string id) : previous_(t_session_id) {
    t_session_id = std::move(id);
  }
  ~ScopedThreadSession() { t_session_id = std::move(previous_); }
  ScopedThreadSession(const ScopedThreadSession&) = delete;
  ScopedThreadSession& operator=(const ScopedThreadSession&) = delete;

 private:
  std::string previous_;
};

/*
 * this API returns the string name of the current thread if set
 * else it returns the hex thread id values from std::this_thread::get_id
 */
inline std::string thread_name_string() {
  if (!t_thread_name.empty()) return t_thread_name;
  std::ostringstream ss;
  ss << std::hex << std::this_thread::get_id();
  return ss.str();
}

/// Thread-safe cross-platform localtime helper
inline std::tm* portable_localtime(const std::time_t* t, std::tm* tm_buf) {
#if defined(_WIN32) || defined(_WIN64)
  localtime_s(tm_buf, t);
  return tm_buf;
#else
  return localtime_r(t, tm_buf);
#endif
}

/// Short file-name from full path (e.g. "src/ui/commands.cpp")
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
  /* virtaul means we have to run derived destructor fiest then we will be  running Logger() descrtuctor */
  virtual ~Logger() = default;

  // Core logging method with full source location
  virtual void log(LogLevel level, std::string_view message,
                   std::source_location loc) = 0;

  /* Convenience methods (call log() directly; macros capture source_location at call site)
   * template : template keyword means that we need to crate actual API at compile time depending on the calllers of this function
   * typename describes what types we need to pick in this template
   * ... means we will be having multiple types
   * Args is list of those types.
   * std::format_string is string of format types " {} {}" where these brackets will be printed with values.
   * <Args...> in the format string represent what types will get inseted in the string.
   * ... is pack in left and unpack in righ  in this case its pack of values.
   * we are calling log API with debug level and std::format into our fmt string with our unpacked arguments each with
   *  std::formard<type>(value)
   */
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

  // Flush any buffered tail so the last lines are never lost at shutdown.
  ~ConsoleLogger() override {
    std::lock_guard<std::mutex> lock(mu_);
    flush_streams_locked();
  }

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
    portable_localtime(&time_t_now, &tm_buf);
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_buf);

    std::lock_guard<std::mutex> lock(mu_);
    auto& stream = (level == LogLevel::kLogLevelError) ? std::cerr : std::cout;
    stream << "[" << time_buf << "." << std::setfill('0') << std::setw(3)
           << ms.count() << "]"
           << " [" << level_to_string(level) << "]"
           << " [" << short_file_name(loc.file_name()) << ":" << loc.line() << "]"
           << " [" << thread_name_string() << "]"
           << " " << message << '\n';

    // WARN/ERROR are flushed immediately. Lower levels are flushed at most
    // every kFlushInterval or every kFlushMaxLines lines, whichever comes first.
    ++pending_lines_;
    const auto steady_now = std::chrono::steady_clock::now();
    const bool urgent = level == LogLevel::kLogLevelWarn ||
                        level == LogLevel::kLogLevelError;
    if (urgent || pending_lines_ >= kFlushMaxLines ||
        steady_now - last_flush_ >= kFlushInterval) {
      flush_streams_locked();
    }
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

  // Guards the stream writes and the flush bookkeeping below.
  void flush_streams_locked() {
    std::cout.flush();
    std::cerr.flush();
    pending_lines_ = 0;
    last_flush_ = std::chrono::steady_clock::now();
  }

  static constexpr std::size_t kFlushMaxLines = 64;
  static constexpr std::chrono::milliseconds kFlushInterval{200};

  std::mutex mu_;
  std::size_t pending_lines_ = 0;
  std::chrono::steady_clock::time_point last_flush_ =
      std::chrono::steady_clock::now();
};

// ── Global logger management ──

namespace detail {

#if QCODE_LOGGER_ATOMIC_SHARED_PTR

/* Instanciating the logger
  * here the static inside the function means the initialization of the instance variable
  * will only happane once.
  */
inline std::atomic<std::shared_ptr<Logger>>& logger_instance() {
  static std::atomic<std::shared_ptr<Logger>> instance{
      std::make_shared<ConsoleLogger>()};
  return instance;
}

/* as Logger is a shared pointer to type std::atomic then to load  of type std::atomic
 * we need .load , .store API from standerd library
 */
inline void store_logger(std::shared_ptr<Logger> logger) {
  logger_instance().store(std::move(logger));
}

inline std::shared_ptr<Logger> load_logger() { return logger_instance().load(); }

#else

struct LoggerSlot {
  std::mutex mu;
  std::shared_ptr<Logger> ptr{std::make_shared<ConsoleLogger>()};
};

inline LoggerSlot& logger_slot() {
  static LoggerSlot slot;
  return slot;
}

inline void store_logger(std::shared_ptr<Logger> logger) {
  auto& slot = logger_slot();
  std::lock_guard<std::mutex> lock(slot.mu);
  slot.ptr = std::move(logger);
}

inline std::shared_ptr<Logger> load_logger() {
  auto& slot = logger_slot();
  std::lock_guard<std::mutex> lock(slot.mu);
  return slot.ptr;
}

#endif

}  // namespace detail

inline void install_logger(std::shared_ptr<Logger> logger) {
  if (logger) {
    detail::store_logger(std::move(logger));
  }
}

inline Logger& logger() {
  auto ptr = detail::load_logger();
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
