#include "session_runtime.h"
#include "bus_json_codec.h"

#include <qcode/session/session_store.h>

namespace qcode {
namespace server {

using namespace qcode::contract;

std::mutex g_sessions_mutex;
std::unordered_map<std::string, std::shared_ptr<GenSession>> g_sessions;

std::vector<qcode::bus::Subscription> subscribe_session(
    qcode::bus::BusRuntime& bus,
    std::shared_ptr<GenSession> session)
{
    std::vector<qcode::bus::Subscription> subs;

    subs.push_back(bus.subscribe<MessageDelta>(
        [session](const MessageDelta::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            auto j = qcode::server::message_delta_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ToolCallStarted>(
        [session](const ToolCallStarted::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            session->tool_call_count++;
            // Flush thinking accumulated since the previous tool call as its
            // own row so DB order preserves the think -> tool interleaving
            // the webui timeline renders.
            std::string reasoning_to_save;
            {
                std::lock_guard<std::mutex> lock(session->queue_mutex);
                if (!session->reasoning_text.empty()) {
                    reasoning_to_save = std::move(session->reasoning_text);
                    session->reasoning_text.clear();
                }
            }
            if (!reasoning_to_save.empty()) {
                qcode::session::save_message(p.session_id, "Reasoning", reasoning_to_save);
            }
            nlohmann::json call_json = {
                {"id", p.tool_call_id},
                {"name", p.tool_name},
                {"arguments", p.arguments},
            };
            qcode::session::save_message(p.session_id, "ToolCall", call_json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));

            auto j = qcode::server::tool_call_started_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ToolCallCompleted>(
        [session](const ToolCallCompleted::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            session->total_tool_time_ms += static_cast<int>(p.duration_ms);
            nlohmann::json result_json = {
                {"tool_call_id", p.tool_call_id},
                {"tool_name", p.tool_name},
                {"result", p.result},
                {"is_error", p.is_error},
                {"duration_ms", p.duration_ms},
            };
            qcode::session::save_message(p.session_id, "ToolResult", result_json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));

            auto j = qcode::server::tool_call_completed_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<SessionStatusChanged>(
        [session](const SessionStatusChanged::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            auto j = qcode::server::session_status_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ErrorOccurred>(
        [session](const ErrorOccurred::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            if (p.severity == "info") {
                // Heartbeat / progress notice — not an error, do not queue as error event
                return;
            }
            auto j = qcode::server::error_occurred_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            if (p.severity == "error" || p.severity == "fatal") {
                session->error = p.message;
            }
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ReasoningDelta>(
        [session](const ReasoningDelta::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            auto j = qcode::server::reasoning_delta_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            if (!p.text.empty()) session->reasoning_text += p.text;
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<TokenUsageUpdated>(
        [session](const TokenUsageUpdated::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            // Persist per-turn deltas into the session row (the DB is the
            // cumulative source of truth across restarts / session switches).
            // The in-memory live_* are kept only for any consumer that wants the
            // latest turn's value; the stats route passes 0 for live tokens so
            // get_session_stats does not double-count against the stored totals.
            qcode::session::persist_session_token_stats(
                session->id, p.prompt_tokens, p.completion_tokens, p.total_tokens);
            session->live_prompt_tokens = p.prompt_tokens;
            session->live_completion_tokens = p.completion_tokens;
            session->live_total_tokens = p.total_tokens;
            auto j = qcode::server::token_usage_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    return subs;
}

}  // namespace server
}  // namespace qcode
