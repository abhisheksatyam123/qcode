#include <qcode/ui/app_store.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/session/token_budget.h>
#include <qcode/session/session_store.h>
#include <algorithm>
#include <climits>
#include <qcode/core/message.h>
#include <qcode/core/logger.h>
#include <qcode/core/errors.h>
#include <nlohmann/json.hpp>

namespace qcode {

AppStore::AppStore(bus::BusPort& bus) : bus_(bus) {
    runtime_ = dynamic_cast<bus::BusRuntime*>(&bus_);
    if (runtime_ != nullptr) {
        subs_.push_back(runtime_->on_drain_complete([this]() {
            if (pending_notify_.exchange(false, std::memory_order_acq_rel)) {
                notify_now();
            }
        }));
    }
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
            .message = format_user_facing_error(message, 120),
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
        state_.messages_history->emplace_back(qcode::Message::user(text));
    } else if (role == "Assistant") {
        state_.messages_history->emplace_back(qcode::Message::assistant(text));
    } else {
        state_.messages_history->emplace_back(qcode::Message::system(text));
    }
    notify();
}

void AppStore::append_assistant_chunk(const std::string& chunk) {
    if (chunk.empty()) return;

    if (!state_.messages_history->empty()) {
        auto& last = state_.messages_history->back();
        if (last.role == qcode::kMessageRoleAssistant) {
            bool found_text = false;
            for (auto& part : last.content) {
                if (auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                    tp->text += chunk;
                    found_text = true;
                    break;
                }
            }
            if (!found_text) {
                last.content.emplace_back(qcode::TextContentPart{chunk});
            }
        } else {
            state_.messages_history->emplace_back(qcode::Message::assistant(chunk));
        }
    } else {
        state_.messages_history->emplace_back(qcode::Message::assistant(chunk));
    }
    notify();
}

void AppStore::append_reasoning(const std::string& chunk,
                                const std::string& signature) {
    // Upstream opencode: strip [REDACTED] markers; drop whitespace-only
    // chunks so scrollback never gets a blank Thought row.
    std::string clean = chunk;
    {
        const std::string tag = "[REDACTED]";
        size_t p = 0;
        while ((p = clean.find(tag, p)) != std::string::npos) clean.erase(p, tag.size());
    }
    bool visible = false;
    for (char c : clean) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { visible = true; break; }
    }
    if (!visible && signature.empty()) return;
    const std::string& use = clean;
    // Start a fresh assistant message when the trailing one already carries
    // visible text — keeps per-step thinking blocks ordered like opencode's
    // part renderers instead of appending below earlier prose.
    const bool trailing_has_text =
        !state_.messages_history->empty() &&
        state_.messages_history->back().role == qcode::kMessageRoleAssistant &&
        state_.messages_history->back().has_text();
    if (state_.messages_history->empty() ||
        trailing_has_text ||
        state_.messages_history->back().role != qcode::kMessageRoleAssistant) {
        if (chunk.empty()) return;
        state_.messages_history->emplace_back(
            qcode::Message::assistant_with_reasoning("", use, signature));
    } else {
        auto& last = state_.messages_history->back();
        bool found = false;
        for (auto& part : last.content) {
            if (auto* rp = std::get_if<qcode::ReasoningContentPart>(&part)) {
                rp->text += use;
                if (!signature.empty()) rp->signature = signature;
                found = true;
                break;
            }
        }
        if (!found && !use.empty()) {
            last.content.emplace_back(
                qcode::ReasoningContentPart{use, signature});
        }
    }
    notify();
}

bool AppStore::is_live_session(const std::string& id) const {
    return id.empty() || id == session_id();
}

std::string AppStore::latest_assistant_text() const {
    for (auto message = state_.messages_history->rbegin();
         message != state_.messages_history->rend(); ++message) {
        if (message->role != qcode::kMessageRoleAssistant) continue;
        std::string text;
        for (const auto& part : message->content) {
            if (const auto* content =
                    std::get_if<qcode::TextContentPart>(&part)) {
                text += content->text;
            }
        }
        if (!text.empty()) return text;
    }
    return {};
}

void AppStore::set_generating(bool v) {
    *state_.is_generating = v;
    if (v) {
        status_ = "generating";
        if (state_.status) *state_.status = "generating";
    } else if (status_ == "generating" || status_ == "agent") {
        status_ = "idle";
        if (state_.status) *state_.status = "idle";
    }
    notify();
}

void AppStore::set_session_id(const std::string& id) {
    *state_.session_id = id;
    if (state_.session_title) {
        *state_.session_title = qcode::session::get_session_title(id);
    }
    // Swap the in-memory queue to the newly active session. Rows for the
    // previous session stay persisted; the new session's rows (if any)
    // resume where they left off.
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        prompt_queue_.clear();
        sync_queue_mirror_unlocked();
    }
    for (auto& prompt : qcode::session::queued_prompt_load(id)) {
        restore_queued_prompt(prompt);
    }
    // Restore persisted agent/reasoning modes (migration v7+ rows). Empty
    // strings mean "never changed" — keep current defaults in that case.
    {
        const auto [agent_mode, reasoning_mode] =
            qcode::session::get_session_modes(id);
        if (!agent_mode.empty() && state_.agent_mode) {
            *state_.agent_mode = agent_mode;
        }
        if (!reasoning_mode.empty() && state_.reasoning_mode) {
            *state_.reasoning_mode = reasoning_mode;
        }
    }
    notify();
}

void AppStore::set_session_title(const std::string& title) {
    if (state_.session_title) *state_.session_title = title;
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
    // Upstream opencode: error text committed to scrollback verbatim
    // (formatError order); no retry badge — backend auto-retries.
    LOG_ERROR("Store: set_error msg={}", msg);
    last_error_ = msg;
    status_ = "error";
    if (state_.status) *state_.status = "error";
    if (state_.last_error) *state_.last_error = msg;
    *state_.is_generating = false;
    add_toast(msg, "error", 8000);
    notify();
}

void AppStore::clear_error() {
    last_error_.clear();
    if (state_.last_error) state_.last_error->clear();
    if (status_ == "error") {
        status_ = "idle";
        if (state_.status) *state_.status = "idle";
    }
    {
        std::lock_guard<std::mutex> lock(toast_mutex_);
        std::erase_if(toasts_, [](const Toast& t) {
            return t.variant == "error" || t.variant == "warning";
        });
    }
    notify();
}

void AppStore::mark_retry_available(const std::string& prompt) {
    if (state_.last_user_prompt) *state_.last_user_prompt = prompt;
    if (state_.retry_available) *state_.retry_available = !prompt.empty();
    notify();
}

void AppStore::clear_retry() {
    if (state_.retry_available) *state_.retry_available = false;
    notify();
}

void AppStore::advance_frame() {
    (*state_.generation_frame)++;
    expire_toasts();
    notify();
}

void AppStore::sync_queue_mirror_unlocked() {
    *state_.queued_prompts = static_cast<int>(prompt_queue_.size());
    if (state_.queued_prompt_texts) {
        state_.queued_prompt_texts->assign(prompt_queue_.begin(),
                                           prompt_queue_.end());
    }
}

void AppStore::enqueue_prompt(const std::string& prompt) {
    // Queued prompts live in prompt_queue_ / queued_prompt_texts and are rendered
    // in the dedicated queued prompt cards section. They enter messages_history
    // and session history only when dequeued and actually executed, avoiding
    // premature history corruption and duplicate rendering in qcode-tui.
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        prompt_queue_.push_back(prompt);
        sync_queue_mirror_unlocked();
        LOG_DEBUG("Store: enqueue_prompt queue_size={}",
                  static_cast<int>(prompt_queue_.size()));
    }
    // Persist so the queue survives restarts.
    if (!session_id().empty()) {
        qcode::session::queued_prompt_add(session_id(), prompt);
    }
    notify();
}

void AppStore::restore_queued_prompt(const std::string& prompt) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    prompt_queue_.push_back(prompt);
    sync_queue_mirror_unlocked();
    LOG_DEBUG("Store: restore_queued_prompt queue_size={}",
              static_cast<int>(prompt_queue_.size()));
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
        sync_queue_mirror_unlocked();
        LOG_DEBUG("Store: dequeue_prompt queue_size={}",
                  static_cast<int>(prompt_queue_.size()));
    }
    if (!session_id().empty()) {
        qcode::session::queued_prompt_pop(session_id());
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
        sync_queue_mirror_unlocked();
    }
    // User-initiated clear also drops persisted rows so they never resume.
    if (!session_id().empty()) {
        qcode::session::queued_prompt_clear(session_id());
    }
    notify();
}

void AppStore::clear_prompt_queue_memory_only() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        prompt_queue_.clear();
        sync_queue_mirror_unlocked();
    }
    notify();
}

bool AppStore::remove_queued_prompt(size_t index_1based) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (index_1based >= 1 && index_1based <= prompt_queue_.size()) {
            auto it = prompt_queue_.begin();
            std::advance(it, static_cast<long>(index_1based - 1));
            prompt_queue_.erase(it);
            sync_queue_mirror_unlocked();
            removed = true;
        }
    }
    if (removed && !session_id().empty()) {
        qcode::session::queued_prompt_remove_at(session_id(), index_1based);
    }
    if (removed) notify();
    return removed;
}

void AppStore::append_to_last_queued_prompt(const std::string& text) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!prompt_queue_.empty()) {
            prompt_queue_.back() += "\n" + text;
            sync_queue_mirror_unlocked();
            LOG_DEBUG("Store: append_to_last_queued_prompt queue_size={}",
                      static_cast<int>(prompt_queue_.size()));
        }
    }
    notify();
}

std::vector<std::string> AppStore::queued_prompts_snapshot() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return std::vector<std::string>(prompt_queue_.begin(), prompt_queue_.end());
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
        if (!is_live_session(p.session_id)) return;
        append_assistant_chunk(p.text);
        if (p.done) {
            set_generating(false);
            const auto final_text = latest_assistant_text();
            qcode::session::save_message(p.session_id, "Assistant", final_text);
        }
    }));
    subs_.push_back(bus_.subscribe<ReasoningDelta>([this](const ReasoningDelta::Payload& p) {
        if (!is_live_session(p.session_id)) return;
        append_reasoning(p.text, p.signature);
    }));
    // ... remaining wire implementation same as before
    subs_.push_back(bus_.subscribe<CompactionResult>([this](const CompactionResult::Payload& p) {
        if (!p.error.empty()) {
            state_.messages_history->emplace_back(qcode::Message::system(p.error));
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

        // Replace history with the handoff summary + recent keep-tail messages.
        // Older turns are dropped from the UI and from subsequent model context
        // (apply_compaction_cutoff also cuts at the summary marker).
        const auto& hist = *state_.messages_history;
        qcode::Messages new_messages;
        new_messages.push_back(qcode::Message::system(note));
        new_messages.push_back(qcode::Message::user(summary_body));

        auto is_prior_compaction_msg = [](const qcode::Message& m) {
            if (m.role == qcode::kMessageRoleSystem) {
                for (const auto& part : m.content) {
                    if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                        if (tp->text.find("Conversation compacted:") !=
                            std::string::npos) {
                            return true;
                        }
                    }
                }
            }
            if (m.role == qcode::kMessageRoleUser) {
                for (const auto& part : m.content) {
                    if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                        if (tp->text.find(
                                "This conversation was compacted into a "
                                "handoff packet") != std::string::npos) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };

        size_t keep_start = (hist.size() > static_cast<size_t>(p.keep))
                                ? hist.size() - static_cast<size_t>(p.keep)
                                : 0;
        for (size_t i = keep_start; i < hist.size(); ++i) {
            // Drop prior handoff packets so the new summary stays the cutoff.
            if (is_prior_compaction_msg(hist[i])) continue;
            new_messages.push_back(hist[i]);
        }

        state_.messages_history = std::make_shared<qcode::Messages>(new_messages);

        // Context window resets to the compacted set (system notes are stripped
        // before generation; estimate matches what apply_compaction_cutoff keeps).
        qcode::Messages context_view = qcode::apply_compaction_cutoff(*state_.messages_history);
        context_view.erase(
            std::remove_if(context_view.begin(), context_view.end(),
                           [](const qcode::Message& m) {
                               return m.role == qcode::kMessageRoleSystem;
                           }),
            context_view.end());
        const int reset_tokens = static_cast<int>(estimate_tokens(context_view));
        *state_.current_context_tokens = reset_tokens;
        *state_.last_estimated_tokens = reset_tokens;
        *state_.last_actual_prompt_tokens = 0;
        *state_.consecutive_prunes = 0;
        *state_.history_window_start = 0;
        *state_.auto_scroll = true;
        *state_.scroll_line = INT_MAX;
        bus_.publish<ContextSizeUpdated>({.context_tokens = reset_tokens});

        // Overwrite history in SQLite database to persist the compact set
        qcode::session::overwrite_session_history(*state_.session_id, *state_.messages_history);

        add_toast("Compaction complete", "success", 3000);
        set_status("idle");
        notify();
    }));

    subs_.push_back(bus_.subscribe<ToastRequested>([this](const ToastRequested::Payload& p) {
        add_toast(p.message, p.variant, p.duration_ms > 0 ? p.duration_ms : 3000);
    }));

    subs_.push_back(bus_.subscribe<ToolCallStarted>([this](const ToolCallStarted::Payload& p) {
        if (!is_live_session(p.session_id)) return;
        // Embed the unique tool_call_id so ToolCallCompleted can pair the result
        // with the exact started entry. Matching by tool_name alone is fragile
        // when multiple calls of the same tool run concurrently.
        qcode::ToolCallContentPart tc_part{p.tool_call_id, p.tool_name, p.arguments};
        state_.messages_history->emplace_back(
            qcode::Message::assistant_with_tools("", {tc_part}));
        // Structured JSON so session reload can rebuild pretty tool blocks.
        nlohmann::json call_json = {
            {"id", p.tool_call_id},
            {"name", p.tool_name},
            {"arguments", p.arguments},
        };
        qcode::session::save_message(p.session_id, "ToolCall", call_json.dump());
        notify();
    }));

    subs_.push_back(bus_.subscribe<ToolCallCompleted>([this](const ToolCallCompleted::Payload& p) {
        if (!is_live_session(p.session_id)) return;
        state_.messages_history->emplace_back(
            qcode::Message::tool_results(
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
        qcode::session::save_message(p.session_id, "ToolResult", result_json.dump());
        notify();
    }));

    subs_.push_back(bus_.subscribe<SessionStatusChanged>([this](const SessionStatusChanged::Payload& p) {
        if (!is_live_session(p.session_id)) return;
        set_status(p.status);
    }));

    subs_.push_back(bus_.subscribe<ErrorOccurred>([this](const ErrorOccurred::Payload& p) {
        if (!is_live_session(p.session_id)) return;
        // Upstream parity: "info" = retry progress toast, turn keeps running.
        if (p.severity == "info") {
            add_toast(p.message, "info", 3500);
            notify();
            return;
        }
        // Abort is near-silent upstream ("Session aborted" notification only,
        // no scrollback error, no latch).
        if (p.message == "Generation stopped" || p.message == "Generation stopped." ||
            p.message.find("aborted") != std::string::npos ||
            p.message.find("Aborted") != std::string::npos) {
            LOG_INFO("Store: generation aborted (silent)");
            set_generating(false);
            if (status_ == "generating" || status_ == "agent") set_status("idle");
            else notify();
            return;
        }
        const std::string msg = format_user_facing_error(p.message);
        if (p.severity == "warning") {
            // Upstream: warnings/empty-responses are transient toasts only —
            // no status latch, no retry badge.
            LOG_WARN("Store: warning message={}", msg);
            set_generating(false);
            add_toast(msg, "warning", 6000);
            if (status_ == "generating" || status_ == "agent") {
                set_status("idle");
            } else {
                notify();
            }
        } else {
            set_error(msg);
            std::string chat = msg;
            if (!chat.starts_with("Error:") && !chat.starts_with("Exception:")) {
                chat = "Error: " + chat;
            }
            append_chat_message("System", chat);
            qcode::session::save_message(p.session_id, "System", chat);
        }
    }));

    subs_.push_back(bus_.subscribe<TokenUsageUpdated>([this](const TokenUsageUpdated::Payload& p) {
        // Accumulate: TokenUsageUpdated carries the LATEST turn's absolute
        // counts, but the Stats tab (TUI) and backend /stats endpoint expect the
        // session-lifetime cumulative total. The database holds the authoritative
        // historical total, so persist the per-turn delta and accumulate in
        // memory as well.
        *state_.total_prompt_tokens += p.prompt_tokens;
        *state_.total_completion_tokens += p.completion_tokens;
        *state_.total_tokens += p.total_tokens;
        // Cache-hit + thinking-token mirrors for the header (latest turn).
        *state_.last_cached_prompt_tokens = p.cached_prompt_tokens;
        *state_.last_reasoning_tokens = p.reasoning_tokens;
        qcode::session::persist_session_token_stats(
            session_id(), p.prompt_tokens, p.completion_tokens, p.total_tokens);
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
        pending_notify_.store(true, std::memory_order_release);
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
    for (auto& entry : cbs) {
        if (entry.second) {
            try {
                entry.second();
            } catch (const std::exception& e) {
                LOG_ERROR("AppStore: exception in subscriber callback: {}", e.what());
            } catch (...) {
                LOG_ERROR("AppStore: unknown exception in subscriber callback");
            }
        }
    }
}

} // namespace qcode
