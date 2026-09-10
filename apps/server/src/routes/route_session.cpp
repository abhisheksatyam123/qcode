#include "routes_internal.h"
#include "session_runtime.h"
#include "http_utils.h"

#include <qcode/core/in_process_bus.h>
#include <qcode/core/logger.h>
#include <qcode/config/provider_info.h>
#include <qcode/generation/generation_service.h>
#include <qcode/session/session_store.h>
#include <qcode/session/system_prompt.h>
#include <qcode/tools/task_tool.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace qcode::contract;

namespace qcode {
namespace server {

namespace {
nlohmann::json session_public_json(const qcode::session::SessionInfo& s) {
    auto modes = qcode::session::get_session_modes(s.id);
    std::string agent = modes.first;
    if (agent.empty()) {
        agent = (s.id.rfind("ses_", 0) == 0) ? "subagent" : "orchestrator";
    }
    return {
        {"id", s.id},
        {"title", s.title},
        {"workspace", s.workspace},
        {"provider", s.provider},
        {"model", s.model},
        {"agent_mode", agent},
        {"reasoning_mode", modes.second},
    };
}

bool query_flag_true(const httplib::Request& req, const char* name) {
    if (!req.has_param(name)) return false;
    const std::string v = req.get_param_value(name);
    return v == "1" || v == "true" || v == "yes";
}
}  // namespace


void register_session_routes(
    httplib::Server& svr,
    std::shared_ptr<qcode::bus::BusRuntime> bus,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers_list,
    const ServerSetupOptions& options) {
    std::string default_workspace = options.default_workspace;

auto handle_generate = [bus, providers_list, default_workspace](const std::string& session_id, const std::string& req_body, httplib::Response& res) {
    // Parse request body
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req_body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }

    std::string text = body.value("text", "");
    if (text.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"'text' field is required"})", "application/json");
        return;
    }

    // body.value() only uses the default when the key is absent. An explicit
    // empty string from the WebUI must still fall back to a real provider.
    std::string provider = body.value("provider", "");
    std::string model = body.value("model", "");
    if (provider.empty() || model.empty()) {
        auto [stored_p, stored_m] = qcode::session::get_session_provider_model(session_id);
        if (provider.empty() && !stored_p.empty()) provider = stored_p;
        if (model.empty() && !stored_m.empty()) model = stored_m;
    }
    {
        int sp = -1;
        for (int i = 0; i < static_cast<int>(providers_list->size()); ++i) {
            if ((*providers_list)[i].id == provider || (*providers_list)[i].name == provider) {
                sp = i;
                break;
            }
        }
        if (sp < 0) sp = 0;
        provider = (*providers_list)[sp].id;

        int sm = -1;
        for (int i = 0; i < static_cast<int>((*providers_list)[sp].models.size()); ++i) {
            if ((*providers_list)[sp].models[i].id == model ||
                (*providers_list)[sp].models[i].name == model) {
                sm = i;
                break;
            }
        }
        if (sm < 0) sm = 0;
        if (!(*providers_list)[sp].models.empty()) {
            model = (*providers_list)[sp].models[sm].id;
        }
    }
    bool study_mode =
#ifdef __ANDROID__
        true;
#else
        false;
#endif
    if (body.contains("study_mode") && body["study_mode"].is_boolean()) {
        study_mode = body["study_mode"].get<bool>();
    }
    const std::string mode = body.value("mode", "");
    if (mode == "study") study_mode = true;
    if (mode == "code") study_mode = false;

    std::string system_prompt;
    if (body.contains("system_prompt") && body["system_prompt"].is_string() &&
        !body["system_prompt"].get<std::string>().empty()) {
        system_prompt = body["system_prompt"].get<std::string>();
    } else if (study_mode) {
        system_prompt =
            qcode::SystemPrompt::build(qcode::SystemPrompt::study_identity());
    } else {
        system_prompt = qcode::SystemPrompt::build_default();
    }
    std::string reasoning_mode = body.value("reasoning_mode", "off");

    std::shared_ptr<GenSession> session;
    {
        std::lock_guard<std::mutex> lock(g_sessions_mutex);
        auto it = g_sessions.find(session_id);
        if (it != g_sessions.end()) session = it->second;
        if (!session) {
            session = std::make_shared<GenSession>();
            session->id = session_id;
            g_sessions[session->id] = session;
        }
    }

    // Cancel any active generation on that session first
    if (session->generation_started.load() && !session->done.load()) {
        if (session->abort_flag) {
            session->abort_flag->store(true);
        }
        int wait_count = 0;
        while (!session->done.load() && wait_count < 20) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            wait_count++;
        }
    }

    // Set up turn ID and per-turn state
    const uint64_t turn_id = ++session->active_turn;
    session->abort_flag = std::make_shared<std::atomic<bool>>(false);
    session->generation_started = false;
    session->done = false;
    session->error.clear();
    session->assistant_text.clear();
    session->reasoning_text.clear();
    session->tool_call_count = 0;
    session->total_tool_time_ms = 0;
    {
        std::lock_guard<std::mutex> lock(session->queue_mutex);
        session->event_queue.clear();
    }

    // Load (or reload) the entire session history from the SQLite database
    // to ensure we have the complete, up-to-date context (including ToolCall,
    // ToolResult, System, and Assistant messages).
    session->messages = qcode::session::load_session_history_parsed(session->id);

    // Resolve workspace
    std::string ws = qcode::session::get_session_workspace(session->id);
    if (ws.empty()) {
        ws = default_workspace;
    }

    // Subscribe to bus events for this session (once)
    if (!session->subs) {
        session->subs = std::make_shared<std::vector<qcode::bus::Subscription>>(
            subscribe_session(*bus, session));
    }

    // Save user message and add to history
    qcode::session::set_session_provider_model(session->id, provider, model);
    qcode::session::save_message(session->id, "User", text);
    session->messages.push_back(qcode::Message::user(text));

    // Keep a local copy of messages for the generation thread, applying the compaction cutoff
    qcode::Messages messages = qcode::apply_compaction_cutoff(session->messages);

    // ── Set up streaming response ──
    // Generation runs in a background thread. The chunked provider
    // callback polls the event queue and writes events as they arrive,
    // giving true real-time streaming to the client.
    res.set_chunked_content_provider("application/x-ndjson; charset=utf-8",
        [bus, session, provider, model, system_prompt, messages = std::move(messages),
         turn_id, ws, reasoning_mode](size_t /*offset*/, httplib::DataSink& sink) mutable -> bool
        {
            // Start generation in a background thread (once per turn)
            if (!session->generation_started.exchange(true)) {
                // Send session.started event first
                nlohmann::json start_msg = {
                    {"type", "session.started"},
                    {"session_id", session->id}
                };
                std::string chunk = start_msg.dump() + "\n";
                if (!sink.write(chunk.data(), chunk.size())) {
                    if (session->abort_flag) session->abort_flag->store(true);
                    return false;
                }

                auto abort_flag = session->abort_flag;
                std::thread gen_thread([session, provider, model, system_prompt,
                                        messages = std::move(messages), turn_id, abort_flag, ws, reasoning_mode]() {
                    qcode::GenerationContext ctx{
                        .session_id = session->id,
                        .reasoning_mode = reasoning_mode,
                        .workspace = ws,
                        .abort_flag = abort_flag
                    };
                    try {
                        g_backend->run_generation(
                            provider, model, system_prompt,
                            messages, true, ctx);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Generation error: {}", e.what());
                        if (session->active_turn.load() == turn_id) {
                            session->error = e.what();
                        }
                    }
                    if (session->active_turn.load() == turn_id) {
                        session->done = true;
                    }
                });
                gen_thread.detach();
            }

            // Dispatch bus events to subscribers (bus requires explicit drain)
            bus->drain();

            // Drain queued events
            std::vector<nlohmann::json> events;
            {
                std::lock_guard<std::mutex> lock(session->queue_mutex);
                events.swap(session->event_queue);
            }

            for (const auto& evt : events) {
                if (evt.value("type", "") == "backend.message.delta") {
                    session->assistant_text += evt.value("text", "");
                }
                // NOTE: reasoning_text is accumulated in the ReasoningDelta
                // subscriber (session_runtime.cpp), which flushes a Reasoning
                // row before each ToolCall row to preserve think/tool order.
                std::string chunk = evt.dump() + "\n";
                if (!sink.write(chunk.data(), chunk.size())) {
                    if (session->abort_flag) session->abort_flag->store(true);
                    return false;
                }
            }

            // If generation is done for this turn, drain remaining events and complete stream
            if (session->done.load() && session->active_turn.load() == turn_id) {
                bus->drain();
                {
                    std::lock_guard<std::mutex> lock(session->queue_mutex);
                    events.swap(session->event_queue);
                }
                for (const auto& evt : events) {
                    if (evt.value("type", "") == "backend.message.delta") {
                        session->assistant_text += evt.value("text", "");
                    }
                    std::string chunk = evt.dump() + "\n";
                    if (!sink.write(chunk.data(), chunk.size())) {
                        if (session->abort_flag) session->abort_flag->store(true);
                        return false;
                    }
                }

                // Persist the assistant reply so the session is resumable.
                if (!session->assistant_text.empty()) {
                    qcode::session::save_message(session->id, "Assistant", session->assistant_text);
                }
                if (!session->reasoning_text.empty()) {
                    qcode::session::save_message(session->id, "Reasoning", session->reasoning_text);
                }
                nlohmann::json final_msg = {
                    {"type", "generation.complete"},
                    {"session_id", session->id},
                    {"tool_call_count", session->tool_call_count.load()},
                    {"total_tool_time_ms", session->total_tool_time_ms.load()}
                };
                if (!session->error.empty() && session->assistant_text.empty()) {
                    final_msg["error"] = session->error;
                }
                std::string chunk = final_msg.dump() + "\n";
                if (!sink.write(chunk.data(), chunk.size())) {
                    if (session->abort_flag) session->abort_flag->store(true);
                    return false;
                }
                sink.done();
                return true;
            }

            // Avoid a hot loop while waiting for the provider or a tool.
            if (events.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return true;
        }
    );
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "*");
    res.set_header("Cache-Control", "no-cache, no-transform");
    res.set_header("Connection", "keep-alive");
    res.set_header("X-Accel-Buffering", "no");
};

// ── Generate response (Deprecated generic fallback) ──
svr.Post("/generate", [handle_generate](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }
    std::string session_id = body.value("session_id", "");
    if (session_id.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"'session_id' field is required"})", "application/json");
        return;
    }
    handle_generate(session_id, req.body, res);
});

// ── Generate response for specific session ──
svr.Post("/session/([^/]+)/generate", [handle_generate](const httplib::Request& req, httplib::Response& res) {
    std::string session_id = url_decode(req.matches[1]);
    handle_generate(session_id, req.body, res);
});

// ── List sessions ──
svr.Get("/sessions", [](const httplib::Request& req, httplib::Response& res) {
    auto list = qcode::session::list_sessions_full(query_flag_true(req, "include_subagents"));
    nlohmann::json j = nlohmann::json::array();
    for (const auto& s : list) {
        j.push_back(session_public_json(s));
    }
    res.set_content(j.dump(2), "application/json");
});

// ── Live delegated child sessions (TaskTool registry) ──
svr.Get("/tasks", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(qcode::TaskTool::list_tasks().dump(2), "application/json");
});

// ── Create session ──
svr.Post("/sessions", [default_workspace](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }
    std::string provider = body.value("provider", "");
    std::string model = body.value("model", "");
    std::string workspace = body.value("workspace", "");
    if (workspace.empty()) {
        workspace = default_workspace;
    }
    std::string custom_id = body.value("custom_id", "");
    if (custom_id.empty()) {
        custom_id = body.value("title", "");
    }
    if (provider.empty() || model.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"provider and model required"})", "application/json");
        return;
    }
    auto id = qcode::session::create_new_session(provider, model, workspace, custom_id);
    res.set_content(nlohmann::json({{"id", id}, {"workspace", workspace}, {"title", id}}).dump(), "application/json");
});

// ── Rename session ──
svr.Post("/rename", [](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }
    std::string session_id = body.value("session_id", "");
    std::string new_title = body.value("title", "");
    if (session_id.empty() || !qcode::session::is_valid_session_id(session_id)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    if (new_title.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"title is required"})", "application/json");
        return;
    }
    qcode::session::rename_session(session_id, new_title);
    res.set_content(R"({"ok":true})", "application/json");
});

// ── Cancel session generation ──
svr.Post("/session/cancel", [](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }
    std::string session_id = body.value("session_id", "");
    if (session_id.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"session_id is required"})", "application/json");
        return;
    }
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    auto it = g_sessions.find(session_id);
    if (it != g_sessions.end()) {
        if (it->second->abort_flag) {
            it->second->abort_flag->store(true);
        }
        res.set_content(R"({"status":"cancelled"})", "application/json");
    } else {
        res.status = 404;
        res.set_content(R"({"error":"session not found"})", "application/json");
    }
});

svr.Post("/session/([^/]+)/cancel", [](const httplib::Request& req, httplib::Response& res) {
    std::string session_id = url_decode(req.matches[1]);
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    auto it = g_sessions.find(session_id);
    if (it != g_sessions.end()) {
        if (it->second->abort_flag) {
            it->second->abort_flag->store(true);
        }
        res.set_content(R"({"status":"cancelled"})", "application/json");
    } else {
        res.status = 404;
        res.set_content(R"({"error":"session not found"})", "application/json");
    }
});

svr.Post("/session/([^/]+)/abort", [](const httplib::Request& req, httplib::Response& res) {
    std::string session_id = url_decode(req.matches[1]);
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    auto it = g_sessions.find(session_id);
    if (it != g_sessions.end()) {
        if (it->second->abort_flag) {
            it->second->abort_flag->store(true);
        }
        res.set_content(R"({"status":"cancelled"})", "application/json");
    } else {
        res.status = 404;
        res.set_content(R"({"error":"session not found"})", "application/json");
    }
});


svr.Get("/session/last", [](const httplib::Request&, httplib::Response& res) {
    std::string sid = qcode::session::get_last_active_session();
    if (sid.empty()) {
        res.set_content(R"({"id":""})", "application/json");
        return;
    }
    nlohmann::json info = {{"id", sid}};
    for (const auto& s : qcode::session::list_sessions_full(true)) {
        if (s.id == sid) {
            info = session_public_json(s);
            break;
        }
    }
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& [sender, content] : qcode::session::load_session_messages(sid)) {
        msgs.push_back({{"role", sender}, {"content", content}});
    }
    info["messages"] = std::move(msgs);
    res.set_content(info.dump(2), "application/json");
});

// ── Get session info (includes delegated child / subagent rows) ──
svr.Get("/session/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    auto list = qcode::session::list_sessions_full(true);
    for (const auto& s : list) {
        if (s.id == sid) {
            res.set_content(session_public_json(s).dump(), "application/json");
            return;
        }
    }
    res.status = 404;
    res.set_content(R"({"error":"session not found"})", "application/json");
});

// ── Set agent mode (plan <-> orchestrator), matching TUI toggle ──
svr.Post("/session/([^/]+)/mode", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (sid.empty() || !qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }
    std::string agent_mode = body.value("agent_mode", "");
    if (agent_mode != "plan" && agent_mode != "orchestrator") {
        res.status = 400;
        res.set_content(R"({"error":"agent_mode must be plan or orchestrator"})", "application/json");
        return;
    }
    auto modes = qcode::session::get_session_modes(sid);
    qcode::session::set_session_modes(sid, agent_mode, modes.second);
    res.set_content(nlohmann::json({{"ok", true}, {"agent_mode", agent_mode}}).dump(), "application/json");
});

// ── Delete session ──
svr.Delete("/session/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }

    // Cancel any active generation on that session first
    {
        std::lock_guard<std::mutex> lock(g_sessions_mutex);
        auto it = g_sessions.find(sid);
        if (it != g_sessions.end()) {
            auto session = it->second;
            if (session->abort_flag) {
                session->abort_flag->store(true);
            }
            g_sessions.erase(it);
        }
    }

    // Delete from database
    qcode::session::delete_session(sid);

    res.set_content(R"({"ok":true})", "application/json");
});

// ── Clear session messages (truncate history) ──
svr.Post("/session/([^/]+)/clear", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }

    // Clear in-memory session messages if a live session exists, so an
    // in-flight generation won't re-persist the old history.
    {
        std::lock_guard<std::mutex> lock(g_sessions_mutex);
        auto it = g_sessions.find(sid);
        if (it != g_sessions.end()) {
            it->second->messages.clear();
        }
    }

    // Permanently truncate the persisted history in SQLite.
    qcode::session::overwrite_session_history(sid, {});

    res.set_content(R"({"ok":true})", "application/json");
});

// ── Get session message history ──
svr.Get("/session/([^/]+)/messages", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    auto hist = qcode::session::load_session_messages(sid);
    nlohmann::json j = nlohmann::json::array();
    for (const auto& [sender, content] : hist) {
        j.push_back({{"role", sender}, {"content", content}});
    }
    res.set_content(j.dump(2), "application/json");
});

    register_session_ops_routes(svr, providers_list);
}

}  // namespace server
}  // namespace qcode
