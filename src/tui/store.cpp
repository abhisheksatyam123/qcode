#include <ai/tui/store.h>
#include <ai/tui/db.h>
#include <algorithm>
#include <ai/types/message.h>
#include <ai/logger.h>

namespace ai {
namespace tui {

AppStore::AppStore(bus::BusPort& bus) : bus_(bus) {}

// ... existing methods ...
void AppStore::add_toast(const std::string& message, const std::string& variant, int duration_ms) {
    {
        std::lock_guard<std::mutex> lock(toast_mutex_);
        toasts_.push_back({
            .message = message,
            .variant = variant,
            .expires_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms)
        });
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
    if (role == "User") {
        state_.messages_history->push_back(ai::Message::user(text));
    } else if (role == "Assistant") {
        state_.messages_history->push_back(ai::Message::assistant(text));
    }
    notify();
}

void AppStore::update_last_assistant_message(const std::string& text) {
    if (!state_.chat_history->empty()) {
        auto& [role, content] = state_.chat_history->back();
        if (role == "Assistant") { content = text; }
        else { state_.chat_history->emplace_back("Assistant", text); }
    } else {
        state_.chat_history->emplace_back("Assistant", text);
    }

    if (!state_.messages_history->empty()) {
        auto& last = state_.messages_history->back();
        if (last.role == ai::kMessageRoleAssistant) {
            bool found_text = false;
            for (auto& part : last.content) {
                if (auto* tp = std::get_if<ai::TextContentPart>(&part)) {
                    tp->text = text;
                    found_text = true;
                    break;
                }
            }
            if (!found_text) { last.content.push_back(ai::TextContentPart{text}); }
        } else {
            state_.messages_history->push_back(ai::Message::assistant(text));
        }
    } else {
        state_.messages_history->push_back(ai::Message::assistant(text));
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
    LOG_DEBUG("Store: set_status s={}", s);
    status_ = s;
    if (state_.status) *state_.status = s;
    if (s == "idle" || s == "error") {
        *state_.is_generating = false;
    }
    notify();
}

void AppStore::set_error(const std::string& msg) {
    LOG_ERROR("Store: set_error msg={}", msg);
    last_error_ = msg;
    status_ = "error";
    if (state_.status) *state_.status = "error";
    if (state_.last_error) *state_.last_error = msg;
    *state_.is_generating = false;
    add_toast(msg, "error", 8000);
    notify();
}

void AppStore::advance_frame() {
    (*state_.generation_frame)++;
    expire_toasts();
    notify();
}

void AppStore::enqueue_prompt(const std::string& prompt) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        prompt_queue_.push_back(prompt);
        *state_.queued_prompts = static_cast<int>(prompt_queue_.size());
        LOG_DEBUG("Store: enqueue_prompt queue_size={}", static_cast<int>(prompt_queue_.size()));
    }
    notify();
}

bool AppStore::has_queued_prompt() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return !prompt_queue_.empty();
}

std::string AppStore::dequeue_prompt() {
    std::string prompt;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        prompt = prompt_queue_.front();
        prompt_queue_.pop_front();
        *state_.queued_prompts = static_cast<int>(prompt_queue_.size());
        LOG_DEBUG("Store: dequeue_prompt queue_size={}", static_cast<int>(prompt_queue_.size()));
    }
    notify();
    return prompt;
}

size_t AppStore::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return prompt_queue_.size();
}

void AppStore::append_to_last_queued_prompt(const std::string& text) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!prompt_queue_.empty()) {
        prompt_queue_.back() += "\n" + text;
        *state_.queued_prompts = static_cast<int>(prompt_queue_.size());
        LOG_DEBUG("Store: append_to_last_queued_prompt queue_size={}", static_cast<int>(prompt_queue_.size()));
    }
}

bus::Subscription AppStore::on_change(Callback cb) {
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        callbacks_.push_back(std::move(cb));
    }
    return bus::Subscription([]{});
}

void AppStore::wire() {
    using namespace contract;
    subs_.push_back(bus_.subscribe<MessageDelta>([this](const MessageDelta::Payload& p) {
        if (p.done) {
            set_generating(false);
            update_last_assistant_message(p.text);
            ai::tui::db::save_message(*state_.session_id, "Assistant", p.text);
        } else {
            update_last_assistant_message(p.text);
        }
    }));
    // ... remaining wire implementation same as before
    subs_.push_back(bus_.subscribe<ToolCallStarted>([this](const ToolCallStarted::Payload& p) {
        std::string tool_str = "  \u23f3 " + p.tool_name + "...";
        state_.chat_history->emplace_back("ToolCall", tool_str);
        ai::ToolCallContentPart tc_part{p.tool_call_id, p.tool_name, p.arguments};
        state_.messages_history->push_back(ai::Message::assistant_with_tools("", {tc_part}));
        notify();
    }));

    subs_.push_back(bus_.subscribe<ToolCallCompleted>([this](const ToolCallCompleted::Payload& p) {
        for (int ci = static_cast<int>(state_.chat_history->size()) - 1; ci >= 0; --ci) {
            auto& entry = (*state_.chat_history)[ci];
            if (entry.first == "ToolCall" && entry.second.find(p.tool_name) != std::string::npos) {
                state_.chat_history->erase(state_.chat_history->begin() + ci);
                break;
            }
        }
        std::string result_str = p.is_error ? "  \u2716 " + p.tool_name + " failed (" + p.result.dump() + ")" : "  \u2714 " + p.tool_name + " (" + std::to_string((int)p.duration_ms) + "ms)";
        state_.chat_history->emplace_back("ToolResult", result_str);
        state_.messages_history->push_back(ai::Message::tool_results({{p.tool_call_id, p.result, p.is_error, p.duration_ms}}));
        notify();
    }));

    subs_.push_back(bus_.subscribe<SessionStatusChanged>([this](const SessionStatusChanged::Payload& p) { set_status(p.status); }));

    subs_.push_back(bus_.subscribe<ErrorOccurred>([this](const ErrorOccurred::Payload& p) {
        if (p.severity == "warning") {
            LOG_WARN("Store: warning message={}", p.message);
            set_status("warn");
            *state_.is_generating = false;
            add_toast("\u26a0 " + p.message, "warning", 6000);
        } else {
            set_error(p.message);
            append_chat_message("System", "Error: " + p.message);
        }
    }));

    subs_.push_back(bus_.subscribe<TokenUsageUpdated>([this](const TokenUsageUpdated::Payload& p) {
        *state_.total_prompt_tokens = p.prompt_tokens;
        *state_.total_completion_tokens = p.completion_tokens;
        *state_.total_tokens = p.total_tokens;
    }));
}

void AppStore::notify() {
    std::vector<Callback> cbs;
    {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        cbs = callbacks_;
    }
    for (auto& cb : cbs) { cb(); }
}

} // namespace tui
} // namespace ai
