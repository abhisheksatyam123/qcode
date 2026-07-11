#pragma once
#include <ai/tui/bus/port.h>
#include <ai/tui/contract/event.h>
#include <ai/tui/state.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
#include <queue>
#include <chrono>

namespace ai {
namespace tui {

namespace bus {
class BusRuntime;
}

struct Toast {
    std::string message;
    std::string variant;
    std::chrono::steady_clock::time_point expires_at;
};

class AppStore {
public:
    explicit AppStore(bus::BusPort& bus);
    ChatState& state() { return state_; }
    const ChatState& state() const { return state_; }
    bool is_generating() const {
        return state_.is_generating->load(std::memory_order_acquire);
    }
    const std::string& session_id() const { return *state_.session_id; }
    const std::string& status() const { return status_; }
    const std::string& last_error() const { return last_error_; }

    void enqueue_prompt(const std::string& prompt);
    bool has_queued_prompt();
    std::string dequeue_prompt();
    void append_to_last_queued_prompt(const std::string& text);
    size_t queue_size() const;

    std::vector<Toast> toasts() const;
    void add_toast(const std::string& message, const std::string& variant = "info", int duration_ms = 5000);
    void expire_toasts();

    void append_chat_message(const std::string& role, const std::string& text);
    void append_assistant_chunk(const std::string& chunk);
    void append_reasoning(const std::string& chunk,
                          const std::string& signature = "");
    void set_generating(bool v);
    void set_session_id(const std::string& id);
    void set_status(const std::string& s);
    void set_error(const std::string& msg);
    void advance_frame();

    using Callback = std::function<void()>;
    bus::Subscription on_change(Callback cb);
    void wire();

private:
    ChatState state_;
    bus::BusPort& bus_;
    std::string status_ = "idle";
    std::string last_error_;
    std::vector<Toast> toasts_;
    mutable std::mutex toast_mutex_;

    std::deque<std::string> prompt_queue_; // Changed to deque for append access
    mutable std::mutex queue_mutex_;

    std::vector<std::pair<uint64_t, Callback>> callbacks_;
    std::vector<bus::Subscription> subs_;
    mutable std::mutex cb_mutex_;
    std::atomic<uint64_t> next_id_{1};
    bus::BusRuntime* runtime_ = nullptr;

    void notify();
    void notify_now();
    std::string latest_assistant_text() const;
    void remove_callback(uint64_t id);
};

} // namespace tui
} // namespace ai
