#include <ai/tui/store.h>
#include <ai/tui/db.h>
#include <algorithm>
#include <ai/types/message.h>
#include <ai/logger.h>

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
    // Also add to messages_history for proper rendering
    if (role == "User") {
        state_.messages_history->push_back(ai::Message::user(text));
    } else if (role == "Assistant") {
        state_.messages_history->push_back(ai::Message::assistant(text));
    }
    // System messages are not added to messages_history (they're informational)
    notify();
}

void AppStore::update_last_assistant_message(const std::string& text) {
    // Update chat_history
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

    // Update messages_history: replace last Assistant entry or append
    if (!state_.messages_history->empty()) {
        auto& last = state_.messages_history->back();
        if (last.role == ai::kMessageRoleAssistant) {
            // Replace text of the last assistant message
            // ai::Message stores text and parts - update the text
            // We need to construct a new message since parts may be complex
            // For streaming text updates, replace last text part
            bool found_text = false;
            for (auto& part : last.content) {
                if (auto* tp = std::get_if<ai::TextContentPart>(&part)) {
                    tp->text = text;
                    found_text = true;
                    break;
                }
            }
            if (!found_text) {
                last.content.push_back(ai::TextContentPart{text});
            }
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
    status_ = s;
    if (s == "idle" || s == "error") {
        *state_.is_generating = false;
    }
    notify();
}

void AppStore::set_error(const std::string& msg) {
    last_error_ = msg;
    status_ = "error";
    *state_.is_generating = false;
    add_toast(msg, "error", 8000);
    notify();
}

void AppStore::advance_frame() {
    (*state_.generation_frame)++;
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
            // Final update: ensure the last assistant message has the complete text
            update_last_assistant_message(p.text);
            // Save to DB
            ai::tui::db::save_message(*state_.session_id, "Assistant", p.text);
        } else {
            update_last_assistant_message(p.text);
        }
    }));

    subs_.push_back(bus_.subscribe<ToolCallStarted>([this](const ToolCallStarted::Payload& p) {
        // Add a tool call entry to chat_history for display
        std::string tool_str = "  \u23f3 " + p.tool_name + "...";
        state_.chat_history->emplace_back("ToolCall", tool_str);
        // Also add to messages_history for proper rendering
        // Create a tool call part
        ai::ToolCallContentPart tc_part{p.tool_call_id, p.tool_name, p.arguments};
        ai::Message tool_msg = ai::Message::assistant_with_tools("", {tc_part});
        state_.messages_history->push_back(std::move(tool_msg));
        notify();
    }));

    subs_.push_back(bus_.subscribe<ToolCallCompleted>([this](const ToolCallCompleted::Payload& p) {
        // Remove the ephemeral "Running..." entry from chat_history
        for (int ci = static_cast<int>(state_.chat_history->size()) - 1; ci >= 0; --ci) {
            auto& entry = (*state_.chat_history)[ci];
            if (entry.first == "ToolCall" && entry.second.find(p.tool_name) != std::string::npos) {
                state_.chat_history->erase(state_.chat_history->begin() + ci);
                break;
            }
        }
        std::string result_str = p.is_error
            ? "  \u2716 " + p.tool_name + " failed (" + p.result.dump() + ")"
            : "  \u2714 " + p.tool_name + " (" + std::to_string((int)p.duration_ms) + "ms)";
        state_.chat_history->emplace_back("ToolResult", result_str);
        // Add tool result to messages_history
        ai::ToolResultContentPart tr_part{p.tool_call_id, p.result, p.is_error};
        ai::Message result_msg = ai::Message::tool_results({tr_part});
        state_.messages_history->push_back(std::move(result_msg));
        notify();
    }));

    subs_.push_back(bus_.subscribe<SessionStatusChanged>([this](const SessionStatusChanged::Payload& p) {
        set_status(p.status);
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
