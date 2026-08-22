#pragma once

#include <qcode/logger/logger.h>
#include <qcode/retry/retry_error.h>

#include <chrono>
#include <functional>
#include <random>
#include <thread>
#include <vector>

namespace qcode {
namespace retry {

using RetryCallback = std::function<void(
    int attempt, int total_attempts, std::chrono::milliseconds delay,
    const std::string& error_msg)>;

struct RetryConfig {
  int max_retries = 2;
  std::chrono::milliseconds initial_delay{2000};
  double backoff_factor = 2.0;
  std::chrono::milliseconds max_delay{30000};  // cap on a single backoff wait
  double jitter = 0.25;  // fractional jitter on each backoff wait (0 = disabled)
  RetryCallback on_retry = nullptr;
};

class RetryPolicy {
 public:
  explicit RetryPolicy(const RetryConfig& config = RetryConfig())
      : config_(config) {}

  template <typename Func, typename Result>
  Result execute_with_retry(
      Func&& func,
      std::function<bool(const Result&)> is_retryable,
      RetryCallback override_on_retry = nullptr) {
    std::chrono::milliseconds current_delay = config_.initial_delay;
    auto cb = override_on_retry ? override_on_retry : config_.on_retry;

    for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
      Result result = func();

      if (!result.is_success()) {
        // Retry only when the result is explicitly retryable and attempts
        // remain. Otherwise surface the failed result directly rather than
        // throwing RetryError (callers prefer a failed Result).
        if (is_retryable(result) && attempt < config_.max_retries) {
          LOG_WARN(
              "Request failed (attempt {}/{}), retrying after {} ms delay...",
              attempt + 1, config_.max_retries + 1, current_delay.count());

          if (cb) {
            std::string err_msg = "Request failed";
            if constexpr (requires(const Result& r) { r.error_message(); }) {
              err_msg = result.error_message();
            } else if constexpr (requires(const Result& r) { r.message; }) {
              err_msg = result.message;
            }
            cb(attempt + 1, config_.max_retries + 1, current_delay, err_msg);
          }

          std::this_thread::sleep_for(current_delay);
          auto delay_ms =
              std::chrono::duration<double, std::milli>(current_delay) *
              config_.backoff_factor;
          current_delay =
              std::chrono::duration_cast<std::chrono::milliseconds>(delay_ms);
          // Jitter the wait to avoid synchronized retries (thundering herd).
          if (config_.jitter > 0.0) {
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<double> dist(1.0 - config_.jitter,
                                                        1.0 + config_.jitter);
            const double factor = dist(rng);
            current_delay = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::duration<double, std::milli>(current_delay) * factor);
          }
          // Cap the wait AFTER jitter so delay never exceeds max_delay.
          if (current_delay > config_.max_delay) {
            current_delay = config_.max_delay;
          }
          continue;
        }
        if (!result.is_success() && attempt > 0) {
          LOG_ERROR("Request failed after {} attempts", attempt + 1);
        }
        return result;
      }

      if (attempt > 0) {
        LOG_INFO("Request succeeded on retry attempt {}/{}", attempt + 1,
                 config_.max_retries + 1);
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
}  // namespace qcode
