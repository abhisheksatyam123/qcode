#pragma once

#include "bus_envelope.h"
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <concepts>
#include <any>

namespace qcode {
namespace bus {

// ── Concept: an event definition ───────────────────────────────────────────────
template <typename T>
concept EventDefinition = requires {
    { T::type } -> std::convertible_to<const char*>;
    typename T::Payload;
};

// ── BusPort: abstract pub/sub backbone ─────────────────────────────────────────
class BusPort {
public:
    virtual ~BusPort() = default;

    // Register an event type
    template <EventDefinition E>
    void register_event() {
        register_type(E::type, std::type_index(typeid(typename E::Payload)));
    }

    // Publish a typed event. Thread-safe.
    template <EventDefinition E>
    void publish(const typename E::Payload& payload) {
        publish_impl(E::type, std::any(payload), std::type_index(typeid(typename E::Payload)));
    }

    // Subscribe to a typed event. Returns RAII Subscription.
    template <EventDefinition E>
    Subscription subscribe(std::function<void(const typename E::Payload&)> callback) {
        return subscribe_impl(E::type, std::type_index(typeid(typename E::Payload)),
            [cb = std::move(callback)](const EventEnvelope& env) {
                cb(std::any_cast<const typename E::Payload&>(env.payload));
            });
    }

    // ── Raw interface (used by implementations) ──
    virtual void publish_impl(const std::string& type, std::any payload, std::type_index payload_tid) = 0;
    virtual Subscription subscribe_impl(const std::string& type, std::type_index payload_tid, EventCallback callback) = 0;
    virtual void register_type(const std::string& type, std::type_index payload_tid) = 0;

    // Drain pending events (call from UI thread).
    virtual size_t drain() = 0;

    // Number of pending events.
    virtual size_t pending() const = 0;
};

} // namespace bus
} // namespace qcode
