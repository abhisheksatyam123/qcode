#pragma once

#include <qcode/core/logger.h>
#include <qcode/core/retry_error.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <random>
#include <thread>
#include <vector>

namespace qcode {
namespace retry {

using RetryCallback = std::function<void(
    int attempt, int total_attempts, std::chrono::milliseconds delay,
    const std::string& error_msg)>;

struct RetryConfig {
  // Mirrors upstream opencode session/retry.ts: RETRY_MAX_RETRIES = 5.
  int max_retries = 5;
  std::chrono::milliseconds initial_delay{2000};  // RETRY_INITIAL_DELAY
  double backoff_factor = 2.0;                    // RETRY_BACKOFF_FACTOR
  // Cap on a single backoff wait when the provider gives no Retry-After
  // hint (RETRY_MAX_DELAY_NO_HEADERS). Header-provided delays may exceed it.
  std::chrono::milliseconds max_delay{30000};
  // One-sided fractional jitter, matching RETRY_JITTER_FACTOR: wait =
  // base + base * jitter * rand(0,1).
  double jitter = 0.25;
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
    auto cb = override_on_retry ? override_on_retry : config_.on_retry;

    for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
      Result result = func();

      if (!result.is_success()) {
        // Retry only when the result is explicitly retryable and attempts
        // remain. Otherwise surface the failed result directly rather than
        // throwing RetryError (callers prefer a failed Result).
        if (is_retryable(result) && attempt < config_.max_retries) {
          const auto wait = next_wait(attempt + 1, extract_retry_after(result));
          LOG_WARN(
              "Request failed (attempt {}/{}), retrying after {} ms delay...",
              attempt + 1, config_.max_retries + 1, wait.count());

          if (cb) {
            std::string err_msg = "Request failed";
            if constexpr (requires(const Result& r) { r.error_message(); }) {
              err_msg = result.error_message();
            } else if constexpr (requires(const Result& r) { r.message; }) {
              err_msg = result.message;
            }
            cb(attempt + 1, config_.max_retries + 1, wait, err_msg);
          }

          std::this_thread::sleep_for(wait);
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
  // Upstream SessionRetry.exponential: base = initial * factor^(attempt-1),
  // then one-sided jitter base + base * jitter * rand(0,1).
  std::chrono::milliseconds exponential(int attempt_1based) const {
    const double base = static_cast<double>(config_.initial_delay.count()) *
                        std::pow(config_.backoff_factor, attempt_1based - 1);
    return std::chrono::milliseconds(
        static_cast<long long>(std::ceil(base)));
  }

  std::chrono::milliseconds apply_jitter(
      std::chrono::milliseconds base) const {
    if (config_.jitter <= 0.0) return base;
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double waited = static_cast<double>(base.count()) *
                          (1.0 + config_.jitter * dist(rng));
    return std::chrono::milliseconds(static_cast<long long>(std::ceil(waited)));
  }

  // Upstream SessionRetry.delay(): honor the provider's Retry-After hint when
  // present (it may exceed the no-header cap); otherwise exponential backoff
  // with jitter capped at max_delay.
  std::chrono::milliseconds next_wait(
      int attempt_1based, std::optional<long long> retry_after_ms) const {
    if (retry_after_ms.has_value() && *retry_after_ms > 0) {
      constexpr long long kMax32BitMs = 2147483647LL;  // setTimeout ceiling
      return std::chrono::milliseconds(std::min(*retry_after_ms, kMax32BitMs));
    }
    const auto base = exponential(attempt_1based);
    const auto jittered = apply_jitter(base);
    if (jittered > config_.max_delay) return config_.max_delay;
    return jittered;
  }

  template <typename Result>
  std::optional<long long> extract_retry_after(const Result& result) const {
    if constexpr (requires(const Result& r) { r.retry_after_ms; }) {
      if (result.retry_after_ms.has_value()) return *result.retry_after_ms;
    }
    return std::nullopt;
  }

  RetryConfig config_;
};

}  // namespace retry
}  // namespace qcode
