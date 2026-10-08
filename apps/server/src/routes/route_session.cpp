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

#include <qcode/tools/image_tool.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
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
    std::string persona = s.persona;
    if (persona.empty()) {
        persona = qcode::session::get_session_persona(s.id);
    }
    return {
        {"id", s.id},
        {"title", s.title},
        {"workspace", s.workspace},
        {"provider", s.provider},
        {"model", s.model},
        {"agent_mode", agent},
        {"reasoning_mode", modes.second},
        {"parent_session_id", s.parent_session_id},
        {"persona", persona}
    };
}

bool query_flag_true(const httplib::Request& req, const char* name) {
    if (!req.has_param(name)) return false;
    const std::string v = req.get_param_value(name);
    return v == "1" || v == "true" || v == "yes";
}

std::shared_ptr<GenSession> find_session(const std::string& sid) {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    auto it = g_sessions.find(sid);
    return it == g_sessions.end() ? nullptr : it->second;
}

// Turn ids are unique across sessions and server restarts, so a client can
// tell late events of a turn it stopped from those of a later turn.
uint64_t next_turn_id() {
    static std::atomic<uint64_t> next{static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())};
    return ++next;
}

// Persisted history; with `limit`, only its first `limit` rows.
nlohmann::json session_messages_json(const std::string& sid,
                                     size_t limit = std::numeric_limits<size_t>::max()) {
    auto rows = qcode::session::load_session_messages(sid);
    if (rows.size() > limit) rows.resize(limit);
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& [sender, content] : rows) {
        msgs.push_back({{"role", sender}, {"content", content}});
    }
    return msgs;
}

// Stream turn `turn` of `session` as NDJSON, from event sequence `cursor` until
// the turn ends. `first` is written before any event. Every line carries
// `"turn": turn`. `stream` is the attach_stream() token taken with `cursor`;
// it keeps the events this stream still has to read. Streams only read: the
// turn runs and persists itself whether or not a client is attached.
void stream_turn(httplib::Response& res, std::shared_ptr<GenSession> session,
                 uint64_t turn, uint64_t cursor, std::shared_ptr<void> stream,
                 nlohmann::json first) {
    std::vector<std::shared_ptr<const std::string>> out{
        encode_stream_line(std::move(first), turn)};
    res.set_chunked_content_provider("application/x-ndjson; charset=utf-8",
        [session, turn, cursor, stream = std::move(stream), out = std::move(out),
         last_write = std::chrono::steady_clock::now()]
        (size_t /*offset*/, httplib::DataSink& sink) mutable -> bool
        {
            try {
                bool ended = false;
                {
                    std::unique_lock<std::mutex> lock(session->queue_mutex);
                    auto end = [&] { return session->events_base + session->events.size(); };
                    auto turn_over = [&] {
                        return !session->generating || session->active_turn != turn;
                    };
                    if (out.empty()) {
                        session->queue_cv.wait_for(lock, std::chrono::milliseconds(500), [&] {
                            return cursor < end() || turn_over();
                        });
                    }
                    cursor = std::max(cursor, session->events_base);
                    for (; cursor < end(); ++cursor) {
                        const auto& evt = session->events[cursor - session->events_base];
                        if (evt.turn == turn) out.push_back(evt.line);
                    }
                    // The turn queues all its events before it stops generating,
                    // so nothing can follow once this is seen under the lock.
                    ended = turn_over();
                    if (ended) {
                        nlohmann::json done = {
                            {"type", "generation.complete"},
                            {"session_id", session->id},
                            {"tool_call_count", session->tool_call_count.load()},
                            {"total_tool_time_ms", session->total_tool_time_ms.load()}
                        };
                        if (!session->error.empty()) done["error"] = session->error;
                        out.push_back(encode_stream_line(std::move(done), turn));
                    }
                }

                for (const auto& line : out) {
                    if (!sink.write(line->data(), line->size())) {
                        LOG_DEBUG("Client disconnected from session {} stream", session->id);
                        return false;
                    }
                    last_write = std::chrono::steady_clock::now();
                }
                out.clear();
                if (ended) {
                    sink.done();
                    return true;
                }

                // Idle: send a keepalive, which also notices a client that left.
                auto now = std::chrono::steady_clock::now();
                if (now - last_write >= std::chrono::milliseconds(2500)) {
                    nlohmann::json hb = {
                        {"type", "backend.heartbeat"},
                        {"session_id", session->id},
                        {"turn", turn}
                    };
                    std::string chunk = hb.dump() + "\n";
                    if (!sink.write(chunk.data(), chunk.size())) {
                        LOG_DEBUG("Client disconnected during heartbeat for session {}", session->id);
                        return false;
                    }
                    last_write = now;
                }
                return true;
            } catch (const std::exception& e) {
                LOG_ERROR("Chunked provider exception for session {}: {}", session->id, e.what());
                return false;
            } catch (...) {
                LOG_ERROR("Chunked provider unknown exception for session {}", session->id);
                return false;
            }
        }
    );
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "*");
    res.set_header("Cache-Control", "no-cache, no-transform");
    res.set_header("Connection", "keep-alive");
    res.set_header("X-Accel-Buffering", "no");
}
}  // namespace

void shutdown_active_sessions() {
    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    for (auto& [id, sess] : g_sessions) {
        if (sess) abort_turn(*sess);
    }
}


std::string load_persona_prompt(const std::string& persona, const std::string& workspace) {
    if (persona.empty()) return "";
    std::vector<std::filesystem::path> candidates;

    std::filesystem::path p(persona);
    if (p.is_absolute()) {
        candidates.push_back(p);
    }

    if (!workspace.empty()) {
        candidates.push_back(std::filesystem::path(workspace) / "persona" / (persona + ".md"));
        candidates.push_back(std::filesystem::path(workspace) / "persona" / persona);
        candidates.push_back(std::filesystem::path(workspace) / (persona + ".md"));
    }

    const char* home = std::getenv("HOME");
    std::filesystem::path home_path = home ? std::filesystem::path(home) : std::filesystem::path("/home/abhi");
    candidates.push_back(home_path / "notes" / "persona" / (persona + ".md"));
    candidates.push_back(home_path / "notes" / "persona" / persona);

    for (const auto& path : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) {
            std::ifstream ifs(path);
            if (ifs) {
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                LOG_INFO("Loaded persona '{}' from {}", persona, path.string());
                return buffer.str();
            }
        }
    }

    LOG_WARN("Could not find persona file for '{}'", persona);
    return "";
}

nlohmann::json list_available_personas(const std::string& workspace) {
    nlohmann::json result = nlohmann::json::array();
    std::vector<std::filesystem::path> scan_dirs;
    if (!workspace.empty()) {
        scan_dirs.push_back(std::filesystem::path(workspace) / "persona");
    }
    const char* home = std::getenv("HOME");
    std::filesystem::path home_path = home ? std::filesystem::path(home) : std::filesystem::path("/home/abhi");
    scan_dirs.push_back(home_path / "notes" / "persona");

    std::unordered_set<std::string> seen_ids;
    for (const auto& dir : scan_dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                std::string id = entry.path().stem().string();
                if (seen_ids.insert(id).second) {
                    std::string title = id;
                    if (!title.empty()) title[0] = std::toupper(static_cast<unsigned char>(title[0]));
                    std::string desc = "";
                    std::ifstream ifs(entry.path());
                    if (ifs) {
                        std::string line;
                        while (std::getline(ifs, line)) {
                            if (line.rfind("You are the ", 0) == 0 || line.rfind("You are a ", 0) == 0) {
                                desc = line;
                                break;
                            }
                        }
                    }
                    result.push_back({
                        {"id", id},
                        {"name", title},
                        {"file", entry.path().filename().string()},
                        {"path", entry.path().string()},
                        {"description", desc}
                    });
                }
            }
        }
    }
    return result;
}

ResolvedModel resolve_provider_model(const std::vector<qcode::ProviderInfo>& providers,
                                     const std::string& provider,
                                     const std::string& model,
                                     httplib::Response& res) {
    ResolvedModel out;
    for (const auto& p : providers) {
        if (provider.empty() || p.id == provider || p.name == provider) {
            out.provider = &p;
            break;
        }
    }
    if (!out.provider) {
        nlohmann::json ids = nlohmann::json::array();
        for (const auto& p : providers) ids.push_back(p.id);
        res.status = 400;
        res.set_content(nlohmann::json({
            {"error", "provider '" + provider + "' is not configured"},
            {"providers", ids}
        }).dump(), "application/json");
        return out;
    }
    for (const auto& m : out.provider->models) {
        if (m.id == model || m.name == model) {
            out.model = &m;
            break;
        }
    }
    if (!out.model && !out.provider->models.empty()) {
        out.model = &out.provider->models.front();
        if (!model.empty()) {
            LOG_WARN("Model '{}' is not configured for provider '{}'; using '{}'",
                     model, out.provider->id, out.model->id);
        }
    }
    return out;
}

void register_session_routes(
    httplib::Server& svr,
    std::shared_ptr<qcode::bus::BusRuntime> bus,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers_list,
    const ServerSetupOptions& options) {
    std::string default_workspace = options.default_workspace;

    // Deliver bus events on the publishing thread, so turns are persisted and
    // streams woken immediately, whether or not an HTTP client is attached.
    if (bus) {
        bus->set_wake_callback([weak = std::weak_ptr<qcode::bus::BusRuntime>(bus)] {
            if (auto b = weak.lock()) deliver_bus_events(*b);
        });
    }

auto handle_generate = [bus, providers_list, default_workspace](const std::string& session_id, const std::string& req_body, httplib::Response& res) {
    ScopedSessionLog scoped_session_log(session_id);
    // Parse request body
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req_body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }

    const bool is_resume = body.value("resume", false);
    std::string text = body.value("text", "");
    if (text.empty() && !is_resume) {
        res.status = 400;
        res.set_content(R"({"error":"'text' field is required"})", "application/json");
        return;
    }

    if (is_resume) {
        // Reattach to the running turn (reload, dropped stream, other tab).
        // The event cursor, unsaved text and persisted row count are taken
        // under the lock turn rows are written under. History is loaded after
        // it and cut to that count: later rows arrive as events, so nothing
        // is missed or repeated, and the turn is not stalled by the load.
        auto session = find_session(session_id);
        uint64_t turn = 0;
        uint64_t cursor = 0;
        std::shared_ptr<void> stream;
        size_t rows = 0;
        nlohmann::json snapshot;
        if (session) {
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            if (session->generating) {
                turn = session->active_turn;
                cursor = session->events_base + session->events.size();
                stream = attach_stream(session);
                if (auto info = qcode::session::get_session_info(session_id)) {
                    rows = static_cast<size_t>(info->message_count);
                }
                snapshot = {
                    {"type", "session.snapshot"},
                    {"session_id", session_id},
                    {"reasoning_text", session->reasoning_text},
                    {"assistant_text", session->assistant_text}
                };
            }
        }
        if (turn == 0) {
            nlohmann::json done = {{"type", "generation.complete"}, {"session_id", session_id}};
            res.set_content(done.dump() + "\n", "application/x-ndjson; charset=utf-8");
            return;
        }
        snapshot["messages"] = session_messages_json(session_id, rows);
        stream_turn(res, session, turn, cursor, std::move(stream), std::move(snapshot));
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
    const auto resolved = resolve_provider_model(*providers_list, provider, model, res);
    if (!resolved.provider) return;
    provider = resolved.provider->id;
    if (resolved.model) model = resolved.model->id;
    const bool vision_supported = resolved.model && resolved.model->vision;
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

    std::string persona_name = body.value("persona", "");
    if (persona_name.empty()) {
        persona_name = qcode::session::get_session_persona(session_id);
    }
    std::string session_ws = qcode::session::get_session_workspace(session_id);
    if (session_ws.empty()) session_ws = default_workspace;

    std::string system_prompt;
    if (body.contains("system_prompt") && body["system_prompt"].is_string() &&
        !body["system_prompt"].get<std::string>().empty()) {
        system_prompt = body["system_prompt"].get<std::string>();
    } else if (!persona_name.empty()) {
        std::string persona_prompt = load_persona_prompt(persona_name, session_ws);
        if (!persona_prompt.empty()) {
            system_prompt = qcode::SystemPrompt::build(persona_prompt, qcode::ToolConfig::orchestrator(vision_supported));
            LOG_INFO("Session {}: loaded persona '{}'", session_id, persona_name);
        }
    }

    if (system_prompt.empty()) {
        if (study_mode) {
            system_prompt =
                qcode::SystemPrompt::build(qcode::SystemPrompt::study_identity());
        } else {
            system_prompt = qcode::SystemPrompt::build_default(qcode::ToolConfig::orchestrator(vision_supported));
        }
    }
    std::string reasoning_mode = body.value("reasoning_mode", "off");
    std::string agent_mode = body.value("agent_mode", "");
    if (agent_mode.empty()) {
        auto modes = qcode::session::get_session_modes(session_id);
        agent_mode = modes.first;
    }
    if (agent_mode.empty()) {
        agent_mode = (session_id.rfind("ses_", 0) == 0) ? "subagent" : "orchestrator";
    }

    std::shared_ptr<GenSession> session;
    {
        std::lock_guard<std::mutex> lock(g_sessions_mutex);
        auto& slot = g_sessions[session_id];
        if (!slot) {
            slot = std::make_shared<GenSession>();
            slot->id = session_id;
            slot->subs = std::make_shared<std::vector<qcode::bus::Subscription>>(
                subscribe_session(*bus, slot));
        }
        session = slot;
    }

    // Resolve workspace
    std::string ws = qcode::session::get_session_workspace(session->id);
    if (ws.empty()) {
        ws = default_workspace;
    }

    // ── Optional image attachments ──
    // body.attachments = [{mime_type?, data|path, description?}] become
    // ImageContentPart on the user message; each provider's request builder
    // converts them to its own image format (image_url / input_image,
    // Anthropic image block, Gemini inlineData).
    std::vector<qcode::ImageContentPart> image_attachments;
    if (body.contains("attachments") && body["attachments"].is_array()) {
        constexpr size_t kMaxDecodedBytes = 10 * 1024 * 1024;
        auto attachment_error = [&](const std::string& msg) {
            res.status = 400;
            nlohmann::json err_j;
            err_j["error"] = msg;
            res.set_content(err_j.dump(), "application/json");
        };
        for (const auto& att : body["attachments"]) {
            if (!att.is_object()) {
                attachment_error("attachment must be an object");
                return;
            }
            std::string data_b64 = att.value("data", "");
            const std::string att_path = att.value("path", "");
            std::string mime = att.value("mime_type", "");
            const std::string desc = att.value("description", "");
            if (data_b64.empty() && att_path.empty()) {
                attachment_error("attachment needs 'data' (base64) or 'path'");
                return;
            }
            if (!att_path.empty()) {
                // Same loader and limits as the agent's `image` tool.
                try {
                    qcode::ToolExecutionContext ctx;
                    ctx.workspace = ws;
                    const auto img = qcode::ImageTool::execute(
                        nlohmann::json{{"path", att_path}}, ctx);
                    mime = img["mime_type"].get<std::string>();
                    data_b64 = img["data"].get<std::string>();
                } catch (const std::exception& e) {
                    attachment_error(std::string("attachment: ") + e.what());
                    return;
                }
            } else {
                // Estimate decoded size from the base64 length.
                if (data_b64.size() / 4 * 3 > kMaxDecodedBytes) {
                    attachment_error("attachment too large (max 10 MiB)");
                    return;
                }
                if (mime.empty()) {
                    attachment_error(
                        "attachment 'mime_type' is required with raw 'data'");
                    return;
                }
            }
            if (mime.rfind("image/", 0) != 0) {
                attachment_error("unsupported attachment media type: " + mime);
                return;
            }
            image_attachments.emplace_back(data_b64, mime, desc);
        }
    }

    // Plain-text rows stay legacy-compatible; with attachments, persist an
    // envelope so reloads keep the images.
    std::string user_row = text;
    if (!image_attachments.empty()) {
        nlohmann::json env{{"text", text}, {"images", nlohmann::json::array()}};
        for (const auto& img : image_attachments) {
            nlohmann::json ji{{"mime_type", img.mime_type},
                              {"data", img.data}};
            if (!img.description.empty()) ji["description"] = img.description;
            env["images"].push_back(std::move(ji));
        }
        user_row = env.dump();
    }

    uint64_t turn_id = 0;
    uint64_t cursor = 0;
    std::shared_ptr<void> stream;
    {
        std::unique_lock<std::mutex> lock(session->queue_mutex);
        // A stopped turn is still winding down: let it end so this prompt
        // starts a new turn instead of continuing the stopped one.
        session->queue_cv.wait_for(lock, std::chrono::seconds(10), [&] {
            return !session->generating || !session->abort_flag->load();
        });
        if (session->generating && session->abort_flag->load()) {
            res.status = 409;
            res.set_content(R"({"error":"the stopped turn is still ending; try again"})", "application/json");
            return;
        }
        if (session->generating) {
            // Steer the running turn: its next model request takes the prompt.
            if (!image_attachments.empty()) {
                res.status = 409;
                res.set_content(R"({"error":"attachments cannot be sent while a turn is running"})", "application/json");
                return;
            }
            session->pending_prompts.push_back(text);
            res.status = 202;
            res.set_content(nlohmann::json({
                {"queued", true},
                {"turn", session->active_turn.load()}
            }).dump(), "application/json");
            return;
        }
        turn_id = next_turn_id();
        session->active_turn = turn_id;
        session->generating = true;
        session->abort_flag = std::make_shared<std::atomic<bool>>(false);
        session->tool_call_count = 0;
        session->total_tool_time_ms = 0;
        session->error.clear();
        session->assistant_text.clear();
        session->reasoning_text.clear();
        cursor = session->events_base + session->events.size();
        stream = attach_stream(session);
        qcode::session::save_message(session->id, "User", user_row);
    }
    qcode::session::set_session_provider_model(session->id, provider, model);
    qcode::Messages messages = qcode::apply_compaction_cutoff(
        qcode::session::load_session_history_parsed(session->id));

    // The turn runs on its own thread and persists itself; streams only read.
    std::thread([bus, session, provider, model, system_prompt, ws, reasoning_mode,
                 agent_mode, messages = std::move(messages)]() mutable {
        // Route this worker's logs to the session's own file.
        qcode::logger::set_thread_session_id(session->id);
        qcode::logger::set_thread_name("gen:" + session->id.substr(0, 8));
        for (;;) {
            std::shared_ptr<std::atomic<bool>> abort_flag;
            {
                std::lock_guard<std::mutex> lock(session->queue_mutex);
                abort_flag = session->abort_flag;
            }
            qcode::GenerationContext ctx{
                .session_id = session->id,
                .reasoning_mode = reasoning_mode,
                .agent_mode = agent_mode,
                .workspace = ws,
                .abort_flag = abort_flag,
                .has_queued_work = [session] {
                    std::lock_guard<std::mutex> lock(session->queue_mutex);
                    return !session->pending_prompts.empty();
                },
                .take_queued_prompts = [session, abort_flag] {
                    std::lock_guard<std::mutex> lock(session->queue_mutex);
                    if (abort_flag->load()) return std::vector<std::string>{};
                    return std::exchange(session->pending_prompts, {});
                }
            };
            try {
                g_backend->run_generation(provider, model, system_prompt, messages, true, ctx);
            } catch (const std::exception& e) {
                LOG_ERROR("Generation error: {}", e.what());
                std::lock_guard<std::mutex> lock(session->queue_mutex);
                session->error = e.what();
            } catch (...) {
                LOG_ERROR("Unknown error during generation");
                std::lock_guard<std::mutex> lock(session->queue_mutex);
                session->error = "Unknown error during generation";
            }

            // Every event of the run is now persisted and queued for streams.
            deliver_bus_events(*bus);
            std::string next;
            {
                std::lock_guard<std::mutex> lock(session->queue_mutex);
                flush_turn_text(*session);
                for (const auto& prompt : std::exchange(session->pending_prompts, {})) {
                    if (!next.empty()) next += "\n\n";
                    next += prompt;
                }
                if (next.empty()) {
                    session->generating = false;
                    trim_events(*session);
                    session->queue_cv.notify_all();
                    return;
                }
                // Prompts that came too late for this run continue the turn.
                session->error.clear();
                session->abort_flag = std::make_shared<std::atomic<bool>>(false);
            }
            bus->publish<UserMessageInjected>({.session_id = session->id, .text = next});
            deliver_bus_events(*bus);
            messages = qcode::apply_compaction_cutoff(
                qcode::session::load_session_history_parsed(session->id));
        }
    }).detach();

    nlohmann::json started = {{"type", "session.started"}, {"session_id", session->id}};
    stream_turn(res, session, turn_id, cursor, std::move(stream), std::move(started));
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
    std::string parent_sid = req.has_param("parent_session_id") ? req.get_param_value("parent_session_id") : "";
    auto list = qcode::session::list_sessions_full(query_flag_true(req, "include_subagents"), parent_sid);
    nlohmann::json j = nlohmann::json::array();
    for (const auto& s : list) {
        j.push_back(session_public_json(s));
    }
    res.set_content(j.dump(2), "application/json");
});

// ── Live delegated child sessions (TaskTool registry) ──
svr.Get("/tasks", [](const httplib::Request& req, httplib::Response& res) {
    std::string parent_sid = req.has_param("parent_session_id")
                                 ? req.get_param_value("parent_session_id")
                                 : (req.has_param("session_id") ? req.get_param_value("session_id") : "");
    res.set_content(qcode::TaskTool::list_tasks(parent_sid).dump(2), "application/json");
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
    std::string custom_id = body.value("title", "");
    if (custom_id.empty()) {
        custom_id = body.value("custom_id", "");
    }
    if (provider.empty() || model.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"provider and model required"})", "application/json");
        return;
    }
    std::string persona = body.value("persona", "");
    auto id = qcode::session::create_new_session(provider, model, workspace, custom_id);
    if (!persona.empty()) {
        qcode::session::set_session_persona(id, persona);
    }
    std::string title = qcode::session::get_session_title(id);
    if (title.empty()) {
        title = custom_id.empty() ? ("Session - " + model) : custom_id;
    }
    res.set_content(nlohmann::json({{"id", id}, {"workspace", workspace}, {"title", title}, {"persona", persona}}).dump(), "application/json");
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
// Waits briefly for the turn to end, so a client refreshing right after Stop
// already sees the session idle.
auto cancel_session = [](const std::string& session_id, httplib::Response& res) {
    auto session = find_session(session_id);
    if (!session) {
        res.status = 404;
        res.set_content(R"({"error":"session not found"})", "application/json");
        return;
    }
    abort_turn(*session);
    {
        std::unique_lock<std::mutex> lock(session->queue_mutex);
        session->queue_cv.wait_for(lock, std::chrono::seconds(3),
                                   [&] { return !session->generating; });
    }
    res.set_content(R"({"status":"cancelled"})", "application/json");
};

svr.Post("/session/cancel", [cancel_session](const httplib::Request& req, httplib::Response& res) {
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
    cancel_session(session_id, res);
});

svr.Post("/session/([^/]+)/cancel", [cancel_session](const httplib::Request& req, httplib::Response& res) {
    cancel_session(url_decode(req.matches[1]), res);
});

svr.Post("/session/([^/]+)/abort", [cancel_session](const httplib::Request& req, httplib::Response& res) {
    cancel_session(url_decode(req.matches[1]), res);
});

// ── Withdraw prompts the running turn has not taken yet ──
// Body {"text": "..."} drops that prompt; an empty body drops all of them.
svr.Delete("/session/([^/]+)/queue", [](const httplib::Request& req, httplib::Response& res) {
    std::string text;
    try {
        if (!req.body.empty()) text = nlohmann::json::parse(req.body).value("text", "");
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }
    size_t removed = 0;
    if (auto session = find_session(url_decode(req.matches[1]))) {
        std::lock_guard<std::mutex> lock(session->queue_mutex);
        auto& queue = session->pending_prompts;
        const size_t before = queue.size();
        if (text.empty()) {
            queue.clear();
        } else {
            queue.erase(std::remove(queue.begin(), queue.end(), text), queue.end());
        }
        removed = before - queue.size();
    }
    res.set_content(nlohmann::json({{"removed", removed}}).dump(), "application/json");
});


// `?messages=0` leaves out the history (clients that only need the id).
svr.Get("/session/last", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = qcode::session::get_last_active_session();
    if (sid.empty()) {
        res.set_content(R"({"id":""})", "application/json");
        return;
    }
    nlohmann::json info = {{"id", sid}};
    if (auto s = qcode::session::get_session_info(sid)) info = session_public_json(*s);
    const std::string with_messages = req.get_param_value("messages");
    if (with_messages != "0" && with_messages != "false") {
        info["messages"] = session_messages_json(sid);
    }
    res.set_content(info.dump(2), "application/json");
});

// ── Get session info (includes delegated child / subagent rows) ──
// Cheap (no history): clients poll it to reattach to a running turn
// (`generating`, `turn`) and to refetch /messages only when
// `message_count` / `updated_at` moved.
svr.Get("/session/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    ScopedSessionLog scoped_log(sid);
    if (auto s = qcode::session::get_session_info(sid)) {
        auto info = session_public_json(*s);
        info["message_count"] = s->message_count;
        info["updated_at"] = s->last_active_at;
        bool generating = false;
        uint64_t turn = 0;
        if (auto live = find_session(sid)) {
            std::lock_guard<std::mutex> lock(live->queue_mutex);
            generating = live->generating;
            turn = live->active_turn;
        }
        info["generating"] = generating;
        info["turn"] = turn;
        res.set_content(info.dump(), "application/json");
        return;
    }
    res.status = 404;
    res.set_content(R"({"error":"session not found"})", "application/json");
});

// ── Set agent mode (plan <-> orchestrator), matching TUI toggle ──

// ── Set session persona ──
svr.Post("/session/([^/]+)/persona", [](const httplib::Request& req, httplib::Response& res) {
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
    std::string persona = body.value("persona", "");
    qcode::session::set_session_persona(sid, persona);
    res.set_content(nlohmann::json({{"ok", true}, {"persona", persona}}).dump(), "application/json");
});

// ── List available personas ──
svr.Get("/personas", [default_workspace](const httplib::Request& req, httplib::Response& res) {
    std::string ws = req.has_param("workspace") ? req.get_param_value("workspace") : default_workspace;
    res.set_content(list_available_personas(ws).dump(2), "application/json");
});
svr.Get("/api/personas", [default_workspace](const httplib::Request& req, httplib::Response& res) {
    std::string ws = req.has_param("workspace") ? req.get_param_value("workspace") : default_workspace;
    res.set_content(list_available_personas(ws).dump(2), "application/json");
});

// ── Delete session ──
svr.Delete("/session/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }

    // Find any child sessions so we can cancel active generations on them too
    auto children = qcode::session::get_child_session_ids(sid);

    // Cancel any active generation on that session and child sessions
    {
        std::lock_guard<std::mutex> lock(g_sessions_mutex);
        auto cancel_gen = [](const std::string& target_id) {
            auto it = g_sessions.find(target_id);
            if (it != g_sessions.end()) {
                abort_turn(*it->second);
                g_sessions.erase(it);
            }
        };
        cancel_gen(sid);
        for (const auto& child_id : children) {
            cancel_gen(child_id);
        }
    }

    // Prune and abort any in-memory tasks belonging to this session or its children
    qcode::TaskTool::delete_session_tasks(sid);
    for (const auto& child_id : children) {
        qcode::TaskTool::delete_session_tasks(child_id);
    }

    // Delete from database (cascades to children)
    qcode::session::delete_session(sid);

    // Release the log sinks for sessions that no longer exist. Files stay
    // on disk; this only frees the in-memory handles.
    qcode::server::prune_session_logs();

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

    // Permanently truncate the persisted history in SQLite.
    qcode::session::overwrite_session_history(sid, {});

    res.set_content(R"({"ok":true})", "application/json");
});

// ── Get session message history ──
svr.Get("/session/([^/]+)/messages", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    ScopedSessionLog scoped_log(sid);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    res.set_content(session_messages_json(sid).dump(2), "application/json");
});

    // ── Get session logs ──
    auto handle_session_logs = [](const httplib::Request& req, httplib::Response& res) {
        std::string sid = url_decode(req.matches[1]);
        bool session_live = false;
        {
            std::lock_guard<std::mutex> lock(g_sessions_mutex);
            if (g_sessions.find(sid) != g_sessions.end()) session_live = true;
        }
        if (!session_live) {
            session_live = qcode::session::session_exists(sid);
        }
        if (!session_live) {
            res.status = 404;
            res.set_content(R"({"error":"session not found"})", "application/json");
            return;
        }
        if (!g_session_logger) {
            res.status = 503;
            res.set_content(R"({"error":"session logger not active"})", "application/json");
            return;
        }

        g_session_logger->flush();
        std::string log_path = g_session_logger->path_for(sid);
        std::error_code ec;
        bool exists = std::filesystem::exists(log_path, ec);
        size_t file_bytes = exists ? std::filesystem::file_size(log_path, ec) : 0;

        size_t limit = 1000;
        if (req.has_param("lines")) {
            try { limit = std::stoul(req.get_param_value("lines")); } catch (...) {}
        }

        std::vector<std::string> lines;
        if (exists && file_bytes > 0) {
            std::ifstream f(log_path);
            std::string line;
            while (std::getline(f, line)) {
                lines.push_back(std::move(line));
                if (lines.size() > limit * 2) {
                    lines.erase(lines.begin(), lines.begin() + (lines.size() - limit));
                }
            }
            if (lines.size() > limit) {
                lines.erase(lines.begin(), lines.begin() + (lines.size() - limit));
            }
        }

        std::string content;
        for (const auto& l : lines) {
            content += l;
            content += "\n";
        }

        if (req.has_param("raw") || (req.has_header("Accept") && req.get_header_value("Accept") == "text/plain")) {
            res.set_content(content, "text/plain; charset=utf-8");
        } else {
            nlohmann::json j = {
                {"session_id", sid},
                {"log", content},
                {"path", log_path},
                {"exists", exists},
                {"size_bytes", file_bytes},
                {"line_count", lines.size()}
            };
            res.set_content(j.dump(2), "application/json");
        }
    };

    svr.Get("/session/([^/]+)/logs", handle_session_logs);
    svr.Get("/api/session/([^/]+)/logs", handle_session_logs);

    // ── Get unscoped server logs ──
    auto handle_server_logs = [](const httplib::Request& req, httplib::Response& res) {
        if (!g_session_logger) {
            res.status = 503;
            res.set_content(R"({"error":"session logger not active"})", "application/json");
            return;
        }

        g_session_logger->flush();
        std::string log_path = g_session_logger->path_for("");
        std::error_code ec;
        bool exists = std::filesystem::exists(log_path, ec);
        size_t file_bytes = exists ? std::filesystem::file_size(log_path, ec) : 0;

        size_t limit = 1000;
        if (req.has_param("lines")) {
            try { limit = std::stoul(req.get_param_value("lines")); } catch (...) {}
        }

        std::vector<std::string> lines;
        if (exists && file_bytes > 0) {
            std::ifstream f(log_path);
            std::string line;
            while (std::getline(f, line)) {
                lines.push_back(std::move(line));
                if (lines.size() > limit * 2) {
                    lines.erase(lines.begin(), lines.begin() + (lines.size() - limit));
                }
            }
            if (lines.size() > limit) {
                lines.erase(lines.begin(), lines.begin() + (lines.size() - limit));
            }
        }

        std::string content;
        for (const auto& l : lines) {
            content += l;
            content += "\n";
        }

        if (req.has_param("raw") || (req.has_header("Accept") && req.get_header_value("Accept") == "text/plain")) {
            res.set_content(content, "text/plain; charset=utf-8");
        } else {
            nlohmann::json j = {
                {"log", content},
                {"path", log_path},
                {"exists", exists},
                {"size_bytes", file_bytes},
                {"line_count", lines.size()}
            };
            res.set_content(j.dump(2), "application/json");
        }
    };

    svr.Get("/logs", handle_server_logs);
    svr.Get("/api/logs", handle_server_logs);

    register_session_ops_routes(svr, providers_list);
}

}  // namespace server
}  // namespace qcode
