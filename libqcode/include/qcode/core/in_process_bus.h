#pragma once

#include "bus_port.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace qcode {
namespace bus {

// ── BusRuntime: in-process event bus ─────────────────────────────────────────
// Uses a mutex-guarded deque for cross-thread event delivery.
// Simple, correct, and fast enough for UI event rates.
class BusRuntime : public BusPort {
public:
    BusRuntime() = default;
    ~BusRuntime() override;

    // BusPort interface
    void publish_impl(const std::string& type, std::any payload, std::type_index payload_tid) override;
    Subscription subscribe_impl(const std::string& type, std::type_index payload_tid, EventCallback callback) override;
    void register_type(const std::string& type, std::type_index payload_tid) override;
    size_t drain() override;
    size_t pending() const override;

    using Callback = std::function<void()>;

    // Called by a producer after it changes the queue from empty to non-empty.
    // The callback runs outside the queue lock and may safely wake a UI loop.
    void set_wake_callback(Callback callback);

    // Explicitly trigger the wake callback (e.g. to notify UI loop on background worker idle)
    void wake();

    // Drain observers are useful for batching work derived from many events.
    Subscription on_drain_complete(Callback callback);
    bool is_draining() const noexcept {
        return is_draining_.load(std::memory_order_acquire);
    }

private:
    struct SubscriberEntry {
        uint64_t id;
        EventCallback callback;
    };

    using SubscriberMap = std::unordered_map<std::string, std::vector<SubscriberEntry>>;

    // Queue for cross-thread event delivery
    mutable std::mutex queue_mutex_;
    std::deque<EventEnvelope> queue_;
    Callback wake_callback_;

    // Subscriber registry
    mutable std::mutex subscribers_mutex_;
    SubscriberMap subscribers_;
    std::atomic<uint64_t> next_sub_id_{1};

    // Type registry
    struct TypeInfo {
        std::type_index payload_tid{typeid(void)};
    };
    mutable std::mutex types_mutex_;
    std::unordered_map<std::string, TypeInfo> types_;

    mutable std::mutex drain_callbacks_mutex_;
    std::vector<std::pair<uint64_t, Callback>> drain_callbacks_;
    std::atomic<uint64_t> next_drain_callback_id_{1};
    std::atomic<bool> is_draining_{false};

    bool coalesce_with_back(EventEnvelope& event);
    void notify_drain_complete();
    void remove_drain_callback(uint64_t id);
};

} // namespace bus
} // namespace qcode
