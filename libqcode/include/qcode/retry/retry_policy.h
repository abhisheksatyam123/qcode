#pragma once

#include <qcode/retry/retry_error.h>

#include <chrono>
#include <functional>
#include <thread>
#include <vector>
#include <random>

namespace ai {
namespace retry {

struct RetryConfig {
  int max_retries = 2;
  std::chrono::milliseconds initial_delay{2000};
  double backoff_factor = 2.0;
  std::chrono::milliseconds max_delay{30000};  // cap on a single backoff wait
  double jitter = 0.25;  // fractional jitter on each backoff wait (0 = disabled)
};

class RetryPolicy {
 public:
  explicit RetryPolicy(const RetryConfig& config = RetryConfig())
      : config_(config) {}

  template <typename Func, typename Result>
  Result execute_with_retry(Func&& func,
                            std::function<bool(const Result&)> is_retryable) {
    std::chrono::milliseconds current_delay = config_.initial_delay;

    for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
      Result result = func();

      if (!result.is_success()) {
        // Retry only when the result is explicitly retryable and attempts
        // remain. Otherwise surface the failed result directly rather than
        // throwing RetryError (callers prefer a failed Result).
        if (is_retryable(result) && attempt < config_.max_retries) {
          std::this_thread::sleep_for(current_delay);
          auto delay_ms =
              std::chrono::duration<double, std::milli>(current_delay) *
              config_.backoff_factor;
          current_delay =
              std::chrono::duration_cast<std::chrono::milliseconds>(delay_ms);
          // Cap the wait so legitimately slow generations are not starved.
          if (current_delay > config_.max_delay) {
            current_delay = config_.max_delay;
          }
          // Jitter the wait to avoid synchronized retries (thundering herd).
          if (config_.jitter > 0.0) {
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<double> dist(1.0 - config_.jitter,
                                                        1.0 + config_.jitter);
            const double factor = dist(rng);
            current_delay = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::duration<double, std::milli>(current_delay) * factor);
          }
          continue;
        }
        return result;
      }

      return result;
    }

    // Unreachable given the loop bounds, but keeps the compiler satisfied.
    return Result("Retry logic error");
  }

 private:
  RetryConfig config_;
};

}  // namespace retry
}  // namespace ai