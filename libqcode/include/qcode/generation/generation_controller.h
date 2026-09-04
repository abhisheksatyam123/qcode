#pragma once

#include <qcode/config/provider_info.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/core/jthread.h>
#include <qcode/ui/app_store.h>
#include <qcode/ui/chat_state.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace qcode {

// True when `prompt` still needs a visible User message appended to history.
// False when history already ends with this exact prompt (aborted turn /
// retry) — prevents duplicate user prompts. Defined in
// generation_controller.cpp; unit-tested for retry/abort parity.
bool should_append_user_message(const qcode::Messages& history,
                                const std::string& prompt);

// Snapshot of provider selection captured when a turn starts.
struct GenerationRequest {
    std::vector<ProviderInfo> providers;
    int provider_idx = 0;
    int model_idx = 0;
    std::string system_prompt;
    bool tools_enabled = true;
    // False for retry: user message is already in history/DB.
    bool append_user_message = true;
};

// Owns the generation worker thread and the busy/abort contract.
//
// Critical rule: the UI thread must never join() while the worker may still be
// blocked in HTTP. Esc force-stop clears UI flags; the worker exits later and
// clears busy_, which lets queued prompts start.
class GenerationController {
public:
    GenerationController(AppStore& store,
                         std::shared_ptr<bus::BusRuntime> bus,
                         std::shared_ptr<std::atomic<bool>> app_running);

    GenerationController(const GenerationController&) = delete;
    GenerationController& operator=(const GenerationController&) = delete;

    ~GenerationController();

    bool is_busy() const noexcept {
        return busy_->load(std::memory_order_acquire);
    }

    // True while a turn is visible as in-progress or a worker is still alive.
    bool is_active() const noexcept;

    // Cooperative stop (checked between tool steps / stream events).
    // Does not clear the prompt queue.
    void request_abort();

    // Unstick the UI immediately without joining the worker.
    // Preserves queued prompts (same as request_abort).
    void force_stop_ui();

    // Abort the in-flight turn and idle the UI for a session switch.
    // Does not join the worker (it may still be blocked in HTTP).
    void prepare_session_switch();

    // Start a turn, or queue the prompt if a worker is still alive.
    void spawn(std::string prompt, GenerationRequest request);

    // Drain one queued prompt when idle. Call from the UI render loop.
    void maybe_start_queued(GenerationRequest request);

    // Request stop and join (process exit only).
    void shutdown();

private:
    void spawn_unlocked(std::string prompt, GenerationRequest request);

    AppStore& store_;
    std::shared_ptr<bus::BusRuntime> bus_;
    std::shared_ptr<std::atomic<bool>> app_running_;
    std::shared_ptr<std::atomic<bool>> busy_;
    qcode::compat::jthread worker_;
};

}  // namespace qcode
