#pragma once

// Polyfill for std::jthread / std::stop_token on platforms whose libc++
// does not yet ship them (notably Android NDK r26).

#include <atomic>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L

#include <stop_token>

namespace qcode::compat {
using stop_token = std::stop_token;
using jthread = std::jthread;
}  // namespace qcode::compat

#else

namespace qcode::compat {

class stop_token {
 public:
  stop_token() noexcept = default;
  explicit stop_token(std::shared_ptr<std::atomic<bool>> flag) noexcept
      : stop_(std::move(flag)) {}

  [[nodiscard]] bool stop_requested() const noexcept {
    return stop_ && stop_->load(std::memory_order_acquire);
  }

 private:
  std::shared_ptr<std::atomic<bool>> stop_;
};

class jthread {
 public:
  jthread() noexcept = default;

  template <typename F>
  explicit jthread(F&& f) {
    auto stop = stop_;
    if constexpr (std::is_invocable_v<std::decay_t<F>, stop_token>) {
      t_ = std::thread([stop, fn = std::forward<F>(f)]() mutable {
        fn(stop_token{stop});
      });
    } else {
      t_ = std::thread(std::forward<F>(f));
    }
  }

  jthread(const jthread&) = delete;
  jthread& operator=(const jthread&) = delete;

  jthread(jthread&& other) noexcept
      : stop_(std::move(other.stop_)), t_(std::move(other.t_)) {
    other.stop_ = std::make_shared<std::atomic<bool>>(false);
  }

  jthread& operator=(jthread&& other) noexcept {
    if (this != &other) {
      request_stop();
      if (joinable()) {
        join();
      }
      stop_ = std::move(other.stop_);
      t_ = std::move(other.t_);
      other.stop_ = std::make_shared<std::atomic<bool>>(false);
    }
    return *this;
  }

  ~jthread() {
    request_stop();
    if (joinable()) {
      join();
    }
  }

  void request_stop() noexcept {
    if (stop_) {
      stop_->store(true, std::memory_order_release);
    }
  }

  [[nodiscard]] bool joinable() const noexcept { return t_.joinable(); }

  void join() { t_.join(); }

  void detach() { t_.detach(); }

 private:
  std::shared_ptr<std::atomic<bool>> stop_{
      std::make_shared<std::atomic<bool>>(false)};
  std::thread t_;
};

}  // namespace qcode::compat

#endif
