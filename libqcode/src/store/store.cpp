#include <ai/tui/store.h>
#include <ai/tui/bus/impl.h>
#include <ai/tui/db.h>
#include <algorithm>
#include <ai/types/message.h>
#include <ai/logger.h>
#include <nlohmann/json.hpp>

namespace ai {
namespace tui {

AppStore::AppStore(bus::BusPort& bus) : bus_(bus) {
    runtime_ = dynamic_cast<bus::BusRuntime*>(&bus_);
}

std::vector<Toast> AppStore::toasts() const {
    std::lock_guard<std::mutex> lock(toast_mutex_);
    return toasts_;
}

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
    if (role == "User") {
        state_.messages_history->emplace_back(ai::Message::user(text));
    } else if (role == "Assistant") {
        state_.messages_history->emplace_back(ai::Message::assistant(text));
    } else {
        state_.messages_history->emplace_back(ai::Message::system(text));
    }
    notify();
}

void AppStore::append_assistant_chunk(const std::string& chunk) {
    if (chunk.empty()) return;

    if (!state_.messages_history->empty()) {
        auto& last = state_.messages_history->back();
        if (last.role == ai::kMessageRoleAssistant) {
            bool found_text = false;
            for (auto& part : last.content) {
                if (auto* tp = std::get_if<ai::TextContentPart>(&part)) {
                    tp->text += chunk;
                    found_text = true;
                    break;
                }
            }
            if (!found_text) {
                last.content.emplace_back(ai::TextContentPart{chunk});
            }
        } else {
            state_.messages_history->emplace_back(ai::Message::assistant(chunk));
        }
    } else {
        state_.messages_history->emplace_back(ai::Message::assistant(chunk));
    }
    notify();
}

void AppStore::append_reasoning(const std::string& chunk,
                                const std::string& signature) {
    if (state_.messages_history->empty() ||
        state_.messages_history->back().role != ai::kMessageRoleAssistant) {
        if (chunk.empty()) return;
        state_.messages_history->emplace_back(
            ai::Message::assistant_with_reasoning("", chunk, signature));
    } else {
        auto& last = state_.messages_history->back();
        bool found = false;
        for (auto& part : last.content) {
            if (auto* rp = std::get_if<ai::ReasoningContentPart>(&part)) {
                rp->text += chunk;
                if (!signature.empty()) rp->signature = signature;
                found = true;
                break;
            }
        }
        if (!found && !chunk.empty()) {
            last.content.emplace_back(
                ai::ReasoningContentPart{chunk, signature});
        }
    }
    notify();
}

std::string AppStore::latest_assistant_text() const {
    for (auto message = state_.messages_history->rbegin();
         message != state_.messages_history->rend(); ++message) {
        if (message->role != ai::kMessageRoleAssistant) continue;
        std::string text;
        for (const auto& part : message->content) {
            if (const auto* content =
                    std::get_if<ai::TextContentPart>(&part)) {
                text += content->text;
            }
        }
        if (!text.empty()) return text;
    }
    return {};
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

void AppStore::clear_prompt_queue() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        prompt_queue_.clear();
        *state_.queued_prompts = 0;
    }
    notify();
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
        callbacks_.push_back({id, std::move(cb)});
    }
    // RAII: destroying the returned Subscription unregisters this callback.
    return bus::Subscription([this, id]() { remove_callback(id); });
}

void AppStore::remove_callback(uint64_t id) {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it) {
        if (it->first == id) {
            callbacks_.erase(it);
            break;
        }
    }
}

void AppStore::wire() {
    using namespace contract;
    subs_.push_back(bus_.subscribe<MessageDelta>([this](const MessageDelta::Payload& p) {
        append_assistant_chunk(p.text);
        if (p.done) {
            set_generating(false);
            const auto final_text = latest_assistant_text();
            ai::tui::db::save_message(p.session_id, "Assistant", final_text);
        }
    }));
    subs_.push_back(bus_.subscribe<ReasoningDelta>([this](const ReasoningDelta::Payload& p) {
        append_reasoning(p.text, p.signature);
    }));
    // ... remaining wire implementation same as before
    subs_.push_back(bus_.subscribe<CompactionResult>([this](const CompactionResult::Payload& p) {
        if (!p.error.empty()) {
            state_.messages_history->emplace_back(ai::Message::system(p.error));
            add_toast(p.error, "error", 5000);
            set_status("idle");
            notify();
            return;
        }
        
        std::string summary_body =
            "This conversation was compacted into a handoff packet" +
            (p.wrote ? (" written to: " + p.todo_path) : "") + ".\n\n" + p.summary;

        std::string note = "Conversation compacted: " + std::to_string(p.original_size) +
                           " messages -> handoff packet";
        note += p.wrote ? ("\nTodo file: " + p.todo_path) : " (todo file write failed)";

        // Build the new sequenced message history containing all old messages,
        // the compaction summary inserted before the 'keep' recent messages,
        // and the 'keep' recent messages.
        const auto& hist = *state_.messages_history;
        ai::Messages new_messages;

        size_t cutoff_idx = (hist.size() > static_cast<size_t>(p.keep))
                            ? hist.size() - static_cast<size_t>(p.keep)
                            : 0;

        // Copy older messages
        for (size_t i = 0; i < cutoff_idx; ++i) {
            new_messages.push_back(hist[i]);
        }

        // Insert compaction note and user summary
        new_messages.push_back(ai::Message::system(note));
        new_messages.push_back(ai::Message::user(summary_body));

        // Copy keep recent messages
        for (size_t i = cutoff_idx; i < hist.size(); ++i) {
            new_messages.push_back(hist[i]);
        }

        state_.messages_history = std::make_shared<ai::Messages>(new_messages);

        // Overwrite history in SQLite database to persist the new sequenced order
        ai::tui::db::overwrite_session_history(*state_.session_id, *state_.messages_history);

        add_toast("Compaction complete", "success", 3000);
        set_status("idle");
        notify();
    }));

    subs_.push_back(bus_.subscribe<ToastRequested>([this](const ToastRequested::Payload& p) {
        add_toast(p.message, p.variant, p.duration_ms > 0 ? p.duration_ms : 3000);
    }));

    subs_.push_back(bus_.subscribe<ToolCallStarted>([this](const ToolCallStarted::Payload& p) {
        // Embed the unique tool_call_id so ToolCallCompleted can pair the result
        // with the exact started entry. Matching by tool_name alone is fragile
        // when multiple calls of the same tool run concurrently.
        ai::ToolCallContentPart tc_part{p.tool_call_id, p.tool_name, p.arguments};
        state_.messages_history->emplace_back(
            ai::Message::assistant_with_tools("", {tc_part}));
        // Structured JSON so session reload can rebuild pretty tool blocks.
        nlohmann::json call_json = {
            {"id", p.tool_call_id},
            {"name", p.tool_name},
            {"arguments", p.arguments},
        };
        ai::tui::db::save_message(p.session_id, "ToolCall", call_json.dump());
        notify();
    }));

    subs_.push_back(bus_.subscribe<ToolCallCompleted>([this](const ToolCallCompleted::Payload& p) {
        state_.messages_history->emplace_back(
            ai::Message::tool_results(
                {{p.tool_call_id, p.result, p.is_error, p.duration_ms}}));
        ++*state_.tool_call_count;
        *state_.total_tool_time_ms += p.duration_ms;
        nlohmann::json result_json = {
            {"tool_call_id", p.tool_call_id},
            {"tool_name", p.tool_name},
            {"result", p.result},
            {"is_error", p.is_error},
            {"duration_ms", p.duration_ms},
        };
        ai::tui::db::save_message(p.session_id, "ToolResult", result_json.dump());
        notify();
    }));

    subs_.push_back(bus_.subscribe<SessionStatusChanged>([this](const SessionStatusChanged::Payload& p) { set_status(p.status); }));

    subs_.push_back(bus_.subscribe<ErrorOccurred>([this](const ErrorOccurred::Payload& p) {
        // Any error/warning must clear the generating flag. Warnings previously
        // set status to "warn" and left is_generating stuck true, freezing Esc
        // and queue drain until a later idle event arrived (or never did).
        if (p.severity == "warning") {
            LOG_WARN("Store: warning message={}", p.message);
            set_generating(false);
            add_toast("\u26a0 " + p.message, "warning", 6000);
            if (status_ == "generating") {
                set_status("idle");
            } else {
                notify();
            }
        } else {
            set_error(p.message);
            std::string msg = p.message;
            if (!msg.starts_with("Error:") && !msg.starts_with("Exception:")) {
                msg = "Error: " + msg;
            }
            append_chat_message("System", msg);
            ai::tui::db::save_message(p.session_id, "System", msg);
        }
    }));

    subs_.push_back(bus_.subscribe<TokenUsageUpdated>([this](const TokenUsageUpdated::Payload& p) {
        *state_.total_prompt_tokens = p.prompt_tokens;
        *state_.total_completion_tokens = p.completion_tokens;
        *state_.total_tokens = p.total_tokens;
        // Calibration anchor: remember the actual prompt token count so the
        // next heuristic estimate can be corrected against ground truth.
        *state_.last_actual_prompt_tokens = p.prompt_tokens;
        notify();
    }));

    // Per-turn context snapshot (current context sent to the model). This is
    // distinct from total_tokens, which is the session lifetime total.
    subs_.push_back(bus_.subscribe<ContextSizeUpdated>([this](const ContextSizeUpdated::Payload& p) {
        *state_.current_context_tokens = p.context_tokens;
        notify();
    }));
}

void AppStore::notify() {
    if (runtime_ != nullptr && runtime_->is_draining()) {
        return;
    }
    notify_now();
}

void AppStore::notify_now() {
    std::vector<std::pair<uint64_t, Callback>> cbs;
    {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        cbs = callbacks_;
    }
    for (auto& entry : cbs) { entry.second(); }
}

} // namespace tui
} // namespace ai
