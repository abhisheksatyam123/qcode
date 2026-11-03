#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace qcode {
namespace tui {

// Coalesces redraw requests from worker threads into at most one redraw per
// `min_interval`.
//
// While a model streams, the bus, the store and the spinner each ask for a
// redraw, often hundreds of times a second. Every one of those used to post
// an FTXUI event, and every event costs a full frame (layout + full-screen
// write). The pacer turns any burst into one post per interval: the first
// request after an idle period is posted at once, so a lone update shows up
// with no added latency, and requests that arrive while a frame is already
// due are folded into it.
//
// Keyboard and mouse input do not go through the pacer: FTXUI redraws after
// them directly, so typing never waits for a frame slot.
class FramePacer {
public:
    using Clock = std::chrono::steady_clock;

    FramePacer(std::function<void()> post, std::chrono::milliseconds min_interval)
        : post_(std::move(post)), interval_(min_interval) {
        worker_ = std::thread([this] { run(); });
    }

    ~FramePacer() { stop(); }

    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    // Thread-safe. Cheap when a redraw is already pending.
    void request() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (pending_ || stopping_) return;
            pending_ = true;
        }
        cv_.notify_one();
    }

    // Stops the worker; later requests are dropped. Idempotent.
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stopping_ = true;
        }
        cv_.notify_one();
        std::lock_guard<std::mutex> join_lock(join_mu_);
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
            worker_.join();
        }
    }

    std::chrono::milliseconds interval() const { return interval_; }

private:
    void run() {
        std::unique_lock<std::mutex> lock(mu_);
        Clock::time_point last_post{};
        while (true) {
            cv_.wait(lock, [this] { return pending_ || stopping_; });
            if (stopping_) return;
            // Hold the request until the interval since the last post has
            // passed; everything requested meanwhile rides along.
            const auto due = last_post + interval_;
            if (Clock::now() < due) {
                cv_.wait_until(lock, due, [this] { return stopping_; });
                if (stopping_) return;
            }
            pending_ = false;
            last_post = Clock::now();
            lock.unlock();
            post_();
            lock.lock();
        }
    }

    std::function<void()> post_;
    const std::chrono::milliseconds interval_;
    std::mutex mu_;
    std::mutex join_mu_;
    std::condition_variable cv_;
    bool pending_ = false;
    bool stopping_ = false;
    std::thread worker_;
};

// Frame interval from $QCODE_TUI_FPS (default 60, clamped to 5..240).
inline std::chrono::milliseconds frame_interval_from_env(const char* fps_env) {
    int fps = 60;
    if (fps_env && *fps_env) {
        try {
            fps = std::stoi(fps_env);
        } catch (...) {
            fps = 60;
        }
    }
    if (fps < 5) fps = 5;
    if (fps > 240) fps = 240;
    return std::chrono::milliseconds(1000 / fps);
}

}  // namespace tui
}  // namespace qcode
