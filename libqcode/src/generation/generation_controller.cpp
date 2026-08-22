#include <qcode/session/generation_controller.h>

#include <qcode/core/logger.h>
#include <qcode/session/generation_service.h>
#include <qcode/session/token_budget.h>
#include <qcode/core/event.h>
#include <qcode/session/session_store.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace qcode {

GenerationController::GenerationController(
    AppStore& store,
    std::shared_ptr<bus::BusRuntime> bus,
    std::shared_ptr<std::atomic<bool>> app_running)
    : store_(store),
      bus_(std::move(bus)),
      app_running_(std::move(app_running)),
      busy_(std::make_shared<std::atomic<bool>>(false)) {}

GenerationController::~GenerationController() { shutdown(); }

bool GenerationController::is_active() const noexcept {
    const auto& st =
        store_.state().status ? *store_.state().status : std::string{};
    return store_.is_generating() || is_busy() || st == "generating" ||
           st == "agent";
}

void GenerationController::request_abort() {
    // Stop the in-flight turn only. Queued prompts are preserved so a
    // force-stop → enqueue handoff (and multi-prompt queues) still run.
    auto& state = store_.state();
    if (state.abort_flag) {
        state.abort_flag->store(true, std::memory_order_release);
    }
    if (worker_.joinable()) {
        worker_.request_stop();
    }
}

void GenerationController::force_stop_ui() {
    // Unstick the UI only — keep any queued prompts so they can still run.
    request_abort();
    store_.set_generating(false);
    store_.set_status("idle");
    if (store_.state().last_user_prompt &&
        !store_.state().last_user_prompt->empty()) {
        store_.mark_retry_available(*store_.state().last_user_prompt);
        store_.add_toast("Generation force-stopped  · press r to retry",
                         "warning", 2500);
    } else {
        store_.add_toast("Generation force-stopped", "warning", 2500);
    }
}

void GenerationController::spawn(std::string prompt, GenerationRequest request) {
    if (is_busy()) {
        // Nudge the lingering worker to exit; keep any already-queued prompts.
        request_abort();
        store_.enqueue_prompt(std::move(prompt));
        store_.add_toast("Waiting for previous turn to stop…", "warning", 2000);
        LOG_INFO("GenerationController: spawn deferred — worker busy");
        return;
    }
    spawn_unlocked(std::move(prompt), std::move(request));
}

void GenerationController::maybe_start_queued(GenerationRequest request) {
    if (store_.is_generating() || is_busy() || !store_.has_queued_prompt()) {
        return;
    }
    auto next = store_.dequeue_prompt();
    const auto remaining = static_cast<int>(store_.queue_size());
    store_.add_toast(
        "Running queued prompt (" + std::to_string(remaining) + " left)",
        "info", 1500);
    // Queued prompts were already appended to the history/DB when they were
    // enqueued (mirrors upstream adding the user message immediately) — skip
    // the second append here.
    request.append_user_message = false;
    spawn_unlocked(std::move(next), std::move(request));
}

void GenerationController::spawn_unlocked(std::string prompt,
                                          GenerationRequest request) {
    if (worker_.joinable()) {
        // Safe: busy_ is false, so the previous worker has finished.
        worker_.join();
    }

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
        store_.append_chat_message("User", prompt);
        session::save_message(store_.session_id(), "User", prompt);
    }

    const auto& providers = request.providers;
    const int sel_prov = request.provider_idx;
    const int sel_mod = request.model_idx;
    LOG_INFO(
        "GenerationController: spawn prompt_len={} queue_remaining={} "
        "provider={} model={} append_user={}",
        prompt.size(), store_.queue_size(),
        providers[sel_prov].name,
        providers[sel_prov].models[sel_mod].name,
        request.append_user_message);

    auto bus_ptr = bus_;
    auto state_ptr = &store_.state();
    auto busy_ptr = busy_;
    auto app_running = app_running_;
    auto store_ptr = &store_;
    auto providers_copy = std::move(request.providers);
    auto sys_prompt = std::move(request.system_prompt);
    const bool tools_enabled = request.tools_enabled;

    worker_ = qcode::compat::jthread(
        [bus_ptr, state_ptr, providers_copy = std::move(providers_copy),
         app_running, busy_ptr, store_ptr, sel_prov, sel_mod,
         sys_prompt = std::move(sys_prompt),
         tools_enabled](qcode::compat::stop_token stop_token) {
            // Clear busy + wake UI when the worker exits (including after Esc
            // force-stop left is_generating already false).
            const auto busy_guard = std::shared_ptr<void>(
                nullptr, [busy_ptr, store_ptr](void*) {
                    busy_ptr->store(false, std::memory_order_release);
                    store_ptr->set_status("idle");
                });

            qcode::logger::set_thread_name("llm");
            GenerationService backend{*bus_ptr, providers_copy};
            if (!app_running->load() || stop_token.stop_requested()) {
                return;
            }

            const auto gen_start = std::chrono::steady_clock::now();
            GenerationContext ctx{
                .session_id = *state_ptr->session_id,
                .reasoning_mode = *state_ptr->reasoning_mode,
                .workspace = session::get_session_workspace(*state_ptr->session_id),
                .abort_flag = state_ptr->abort_flag};
            if (state_ptr->abort_flag) {
                state_ptr->abort_flag->store(false, std::memory_order_release);
            }

            // Snapshot history via shared_ptr so compaction cannot race the
            // vector while we copy.
            const auto history = state_ptr->messages_history;
            qcode::Messages gen_messages = history ? *history : qcode::Messages{};
            gen_messages = qcode::apply_compaction_cutoff(gen_messages);
            gen_messages.erase(
                std::remove_if(gen_messages.begin(), gen_messages.end(),
                               [](const qcode::Message& message) {
                                   return message.role == qcode::kMessageRoleSystem;
                               }),
                gen_messages.end());

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
                if (total > prune_at) {
                    LOG_WARN(
                        "Context over window: {}/{} tokens ({}%) >= prune "
                        "threshold {} — pruning",
                        total, ctx_window, pct, prune_at);
                    gen_messages = prune_context(gen_messages, ctx_window);
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
                                   sys_prompt, gen_messages, tools_enabled,
                                   ctx);

            const auto duration_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - gen_start)
                    .count();
            LOG_INFO(
                "GenerationController: complete duration_ms={} "
                "queue_remaining={}",
                duration_ms, store_ptr->queue_size());
            if (store_ptr->is_generating()) {
                store_ptr->set_generating(false);
                store_ptr->set_status("idle");
            }
        });
}

void GenerationController::shutdown() {
    request_abort();
    store_.clear_prompt_queue();
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
