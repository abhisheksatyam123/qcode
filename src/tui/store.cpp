#include <ai/tui/store.h>
#include <algorithm>

namespace ai {
namespace tui {

AppStore::AppStore(bus::BusPort& bus) : bus_(bus) {}

void AppStore::add_toast(const std::string& message, const std::string& variant, int duration_ms) {
    {
        std::lock_guard<std::mutex> lock(toast_mutex_);
        toasts_.push_back({
            .message = message,
            .variant = variant,
            .expires_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms)
        });
        // Keep max 5 toasts
        if (toasts_.size() > 5) {
            toasts_.erase(toasts_.begin());
        }
    }
    notify();
}

void AppStore::expire_toasts() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(toast_mutex_);
    auto it = std::remove_if(toasts_.begin(), toasts_.end(),
        [now](const Toast& t) { return now >= t.expires_at; });
    if (it != toasts_.end()) {
        toasts_.erase(it, toasts_.end());
        notify();
    }
}

void AppStore::append_chat_message(const std::string& role, const std::string& text) {
    state_.chat_history->emplace_back(role, text);
    notify();
}

void AppStore::update_last_assistant_message(const std::string& text) {
    if (!state_.chat_history->empty()) {
        auto& [role, content] = state_.chat_history->back();
        if (role == "Assistant") {
            content = text;
        } else {
            state_.chat_history->emplace_back("Assistant", text);
        }
    } else {
        state_.chat_history->emplace_back("Assistant", text);
    }
    notify();
}

void AppStore::set_generating(bool v) {
    *state_.is_generating = v;
    notify();
}

void AppStore::set_session_id(const std::string& id) {
    *state_.session_id = id;
    notify();
}

void AppStore::set_status(const std::string& s) {
    status_ = s;
    notify();
}

void AppStore::set_error(const std::string& msg) {
    last_error_ = msg;
    status_ = "error";
    // Auto-show a toast for errors
    add_toast(msg, "error", 8000);
    notify();
}

void AppStore::advance_frame() {
    (*state_.generation_frame)++;
    // Also expire toasts on each frame advance
    expire_toasts();
    notify();
}

bus::Subscription AppStore::on_change(Callback cb) {
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        callbacks_.push_back(std::move(cb));
    }
    return bus::Subscription([]{}); // no-op unsubscribe (callbacks live for app lifetime)
}

void AppStore::wire() {
    using namespace contract;

    subs_.push_back(bus_.subscribe<MessageDelta>([this](const MessageDelta::Payload& p) {
        if (p.done) {
            set_generating(false);
        } else {
            update_last_assistant_message(p.text);
        }
    }));

    subs_.push_back(bus_.subscribe<SessionStatusChanged>([this](const SessionStatusChanged::Payload& p) {
        set_status(p.status);
        if (p.status == "idle") {
            add_toast("Generation complete", "success", 3000);
        } else if (p.status == "generating") {
            add_toast("Generating...", "info", 2000);
        }
    }));

    subs_.push_back(bus_.subscribe<ErrorOccurred>([this](const ErrorOccurred::Payload& p) {
        set_error(p.message);
        append_chat_message("System", "Error: " + p.message);
    }));

    subs_.push_back(bus_.subscribe<TokenUsageUpdated>([this](const TokenUsageUpdated::Payload& p) {
        *state_.total_prompt_tokens = p.prompt_tokens;
        *state_.total_completion_tokens = p.completion_tokens;
        *state_.total_tokens = p.total_tokens;
    }));

    subs_.push_back(bus_.subscribe<ToastRequested>([this](const ToastRequested::Payload& p) {
        add_toast(p.message, p.variant, p.duration_ms);
    }));
}

void AppStore::notify() {
    std::vector<Callback> cbs;
    {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        cbs = callbacks_;
    }
    for (auto& cb : cbs) {
        cb();
    }
}

} // namespace tui
} // namespace ai
