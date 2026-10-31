#include "session_runtime.h"
#include "bus_json_codec.h"

#include <qcode/core/logger.h>
#include <qcode/session/session_store.h>

namespace qcode {
namespace server {

using namespace qcode::contract;

namespace {

// Bounds memory; a stream further behind than this skips ahead.
constexpr size_t kMaxStreamEvents = 4096;

// Queue an event for the session's streams. Caller holds queue_mutex.
void push_event(GenSession& session, nlohmann::json j) {
    if (!session.generating && session.streams == 0) return;  // no one can read it
    const uint64_t turn = session.active_turn.load();
    session.events.push_back({turn, encode_stream_line(std::move(j), turn)});
    if (session.events.size() > kMaxStreamEvents) {
        session.events.pop_front();
        ++session.events_base;
    }
    session.queue_cv.notify_all();
}

// Run `fn(session, payload)` for each event of type E while the session
// lives. Holding it weakly lets a deleted session (and its subscriptions)
// be freed instead of being kept alive by the bus.
template <typename E, typename Fn>
qcode::bus::Subscription subscribe_weak(qcode::bus::BusRuntime& bus,
                                        const std::shared_ptr<GenSession>& session,
                                        Fn fn) {
    return bus.subscribe<E>(
        [weak = std::weak_ptr<GenSession>(session), fn](const typename E::Payload& p) {
            if (auto s = weak.lock()) fn(s, p);
        });
}

}  // namespace

std::mutex g_sessions_mutex;
std::unordered_map<std::string, std::shared_ptr<GenSession>> g_sessions;
std::shared_ptr<qcode::SessionFileLogger> g_session_logger;

void prune_session_logs() {
    if (!g_session_logger) return;
    auto logger = g_session_logger;
    logger->prune([](const std::string& sid) {
        if (sid.empty()) return true;  // unscoped stem file is always kept
        {
            std::lock_guard<std::mutex> lock(g_sessions_mutex);
            if (g_sessions.find(sid) != g_sessions.end()) return true;
        }
        return qcode::session::session_exists(sid);
    });
}

void deliver_bus_events(qcode::bus::BusRuntime& bus) {
    // One thread at a time keeps subscribers in bus order. A subscriber that
    // publishes re-enters here; the loop below picks its event up.
    static std::mutex deliver_mutex;
    thread_local bool delivering = false;
    if (delivering) return;
    std::lock_guard<std::mutex> lock(deliver_mutex);
    delivering = true;
    for (;;) {
        try {
            if (bus.drain() == 0) break;
        } catch (const std::exception& e) {
            LOG_ERROR("Bus subscriber failed: {}", e.what());
        } catch (...) {
            LOG_ERROR("Bus subscriber failed");
        }
    }
    delivering = false;
}

void flush_turn_text(GenSession& session) {
    if (!session.reasoning_text.empty()) {
        // Keep the signature: the next turn replays the signed thinking block.
        qcode::session::save_message(
            session.id, "Reasoning",
            qcode::session::encode_reasoning_row(session.reasoning_text,
                                                 session.reasoning_signature));
    }
    if (!session.assistant_text.empty()) {
        qcode::session::save_message(session.id, "Assistant", session.assistant_text);
    }
    session.reasoning_text.clear();
    session.reasoning_signature.clear();
    session.assistant_text.clear();
}

void abort_turn(GenSession& session) {
    std::lock_guard<std::mutex> lock(session.queue_mutex);
    session.abort_flag->store(true);
    session.pending_prompts.clear();
}

std::shared_ptr<const std::string> encode_stream_line(nlohmann::json j, uint64_t turn) {
    j["turn"] = turn;
    return std::make_shared<const std::string>(
        j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n");
}

std::shared_ptr<void> attach_stream(const std::shared_ptr<GenSession>& session) {
    ++session->streams;
    return std::shared_ptr<void>(nullptr, [session](void*) {
        std::lock_guard<std::mutex> lock(session->queue_mutex);
        --session->streams;
        trim_events(*session);
    });
}

void trim_events(GenSession& session) {
    if (session.generating || session.streams > 0) return;
    session.events_base += session.events.size();
    session.events.clear();
}

std::vector<qcode::bus::Subscription> subscribe_session(
    qcode::bus::BusRuntime& bus,
    const std::shared_ptr<GenSession>& session)
{
    std::vector<qcode::bus::Subscription> subs;

    subs.push_back(subscribe_weak<MessageDelta>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const MessageDelta::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->assistant_text += p.text;
            push_event(*session, qcode::server::message_delta_to_json(p));
        }
    ));

    subs.push_back(subscribe_weak<ToolCallStarted>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const ToolCallStarted::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            session->tool_call_count++;
            nlohmann::json call_json = {
                {"id", p.tool_call_id},
                {"name", p.tool_name},
                {"arguments", p.arguments},
            };
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            // Flush thinking and text accumulated since the previous tool call
            // as their own rows so DB order preserves the think -> text ->
            // tool interleaving the webui timeline renders.
            flush_turn_text(*session);
            qcode::session::save_message(p.session_id, "ToolCall", call_json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
            push_event(*session, qcode::server::tool_call_started_to_json(p));
        }
    ));

    subs.push_back(subscribe_weak<ToolCallCompleted>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const ToolCallCompleted::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            session->total_tool_time_ms += static_cast<int>(p.duration_ms);
            nlohmann::json result_json = {
                {"tool_call_id", p.tool_call_id},
                {"tool_name", p.tool_name},
                {"result", p.result},
                {"is_error", p.is_error},
                {"duration_ms", p.duration_ms},
            };
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            qcode::session::save_message(p.session_id, "ToolResult", result_json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
            push_event(*session, qcode::server::tool_call_completed_to_json(p));
        }
    ));

    subs.push_back(subscribe_weak<UserMessageInjected>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const UserMessageInjected::Payload& p) {
            if (p.session_id != session->id) return;
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            // Save the reply so far first, so history shows the prompt where
            // the model actually read it.
            flush_turn_text(*session);
            qcode::session::save_message(session->id, "User", p.text);
            push_event(*session, qcode::server::user_message_injected_to_json(p));
        }
    ));

    subs.push_back(subscribe_weak<SessionTitleChanged>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const SessionTitleChanged::Payload& p) {
            if (p.session_id != session->id) return;
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            push_event(*session, qcode::server::session_title_changed_to_json(p));
        }
    ));

    subs.push_back(subscribe_weak<ConversationCompacted>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const ConversationCompacted::Payload& p) {
            if (p.session_id != session->id) return;
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            // Reply so far first; then the marker row the next turn cuts at.
            flush_turn_text(*session);
            qcode::session::save_message(session->id, "System", compacted_note(p));
            qcode::session::save_message(session->id, "User", p.text);
            push_event(*session, qcode::server::conversation_compacted_to_json(p));
        }
    ));

    subs.push_back(subscribe_weak<SessionStatusChanged>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const SessionStatusChanged::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            push_event(*session, qcode::server::session_status_to_json(p));
        }
    ));

    subs.push_back(subscribe_weak<ErrorOccurred>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const ErrorOccurred::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            if (p.severity == "info") {
                // Heartbeat / progress notice — not an error, do not queue as error event
                return;
            }
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            if (p.severity == "error" || p.severity == "fatal") {
                session->error = p.message;
            }
            push_event(*session, qcode::server::error_occurred_to_json(p));
        }
    ));

    subs.push_back(subscribe_weak<ReasoningDelta>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const ReasoningDelta::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->reasoning_text += p.text;
            if (!p.signature.empty()) session->reasoning_signature = p.signature;
            push_event(*session, qcode::server::reasoning_delta_to_json(p));
        }
    ));

    // Per-call latency, tokens and cost (persisted before it is published):
    // the WebUI Stats tab follows a turn call by call.
    subs.push_back(subscribe_weak<StepLatency>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const StepLatency::Payload& p) {
            if (!p.session_id.empty() && p.session_id != session->id) return;
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            push_event(*session, qcode::server::step_latency_to_json(p));
        }
    ));

    subs.push_back(subscribe_weak<TokenUsageUpdated>(bus, session,
        [](const std::shared_ptr<GenSession>& session, const TokenUsageUpdated::Payload& p) {
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
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            push_event(*session, qcode::server::token_usage_to_json(p));
        }
    ));

    return subs;
}

}  // namespace server
}  // namespace qcode
