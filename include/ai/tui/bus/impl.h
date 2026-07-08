#pragma once

#include "port.h"
#include <mutex>
#include <deque>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <cstdint>

namespace ai {
namespace tui {
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

private:
    struct SubscriberEntry {
        uint64_t id;
        EventCallback callback;
    };

    using SubscriberMap = std::unordered_map<std::string, std::vector<SubscriberEntry>>;

    // Queue for cross-thread event delivery
    mutable std::mutex queue_mutex_;
    std::deque<EventEnvelope> queue_;

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
};

} // namespace bus
} // namespace tui
} // namespace ai
