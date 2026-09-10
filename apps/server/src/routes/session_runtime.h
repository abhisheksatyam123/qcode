#pragma once

#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>
#include <qcode/generation/generation_service.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace qcode {
namespace server {

struct GenSession {
    std::string id;
    std::shared_ptr<std::atomic<bool>> abort_flag{std::make_shared<std::atomic<bool>>(false)};
    qcode::Messages messages;
    std::vector<nlohmann::json> event_queue;
    std::mutex queue_mutex;
    std::atomic<bool> done{false};
    std::atomic<bool> generation_started{false};
    std::atomic<uint64_t> active_turn{0};
    std::string error;
    std::string assistant_text;
    std::string reasoning_text;
    std::shared_ptr<std::vector<qcode::bus::Subscription>> subs;
    std::atomic<int> live_prompt_tokens{0};
    std::atomic<int> live_completion_tokens{0};
    std::atomic<int> live_total_tokens{0};
    std::atomic<int> tool_call_count{0};
    std::atomic<int> total_tool_time_ms{0};
};

extern std::mutex g_sessions_mutex;
extern std::unordered_map<std::string, std::shared_ptr<GenSession>> g_sessions;

std::vector<qcode::bus::Subscription> subscribe_session(
    qcode::bus::BusRuntime& bus,
    std::shared_ptr<GenSession> session);

}  // namespace server
}  // namespace qcode
