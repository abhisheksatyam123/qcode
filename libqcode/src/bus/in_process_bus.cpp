#include <qcode/bus/in_process_bus.h>
#include <qcode/contract/event.h>

#include <algorithm>
#include <utility>

namespace qcode {
namespace bus {

BusRuntime::~BusRuntime() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.clear();
        wake_callback_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(drain_callbacks_mutex_);
        drain_callbacks_.clear();
    }
}

void BusRuntime::publish_impl(const std::string& type, std::any payload, std::type_index payload_tid) {
    EventEnvelope env{
        .type = type,
        .payload = std::move(payload),
        .payload_type = payload_tid,
    };
    Callback wake;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        const auto was_empty = queue_.empty();
        if (!coalesce_with_back(env)) {
            queue_.emplace_back(std::move(env));
        }
        if (was_empty && !queue_.empty()) {
            wake = wake_callback_;
        }
    }
    if (wake) wake();
}

bool BusRuntime::coalesce_with_back(EventEnvelope& event) {
    if (queue_.empty() || queue_.back().type != event.type) return false;

    using contract::MessageDelta;
    using contract::ReasoningDelta;
    if (event.type == MessageDelta::type) {
        auto* previous =
            std::any_cast<MessageDelta::Payload>(&queue_.back().payload);
        auto* incoming = std::any_cast<MessageDelta::Payload>(&event.payload);
        if (previous == nullptr || incoming == nullptr || previous->done ||
            incoming->done || previous->session_id != incoming->session_id) {
            return false;
        }
        previous->text += incoming->text;
        return true;
    }

    if (event.type == ReasoningDelta::type) {
        auto* previous =
            std::any_cast<ReasoningDelta::Payload>(&queue_.back().payload);
        auto* incoming = std::any_cast<ReasoningDelta::Payload>(&event.payload);
        if (previous == nullptr || incoming == nullptr || previous->done ||
            incoming->done || previous->session_id != incoming->session_id) {
            return false;
        }
        previous->text += incoming->text;
        if (!incoming->signature.empty()) {
            previous->signature = std::move(incoming->signature);
        }
        return true;
    }

    return false;
}

void BusRuntime::set_wake_callback(Callback callback) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    wake_callback_ = std::move(callback);
}

Subscription BusRuntime::on_drain_complete(Callback callback) {
    const auto id =
        next_drain_callback_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(drain_callbacks_mutex_);
        drain_callbacks_.emplace_back(id, std::move(callback));
    }
    return Subscription([this, id]() { remove_drain_callback(id); });
}

void BusRuntime::remove_drain_callback(uint64_t id) {
    std::lock_guard<std::mutex> lock(drain_callbacks_mutex_);
    const auto it = std::find_if(
        drain_callbacks_.begin(), drain_callbacks_.end(),
        [id](const auto& entry) { return entry.first == id; });
    if (it != drain_callbacks_.end()) drain_callbacks_.erase(it);
}

void BusRuntime::notify_drain_complete() {
    std::vector<Callback> callbacks;
    {
        std::lock_guard<std::mutex> lock(drain_callbacks_mutex_);
        callbacks.reserve(drain_callbacks_.size());
        for (const auto& [id, callback] : drain_callbacks_) {
            static_cast<void>(id);
            callbacks.emplace_back(callback);
        }
    }
    for (const auto& callback : callbacks) callback();
}

Subscription BusRuntime::subscribe_impl(
    const std::string& type,
    std::type_index payload_tid,
    EventCallback callback)
{
    static_cast<void>(payload_tid);
    uint64_t id = next_sub_id_.fetch_add(1, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_[type].push_back({id, std::move(callback)});
    }

    return Subscription([this, type, id]() {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        auto& vec = subscribers_[type];
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it->id == id) {
                vec.erase(it);
                break;
            }
        }
    });
}

void BusRuntime::register_type(const std::string& type, std::type_index payload_tid) {
    std::lock_guard<std::mutex> lock(types_mutex_);
    types_[type] = {payload_tid};
}

size_t BusRuntime::drain() {
    size_t count = 0;
    constexpr size_t kMaxDrain = 1024;
    is_draining_.store(true, std::memory_order_release);

    try {
        while (count < kMaxDrain) {
            EventEnvelope env;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (queue_.empty()) break;
                env = std::move(queue_.front());
                queue_.pop_front();
            }

            // Collect subscribers under lock, invoke outside lock
            std::vector<SubscriberEntry> targets;
            {
                std::lock_guard<std::mutex> lock(subscribers_mutex_);
                auto it = subscribers_.find(env.type);
                if (it != subscribers_.end()) {
                    targets = it->second;
                }
                // Wildcard subscribers
                auto wit = subscribers_.find("*");
                if (wit != subscribers_.end()) {
                    for (const auto& sub : wit->second) {
                        targets.push_back(sub);
                    }
                }
            }

            for (const auto& sub : targets) {
                sub.callback(env);
            }
            ++count;
        }
    } catch (...) {
        is_draining_.store(false, std::memory_order_release);
        if (count > 0) notify_drain_complete();
        throw;
    }

    is_draining_.store(false, std::memory_order_release);
    if (count > 0) notify_drain_complete();
    Callback wake;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!queue_.empty()) wake = wake_callback_;
    }
    if (wake) wake();
    return count;
}

size_t BusRuntime::pending() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
}

} // namespace bus
} // namespace qcode
