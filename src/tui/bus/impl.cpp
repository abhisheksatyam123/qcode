#include <ai/tui/bus/impl.h>
#include <cassert>

namespace ai {
namespace tui {
namespace bus {

BusRuntime::~BusRuntime() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.clear();
}

void BusRuntime::publish_impl(const std::string& type, std::any payload, std::type_index payload_tid) {
    EventEnvelope env;
    env.type = type;
    env.payload = std::move(payload);
    env.payload_type = payload_tid;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(std::move(env));
    }
}

Subscription BusRuntime::subscribe_impl(
    const std::string& type,
    std::type_index payload_tid,
    EventCallback callback)
{
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
    return count;
}

size_t BusRuntime::pending() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
}

} // namespace bus
} // namespace tui
} // namespace ai
