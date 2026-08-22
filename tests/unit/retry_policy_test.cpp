#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <qcode/core/retry_policy.h>
#include <qcode/core/generate_options.h>
#include <chrono>

namespace qcode {
namespace test {

struct MockTestResult {
  bool success{false};
  bool retryable{false};
  std::string message;

  bool is_success() const { return success; }
};

TEST(RetryPolicyTest, SucceedsOnFirstAttempt) {
  retry::RetryConfig config;
  config.max_retries = 3;
  config.initial_delay = std::chrono::milliseconds(10);
  retry::RetryPolicy policy(config);

  int call_count = 0;
  auto result = policy.execute_with_retry<std::function<MockTestResult()>, MockTestResult>(
      [&call_count]() -> MockTestResult {
        call_count++;
        return MockTestResult{.success = true, .retryable = false, .message = "ok"};
      },
      [](const MockTestResult& res) { return res.retryable; });

  EXPECT_TRUE(result.is_success());
  EXPECT_EQ(call_count, 1);
}

TEST(RetryPolicyTest, FailsImmediatelyWhenNotRetryable) {
  retry::RetryConfig config;
  config.max_retries = 3;
  config.initial_delay = std::chrono::milliseconds(10);
  retry::RetryPolicy policy(config);

  int call_count = 0;
  auto result = policy.execute_with_retry<std::function<MockTestResult()>, MockTestResult>(
      [&call_count]() -> MockTestResult {
        call_count++;
        return MockTestResult{.success = false, .retryable = false, .message = "401 Unauthorized"};
      },
      [](const MockTestResult& res) { return res.retryable; });

  EXPECT_FALSE(result.is_success());
  EXPECT_EQ(result.message, "401 Unauthorized");
  EXPECT_EQ(call_count, 1);
}

TEST(RetryPolicyTest, RetriesAndSucceedsOnSecondAttempt) {
  retry::RetryConfig config;
  config.max_retries = 3;
  config.initial_delay = std::chrono::milliseconds(10);
  config.jitter = 0.0;
  retry::RetryPolicy policy(config);

  int call_count = 0;
  auto start = std::chrono::steady_clock::now();

  auto result = policy.execute_with_retry<std::function<MockTestResult()>, MockTestResult>(
      [&call_count]() -> MockTestResult {
        call_count++;
        if (call_count == 1) {
          return MockTestResult{.success = false, .retryable = true, .message = "429 Too Many Requests"};
        }
        return MockTestResult{.success = true, .retryable = false, .message = "success after retry"};
      },
      [](const MockTestResult& res) { return res.retryable; });

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  EXPECT_TRUE(result.is_success());
  EXPECT_EQ(result.message, "success after retry");
  EXPECT_EQ(call_count, 2);
  EXPECT_GE(duration.count(), 8); // slept at least initial_delay (10ms)
}

TEST(RetryPolicyTest, ExhaustsMaxRetriesAndReturnsFailure) {
  retry::RetryConfig config;
  config.max_retries = 2;
  config.initial_delay = std::chrono::milliseconds(10);
  config.backoff_factor = 2.0;
  config.jitter = 0.0;
  retry::RetryPolicy policy(config);

  int call_count = 0;
  auto result = policy.execute_with_retry<std::function<MockTestResult()>, MockTestResult>(
      [&call_count]() -> MockTestResult {
        call_count++;
        return MockTestResult{.success = false, .retryable = true, .message = "503 Service Unavailable"};
      },
      [](const MockTestResult& res) { return res.retryable; });

  EXPECT_FALSE(result.is_success());
  EXPECT_EQ(result.message, "503 Service Unavailable");
  // Total attempts = 1 initial + 2 retries = 3 total calls
  EXPECT_EQ(call_count, 3);
}

TEST(RetryPolicyTest, DelayCapAndJitterApplied) {
  retry::RetryConfig config;
  config.max_retries = 2;
  config.initial_delay = std::chrono::milliseconds(100);
  config.backoff_factor = 10.0; // would jump to 1000ms
  config.max_delay = std::chrono::milliseconds(150); // cap at 150ms
  config.jitter = 0.1; // 10% jitter
  retry::RetryPolicy policy(config);

  int call_count = 0;
  auto start = std::chrono::steady_clock::now();

  auto result = policy.execute_with_retry<std::function<MockTestResult()>, MockTestResult>(
      [&call_count]() -> MockTestResult {
        call_count++;
        return MockTestResult{.success = false, .retryable = true, .message = "Rate limit"};
      },
      [](const MockTestResult& res) { return res.retryable; });

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  EXPECT_FALSE(result.is_success());
  EXPECT_EQ(call_count, 3);
  // First sleep ~100ms (+/- 10%), second sleep capped at ~150ms (+/- 10%), total around 220-280ms
  EXPECT_GE(duration.count(), 180);
  EXPECT_LE(duration.count(), 400);
}

TEST(RetryPolicyTest, WorksWithGenerateResult) {
  retry::RetryConfig config;
  config.max_retries = 1;
  config.initial_delay = std::chrono::milliseconds(5);
  config.jitter = 0.0;
  retry::RetryPolicy policy(config);

  int call_count = 0;
  auto result = policy.execute_with_retry<std::function<GenerateResult()>, GenerateResult>(
      [&call_count]() -> GenerateResult {
        call_count++;
        if (call_count == 1) {
          GenerateResult r("HTTP 429 error");
          r.is_retryable = true;
          return r;
        }
        GenerateResult r;
        r.text = "Hello world";
        r.finish_reason = kFinishReasonStop;
        return r;
      },
      [](const GenerateResult& res) { return res.is_retryable.value_or(false); });

  EXPECT_TRUE(result.is_success());
  EXPECT_EQ(result.text, "Hello world");
  EXPECT_EQ(call_count, 2);
}

TEST(RetryPolicyTest, RetryCallbackInvokedWithDetails) {
  retry::RetryConfig config;
  config.max_retries = 2;
  config.initial_delay = std::chrono::milliseconds(5);
  config.jitter = 0.0;
  retry::RetryPolicy policy(config);

  int callback_count = 0;
  int last_attempt = 0;
  int last_total = 0;
  std::string last_err;

  auto cb = [&](int attempt, int total, std::chrono::milliseconds delay, const std::string& err) {
    callback_count++;
    last_attempt = attempt;
    last_total = total;
    last_err = err;
  };

  int call_count = 0;
  auto result = policy.execute_with_retry<std::function<GenerateResult()>, GenerateResult>(
      [&call_count]() -> GenerateResult {
        call_count++;
        if (call_count == 1) {
          GenerateResult r("HTTP 429 error");
          r.is_retryable = true;
          return r;
        }
        GenerateResult r;
        r.text = "Success on retry";
        r.finish_reason = kFinishReasonStop;
        return r;
      },
      [](const GenerateResult& res) { return res.is_retryable.value_or(false); },
      cb);

  EXPECT_TRUE(result.is_success());
  EXPECT_EQ(call_count, 2);
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(last_attempt, 1);
  EXPECT_EQ(last_total, 3);
  EXPECT_EQ(last_err, "HTTP 429 error");
}

}  // namespace test
}  // namespace qcode
