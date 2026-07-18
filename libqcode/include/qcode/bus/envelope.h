#pragma once

#include <any>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <concepts>
#include <utility>
#include <vector>

namespace qcode {
namespace bus {

// ── Subscription token (RAII) ──────────────────────────────────────────────────
class Subscription {
public:
    Subscription() = default;
    Subscription(std::function<void()> unsub) : unsub_(std::move(unsub)) {}
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept : unsub_(std::move(other.unsub_)) {
        other.unsub_ = nullptr;
    }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            if (unsub_) unsub_();
            unsub_ = std::move(other.unsub_);
            other.unsub_ = nullptr;
        }
        return *this;
    }
    ~Subscription() { if (unsub_) unsub_(); }
    void unsubscribe() { if (unsub_) { unsub_(); unsub_ = nullptr; } }
    explicit operator bool() const { return unsub_ != nullptr; }
private:
    std::function<void()> unsub_;
};

// ── Runtime event envelope (type-erased for the queue) ─────────────────────────
struct EventEnvelope {
    std::string type;
    std::any    payload;
    std::type_index payload_type = std::type_index(typeid(void));
};

// ── Generic callback for type-erased events ────────────────────────────────────
using EventCallback = std::function<void(const EventEnvelope&)>;

} // namespace bus
} // namespace qcode
