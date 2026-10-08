#pragma once

#include <qcode/core/logger.h>

#include <chrono>
#include <string_view>

// Lightweight timing probes. Everything is logged at DEBUG with a "[perf]"
// prefix, so `grep '\[perf\]'` on a debug log gives the numbers; with DEBUG
// off a probe costs one level check.
//
//   PERF_SCOPE("prune_context");           // logs elapsed ms at scope exit
//   qcode::perf::Stopwatch sw;  ... sw.ms() // manual, for values in a message
//   PERF_LOG("step={} history={} msgs", n, m);

namespace qcode::perf {

class Stopwatch {
 public:
  Stopwatch() : start_(std::chrono::steady_clock::now()) {}
  double ms() const {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start_)
        .count();
  }

 private:
  std::chrono::steady_clock::time_point start_;
};

class ScopedTimer {
 public:
  explicit ScopedTimer(std::string_view name)
      : name_(name),
        on_(logger::logger().is_enabled(logger::LogLevel::kLogLevelDebug)) {}
  ~ScopedTimer() {
    if (on_) LOG_DEBUG("[perf] {} {:.2f} ms", name_, watch_.ms());
  }
  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

 private:
  std::string_view name_;
  bool on_;
  Stopwatch watch_;
};

// Averages a hot loop (e.g. UI frames) and logs once per `window` calls.
class RollingTimer {
 public:
  RollingTimer(std::string_view name, int window = 120)
      : name_(name), window_(window) {}
  // Times the enclosing scope and feeds it to the rolling average.
  class Scope {
   public:
    explicit Scope(RollingTimer& t) : t_(t) {}
    ~Scope() { t_.add(watch_.ms()); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    RollingTimer& t_;
    Stopwatch watch_;
  };

 private:
  void add(double ms) {
    sum_ += ms;
    if (ms > max_) max_ = ms;
    if (++count_ < window_) return;
    LOG_DEBUG("[perf] {} n={} avg_ms={:.2f} max_ms={:.2f}", name_, count_,
              sum_ / count_, max_);
    count_ = 0;
    sum_ = max_ = 0;
  }

  std::string_view name_;
  int window_;
  int count_ = 0;
  double sum_ = 0;
  double max_ = 0;
};

}  // namespace qcode::perf

#define PERF_CONCAT_(a, b) a##b
#define PERF_CONCAT(a, b) PERF_CONCAT_(a, b)
#define PERF_SCOPE(name) \
  ::qcode::perf::ScopedTimer PERF_CONCAT(perf_scope_, __LINE__)(name)
#define PERF_LOG(...) LOG_DEBUG("[perf] " __VA_ARGS__)
