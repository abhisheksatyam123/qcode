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
#include <deque>
#include <chrono>

namespace ai {
namespace tui {

// ── Toast notification ────────────────────────────────────────────────────────
struct Toast {
    std::string message;
    std::string variant; // "info", "success", "warning", "error"
    std::chrono::steady_clock::time_point expires_at;
};

// ── AppStore: centralized reactive state ─────────────────────────────────────
class AppStore {
public:
    explicit AppStore(bus::BusPort& bus);

    ChatState& state() { return state_; }
    const ChatState& state() const { return state_; }

    bool is_generating() const { return *state_.is_generating; }
    const std::string& session_id() const { return *state_.session_id; }
    const std::string& status() const { return status_; }
    const std::string& last_error() const { return last_error_; }

    // Toasts
    const std::vector<Toast>& toasts() const { return toasts_; }
    void add_toast(const std::string& message, const std::string& variant = "info", int duration_ms = 5000);
    void expire_toasts();

    void append_chat_message(const std::string& role, const std::string& text);
    void update_last_assistant_message(const std::string& text);
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
    std::vector<Callback> callbacks_;
    std::vector<bus::Subscription> subs_;
    mutable std::mutex cb_mutex_;
    std::atomic<uint64_t> next_id_{1};

    void notify();
};

} // namespace tui
} // namespace ai
