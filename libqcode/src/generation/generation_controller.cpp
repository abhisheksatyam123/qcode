#include <qcode/generation/generation_controller.h>

#include <qcode/compaction/compaction_request.h>
#include <qcode/session/session_title.h>
#include <qcode/core/logger.h>
#include <qcode/core/perf.h>
#include <qcode/generation/generation_service.h>
#include <qcode/generation/turn_prefix.h>
#include <qcode/session/token_budget.h>
#include <qcode/core/event.h>
#include <qcode/session/session_store.h>
#include <qcode/transform/provider_transform.h>

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <utility>

namespace qcode {

namespace {

// True when some tool call has no matching tool result, i.e. when
// close_unpaired_tool_calls would synthesize one. Ids only, no payload copies.
bool has_unpaired_tool_calls(const Messages& messages) {
    std::unordered_set<std::string> result_ids;
    for (const auto& msg : messages) {
        for (const auto& part : msg.content) {
            if (const auto* tr = std::get_if<ToolResultContentPart>(&part);
                tr && !tr->tool_call_id.empty()) {
                result_ids.insert(tr->tool_call_id);
            }
        }
    }
    for (const auto& msg : messages) {
        for (const auto& part : msg.content) {
            const auto* call = std::get_if<ToolCallContentPart>(&part);
            if (call && !call->id.empty() && !result_ids.contains(call->id)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

// Decides whether `prompt` still needs a visible User message appended to
// history. Guards against DUPLICATE user prompts after retries/aborts: if
// the trailing conversation already ends with this exact user prompt (an
// aborted turn leaves it in place with no assistant reply yet), re-sending
// must reuse it rather than append a second copy.
bool should_append_user_message(const qcode::Messages& history,
                                const std::string& prompt) {
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->role == kMessageRoleUser && !it->has_tool_results()) {
            return it->get_text() != prompt;
        }
        if (it->role == kMessageRoleAssistant) {
            // A text-bearing assistant reply closes the turn: fresh send.
            // A text-less assistant holding only tool calls is an aborted
            // mid-tool artifact — keep scanning for the open user prompt.
            bool has_text = false;
            for (const auto& part : it->content) {
                if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                    if (!tp->text.empty()) has_text = true;
                }
            }
            if (has_text) return true;
        }
    }
    return true;
}

GenerationController::GenerationController(
    AppStore& store,
    std::shared_ptr<bus::BusRuntime> bus,
    std::shared_ptr<std::atomic<bool>> app_running)
    : store_(store),
      bus_(std::move(bus)),
      app_running_(std::move(app_running)),
      busy_(std::make_shared<std::atomic<bool>>(false)) {}

GenerationController::~GenerationController() { shutdown(); }

std::string GenerationController::running_session_id() const {
    if (!is_busy()) return "";
    std::lock_guard<std::mutex> lock(active_session_mutex_);
    return active_session_id_;
}

bool GenerationController::is_active() const noexcept {
    const auto& st =
        store_.state().status ? *store_.state().status : std::string{};
    return store_.is_generating() || is_busy() || st == "generating" ||
           st == "agent";
}

void GenerationController::request_abort() {
    // Stop the in-flight turn. Queued prompts are kept but paused until the
    // user sends again, so Esc never hands off to another turn by itself.
    auto& state = store_.state();
    if (state.abort_flag) {
        state.abort_flag->store(true, std::memory_order_release);
    }
    queue_paused_ = true;
    abort_requested_ns_.store(
        std::chrono::steady_clock::now().time_since_epoch().count(),
        std::memory_order_relaxed);
    if (worker_.joinable()) {
        worker_.request_stop();
    }
    // Hold auto-start so a second Esc ("force stop") cannot land on a
    // queued Grok/Cursor turn that spawned the instant this one died.
    queue_resume_at_ = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(2000);
}

void GenerationController::prepare_session_switch() {
    request_abort();
    store_.set_generating(false);
    store_.set_status("idle");
}

void GenerationController::force_stop_ui() {
    prepare_session_switch();
    const auto queued_count = store_.queue_size();
    if (queued_count > 0) {
        store_.clear_prompt_queue();
        LOG_INFO("GenerationController: force_stop_ui cleared {} queued prompts",
                 queued_count);
    }
    if (store_.state().last_user_prompt &&
        !store_.state().last_user_prompt->empty()) {
        store_.mark_retry_available(*store_.state().last_user_prompt);
        std::string msg = "Generation force-stopped";
        if (queued_count > 0) {
            msg += " (" + std::to_string(queued_count) + " queued dropped)";
        }
        msg += " · /retry to resend";
        store_.add_toast(msg, "warning", 2500);
    } else {
        std::string msg = "Generation force-stopped";
        if (queued_count > 0) {
            msg += " (" + std::to_string(queued_count) + " queued dropped)";
        }
        store_.add_toast(msg, "warning", 2500);
    }
}

void GenerationController::spawn(std::string prompt, GenerationRequest request) {
    // Never touch the running turn's abort flag here: clearing it used to
    // un-stop a turn the user had just stopped with Esc.
    queue_paused_ = false;
    if (is_busy()) {
        const bool stopping =
            store_.state().abort_flag && store_.state().abort_flag->load();
        store_.enqueue_prompt(std::move(prompt));
        const auto n = store_.queue_size();
        store_.add_toast(stopping ? "Prompt queued — runs once the stop finishes"
                                  : "Prompt queued — joins the next model request",
                         "info", 1500);
        LOG_INFO("GenerationController: spawn deferred — queued (size={})", n);
        return;
    }
    spawn_unlocked(std::move(prompt), std::move(request));
}

void GenerationController::maybe_start_queued(GenerationRequest request) {
    if (store_.is_generating() || is_busy() || store_.status() == "error" ||
        !store_.has_queued_prompt()) {
        return;
    }
    if (queue_paused_) {
        return;
    }
    if (std::chrono::steady_clock::now() < queue_resume_at_) {
        return;
    }
    auto next = store_.dequeue_prompt();
    const auto remaining = static_cast<int>(store_.queue_size());
    store_.add_toast(
        "Running queued prompt (" + std::to_string(remaining) + " left)",
        "info", 1500);
    // request.append_user_message remains true (default) so spawn_unlocked appends
    // the user message to history and session DB when execution actually begins.
    spawn_unlocked(std::move(next), std::move(request));
}

void GenerationController::spawn_unlocked(std::string prompt,
                                          GenerationRequest request) {
    if (worker_.joinable()) {
        // Safe: busy_ is false, so the previous worker has finished.
        worker_.join();
    }
    // A fresh abort token per turn: Esc stops exactly this turn, and nothing
    // that happens later (a new send, a queued start) can clear it.
    auto abort_flag = std::make_shared<std::atomic<bool>>(false);
    store_.state().abort_flag = abort_flag;
    queue_paused_ = false;

    const auto& providers = request.providers;
    const int sel_prov = request.provider_idx;
    const int sel_mod = request.model_idx;
    if (sel_prov < 0 || sel_prov >= static_cast<int>(providers.size()) ||
        sel_mod < 0 ||
        sel_mod >= static_cast<int>(providers[sel_prov].models.size()) ||
        providers[sel_prov].models[sel_mod].id.empty()) {
        LOG_ERROR(
            "GenerationController: invalid selection provider_idx={} "
            "model_idx={} providers={}",
            sel_prov, sel_mod, providers.size());
        store_.add_toast("No model selected — use /model", "error", 4000);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(active_session_mutex_);
        active_session_id_ = store_.session_id();
    }
    abort_requested_ns_.store(0, std::memory_order_relaxed);
    busy_->store(true, std::memory_order_release);
    store_.set_generating(true);
    store_.clear_error();
    store_.clear_retry();
    *store_.state().auto_scroll = true;
    if (store_.state().last_user_prompt) {
        // Keep for retry even if this turn later fails.
        *store_.state().last_user_prompt = prompt;
    }
    if (request.append_user_message) {
        request.append_user_message =
            should_append_user_message(*store_.state().messages_history, prompt);
    }
    if (request.append_user_message) {
        store_.append_chat_message("User", prompt);
        session::save_message(store_.session_id(), "User", prompt);
        // A small model names a session that still has its default title.
        session_title::maybe_generate_async(
            std::make_shared<const std::vector<ProviderInfo>>(providers),
            store_.session_id(), prompt,
            [bus = bus_](const std::string& sid, const std::string& title) {
                bus->publish<contract::SessionTitleChanged>(
                    {.session_id = sid, .title = title});
                bus->wake();
            });
    }
    // A TUI restart or Esc mid-tool persists ToolCallStarted without a
    // matching ToolResult. Providers 400 ("No tool output found for
    // function call") if we replay that pair. Close them on the UI thread
    // so the worker snapshot, the scrollback, and the session DB agree.
    if (auto& hist_ptr = store_.state().messages_history;
        hist_ptr && has_unpaired_tool_calls(*hist_ptr)) {
        auto repaired =
            ProviderTransform::close_unpaired_tool_calls(*hist_ptr);
        LOG_WARN(
            "GenerationController: closed {} unpaired tool call(s) "
            "left by an interrupted turn",
            repaired.size() - hist_ptr->size());
        hist_ptr =
            std::make_shared<qcode::Messages>(std::move(repaired));
        session::overwrite_session_history(store_.session_id(),
                                           *hist_ptr);
    }
    LOG_INFO(
        "GenerationController: spawn prompt_len={} queue_remaining={} "
        "provider={} model={} model_id={} append_user={}",
        prompt.size(), store_.queue_size(),
        providers[sel_prov].name,
        providers[sel_prov].models[sel_mod].name,
        providers[sel_prov].models[sel_mod].id,
        request.append_user_message);

    auto bus_ptr = bus_;
    auto state_ptr = &store_.state();
    auto busy_ptr = busy_;
    auto app_running = app_running_;
    auto store_ptr = &store_;
    auto providers_copy = std::move(request.providers);
    auto sys_prompt = std::move(request.system_prompt);
    const bool tools_enabled = request.tools_enabled;
    const std::string spawn_session = store_.session_id();

    worker_ = qcode::compat::jthread(
        [this, bus_ptr, state_ptr, providers_copy = std::move(providers_copy),
         app_running, busy_ptr, store_ptr, sel_prov, sel_mod,
         sys_prompt = std::move(sys_prompt), tools_enabled, spawn_session,
         abort_flag](qcode::compat::stop_token stop_token) {
            qcode::logger::ScopedThreadSession bind(spawn_session);
            // Clear busy + wake UI when the worker exits (including after Esc
            // force-stop left is_generating already false).
            const auto busy_guard = std::shared_ptr<void>(
                nullptr, [this, busy_ptr, store_ptr, spawn_session, bus_ptr](void*) {
                    if (const auto at = abort_requested_ns_.exchange(0); at != 0) {
                        const auto now =
                            std::chrono::steady_clock::now().time_since_epoch().count();
                        PERF_LOG("esc_to_worker_exit_ms={:.1f}",
                                 std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::duration(now - at))
                                     .count());
                    }
                    {
                        std::lock_guard<std::mutex> lock(active_session_mutex_);
                        if (active_session_id_ == spawn_session) {
                            active_session_id_.clear();
                        }
                    }
                    busy_ptr->store(false, std::memory_order_release);
                    if (store_ptr->session_id() == spawn_session &&
                        store_ptr->status() != "error") {
                        store_ptr->set_status("idle");
                    }
                    if (bus_ptr) {
                        bus_ptr->wake();
                    }
                });

            qcode::logger::set_thread_name("llm");
            GenerationService backend{*bus_ptr, providers_copy};
            if (!app_running->load() || stop_token.stop_requested()) {
                return;
            }

            try {
                const auto gen_start = std::chrono::steady_clock::now();
                GenerationContext ctx{
                    .session_id = spawn_session,
                    .reasoning_mode = *state_ptr->reasoning_mode,
                    .agent_mode = state_ptr->agent_mode ? *state_ptr->agent_mode
                                                        : "build",
                    .workspace = session::get_session_workspace(spawn_session),
                    .abort_flag = abort_flag,
                    .has_queued_work = [store_ptr]() {
                        return store_ptr->has_queued_prompt();
                    },
                    .take_queued_prompts = [store_ptr, spawn_session]() {
                        return store_ptr->take_queued_prompts(spawn_session);
                    }};

                // Snapshot history via shared_ptr so compaction cannot race the
                // vector while we copy.
                const auto history = state_ptr->messages_history;
                // Same transform /compact replays (turn_prefix.h), so the
                // summarizer stays a prefix of this request.
                qcode::Messages gen_messages = qcode::prepare_turn_history(
                    history ? *history : qcode::Messages{},
                    /*drop_system_notes=*/true);

                const size_t ctx_window =
                    providers_copy[sel_prov].models[sel_mod].context_window;
                if (ctx_window > 0) {
                    const size_t sys_tok = estimate_system_tokens(sys_prompt);
                    const size_t msg_tok = estimate_tokens(gen_messages);
                    const size_t heuristic = sys_tok + msg_tok;
                    const size_t total = calibrate_estimate(
                        heuristic, *state_ptr->last_actual_prompt_tokens,
                        *state_ptr->last_estimated_tokens);
                    *state_ptr->last_estimated_tokens = static_cast<int>(heuristic);
                    // Publish the current per-turn context size so the TUI can show
                    // "<current context> / <window>" instead of the session lifetime
                    // total. This grows as tool calls append messages each step.
                    *state_ptr->current_context_tokens = static_cast<int>(total);
                    if (bus_ptr) {
                        bus_ptr->publish<contract::ContextSizeUpdated>({.context_tokens = static_cast<int>(total)});
                    }

                    const size_t warn_at = (ctx_window * 7) / 10;
                    const size_t prune_at = (ctx_window * 85) / 100;
                    const int pct = static_cast<int>(total * 100 / ctx_window);
                    LOG_INFO(
                        "Context window: {}/{} tokens ({}%)  [sys={} msg={} "
                        "window={} model={}]",
                        total, ctx_window, pct, sys_tok, msg_tok, ctx_window,
                        providers_copy[sel_prov].models[sel_mod].name);
                    ctx.context_tokens_hint = total;
                    // Auto-compaction (tool loop) keeps the context under its
                    // threshold; pruning would only break the prompt cache.
                    const size_t auto_at = qcode::compaction::auto_compact_threshold(
                        &providers_copy[sel_prov].models[sel_mod]);
                    if (auto_at > 0 && tools_enabled) {
                        LOG_INFO("Context {}/{}: auto-compaction at {} tokens",
                                 total, ctx_window, auto_at);
                        *state_ptr->consecutive_prunes = 0;
                    } else if (total > prune_at) {
                        LOG_WARN(
                            "Context over window: {}/{} tokens ({}%) >= prune "
                            "threshold {} — pruning",
                            total, ctx_window, pct, prune_at);
                        gen_messages = prune_context(std::move(gen_messages), ctx_window);
                        const size_t after =
                            sys_tok + estimate_tokens(gen_messages);
                        if (after < total) {
                            LOG_INFO(
                                "Context pruned: {} -> {} tokens ({}% of window)",
                                total, after, static_cast<int>(after * 100 / ctx_window));
                            store_ptr->add_toast(
                                "Context pruned: " + std::to_string(total) +
                                    " → " + std::to_string(after) + " tokens",
                                "warning", 4000);
                            *state_ptr->consecutive_prunes =
                                *state_ptr->consecutive_prunes + 1;
                        }
                    } else if (total > warn_at) {
                        LOG_WARN(
                            "Context near window: {}/{} tokens ({}%) > warn "
                            "threshold {} — suggest /compact",
                            total, ctx_window, pct, warn_at);
                        store_ptr->add_toast(
                            "Context at " + std::to_string(total) + "/" +
                                std::to_string(ctx_window) +
                                " tokens — run /compact soon",
                            "info", 3000);
                        *state_ptr->consecutive_prunes = 0;
                    } else {
                        *state_ptr->consecutive_prunes = 0;
                    }

                    if (*state_ptr->consecutive_prunes >= 3) {
                        *state_ptr->consecutive_prunes = 0;
                        LOG_WARN(
                            "Context required pruning for three consecutive turns");
                        store_ptr->add_toast(
                            "Context remains large — run /compact when this "
                            "turn finishes",
                            "warning", 5000);
                    }
                }

                backend.run_generation(providers_copy[sel_prov].id,
                                       providers_copy[sel_prov].models[sel_mod].id,
                                       sys_prompt, std::move(gen_messages), tools_enabled,
                                       ctx);

                const auto duration_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - gen_start)
                        .count();
                LOG_INFO(
                    "GenerationController: complete duration_ms={} "
                    "queue_remaining={}",
                    duration_ms, store_ptr->queue_size());
            } catch (const std::exception& e) {
                LOG_ERROR("GenerationController: worker exception: {}", e.what());
                if (store_ptr->session_id() == spawn_session) {
                    store_ptr->set_status("error");
                    store_ptr->add_toast(std::string("Generation error: ") + e.what(), "error", 5000);
                }
            }
            busy_ptr->store(false, std::memory_order_release);
            if (store_ptr->is_generating() &&
                store_ptr->session_id() == spawn_session) {
                store_ptr->set_generating(false);
                if (store_ptr->status() != "error") {
                    store_ptr->set_status("idle");
                }
            }
            if (bus_ptr) {
                bus_ptr->wake();
            }
        });
}

void GenerationController::shutdown() {
    request_abort();
    // App exit: drop only the in-memory mirror so queued prompts survive in
    // the session store and resume on next launch.
    store_.clear_prompt_queue_memory_only();
    if (app_running_) {
        app_running_->store(false, std::memory_order_release);
    }
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    busy_->store(false, std::memory_order_release);
}

}  // namespace qcode
