#include "server_routes.h"

#include <qcode/core/file_logger.h>
#include <qcode/providers/registry.h>
#include <filesystem>
#include <fstream>
#include <qcode/session/generation_service.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/core/config.h>
#include <qcode/session/session_store.h>
#include <qcode/session/study_assistant.h>
#include <qcode/session/study_store.h>
#include <qcode/core/state.h>
#include <qcode/session/system_prompt.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>
#include <qcode/ui/bus_json_codec.h>
#include <qcode/core/uuid.h>

#include "http_utils.h"
#include "terminal_pty.h"
#include "workspace_fs.h"

#include <nlohmann/json.hpp>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <map>
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>

namespace fs = std::filesystem;

using namespace qcode::contract;
using qcode::server::url_decode;
using qcode::server::shell_quote;
using qcode::server::exec_capture;
using qcode::server::expand_tilde;
using qcode::server::split_lines;
using qcode::server::kMaxFsFileBytes;
using qcode::server::resolve_session_workspace;
using qcode::server::resolve_workspace_path;
using qcode::server::looks_binary;
using qcode::server::TerminalSession;
using qcode::server::create_terminal;
using qcode::server::destroy_terminal;
using qcode::server::find_terminal;

namespace qcode {
namespace server {

std::unique_ptr<qcode::GenerationService> g_backend;

namespace {

std::shared_ptr<qcode::bus::BusRuntime> g_bus;

static std::string get_mime_type(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = std::tolower(static_cast<unsigned char>(c));
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "webp") return "image/webp";
    if (ext == "ico") return "image/x-icon";
    if (ext == "bmp") return "image/bmp";
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js" || ext == "mjs") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "xml") return "application/xml";
    if (ext == "pdf") return "application/pdf";
    if (ext == "txt" || ext == "log" || ext == "md") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

// ── Per-generation session state ──
struct GenSession {
    std::string id;
    qcode::GenerationContext ctx;
    qcode::Messages messages;
    std::vector<nlohmann::json> event_queue;
    std::mutex queue_mutex;
    std::atomic<bool> done{false};
    std::atomic<bool> generation_started{false};
    std::string error;
    std::string assistant_text;  // accumulated (cumulative) assistant reply, for DB persistence
    std::string reasoning_text;  // accumulated reasoning, for DB persistence
    std::shared_ptr<std::vector<qcode::bus::Subscription>> subs;
    // Live token accounting (accumulated from TokenUsageUpdated during generation).
    std::atomic<int> live_prompt_tokens{0};
    std::atomic<int> live_completion_tokens{0};
    std::atomic<int> live_total_tokens{0};
};

static std::mutex g_sessions_mutex;
static std::unordered_map<std::string, std::shared_ptr<GenSession>> g_sessions;

// ── Event collector: subscribes to bus and queues events for a session ──
static std::vector<qcode::bus::Subscription> subscribe_session(
    qcode::bus::BusRuntime& bus,
    std::shared_ptr<GenSession> session)
{
    std::vector<qcode::bus::Subscription> subs;

    subs.push_back(bus.subscribe<MessageDelta>(
        [session](const MessageDelta::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = qcode::server::message_delta_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ToolCallStarted>(
        [session](const ToolCallStarted::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            nlohmann::json call_json = {
                {"id", p.tool_call_id},
                {"name", p.tool_name},
                {"arguments", p.arguments},
            };
            qcode::session::save_message(p.session_id, "ToolCall", call_json.dump());

            auto j = qcode::server::tool_call_started_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ToolCallCompleted>(
        [session](const ToolCallCompleted::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            nlohmann::json result_json = {
                {"tool_call_id", p.tool_call_id},
                {"tool_name", p.tool_name},
                {"result", p.result},
                {"is_error", p.is_error},
                {"duration_ms", p.duration_ms},
            };
            qcode::session::save_message(p.session_id, "ToolResult", result_json.dump());

            auto j = qcode::server::tool_call_completed_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<SessionStatusChanged>(
        [session](const SessionStatusChanged::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = qcode::server::session_status_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ErrorOccurred>(
        [session](const ErrorOccurred::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = qcode::server::error_occurred_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ReasoningDelta>(
        [session](const ReasoningDelta::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = qcode::server::reasoning_delta_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<TokenUsageUpdated>(
        [session](const TokenUsageUpdated::Payload& p) {
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


}  // namespace

void setup_server_routes(
    httplib::Server& svr,
    std::shared_ptr<qcode::bus::BusRuntime> bus,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers_list,
    const ServerSetupOptions& options) {
  g_bus = std::move(bus);
  g_backend = std::make_unique<qcode::GenerationService>(*g_bus, *providers_list);
  const std::string default_workspace = options.default_workspace;

  if (!options.webui_dir.empty()) {
    if (access(options.webui_dir.c_str(), F_OK) == 0) {
      LOG_INFO("Serving Web UI from {}", options.webui_dir);
      svr.set_mount_point("/", options.webui_dir);
    } else {
      LOG_WARN("Web UI directory not found at {}", options.webui_dir);
    }
  }

  // Also expose /api/health for Android clients that probe it.
  svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"status":"ok"})", "application/json");
  });

// Health check
svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"status":"ok"})", "application/json");
});

// Provider list
svr.Get("/providers", [providers_list](const httplib::Request&, httplib::Response& res) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& p : *providers_list) {
        nlohmann::json models = nlohmann::json::array();
        for (const auto& m : p.models) {
            models.push_back({{"id", m.id}, {"name", m.name}});
        }
        j.push_back({{"id", p.id}, {"name", p.name}, {"models", models}});
    }
    res.set_content(j.dump(2), "application/json");
});

// Model Performance & Telemetry List
svr.Get("/api/models/performance", [](const httplib::Request&, httplib::Response& res) {
    auto summaries = qcode::session::list_all_model_performance_summaries();
    nlohmann::json j = nlohmann::json::array();
    for (const auto& s : summaries) {
        j.push_back({
            {"model_id", s.model_id},
            {"provider", s.provider},
            {"model_name", s.model_name},
            {"architecture_info", s.architecture_info},
            {"context_window", s.context_window},
            {"output_limit", s.output_limit},
            {"tool_call_supported", s.tool_call_supported},
            {"multi_turn_reliable", s.multi_turn_reliable},
            {"verified_benchmark", s.verified_benchmark},
            {"recommended_for", s.recommended_for},
            {"total_turns", s.total_turns},
            {"successful_turns", s.successful_turns},
            {"failed_turns", s.failed_turns},
            {"tool_loop_failures", s.tool_loop_failures},
            {"avg_latency_ms", s.avg_latency_ms},
            {"positive_feedback_count", s.positive_feedback_count},
            {"negative_feedback_count", s.negative_feedback_count},
            {"success_rate", s.success_rate()}
        });
    }
    res.set_content(j.dump(2), "application/json");
});

// Model User Feedback
svr.Post("/api/models/feedback", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = nlohmann::json::parse(req.body);
        std::string model_id = body.value("model_id", "");
        std::string rating = body.value("rating", "up");
        if (model_id.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"model_id is required"})", "application/json");
            return;
        }
        qcode::session::record_user_feedback(model_id, rating == "up");
        res.set_content(R"({"status":"success"})", "application/json");
    } catch (const std::exception& e) {
        res.status = 400;
        res.set_content(R"({"error":"invalid json"})", "application/json");
    }
});

// ── Helper to handle generation ──
auto handle_generate = [providers_list, default_workspace](const std::string& session_id, const std::string& req_body, httplib::Response& res) {
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
        if (session->ctx.abort_flag) {
            session->ctx.abort_flag->store(true);
        }
        int wait_count = 0;
        while (!session->done.load() && wait_count < 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            wait_count++;
        }
    }

    // Reset per-turn state so each /generate starts a fresh generation
    // thread (fixes broken multi-turn after the first reply).
    session->generation_started = false;
    session->done = false;
    session->error.clear();
    session->assistant_text.clear();
    session->reasoning_text.clear();
    if (session->ctx.abort_flag) {
        session->ctx.abort_flag->store(false);
    }
    {
        std::lock_guard<std::mutex> lock(session->queue_mutex);
        session->event_queue.clear();
    }

    // Load (or reload) the entire session history from the SQLite database
    // to ensure we have the complete, up-to-date context (including ToolCall,
    // ToolResult, System, and Assistant messages).
    session->messages = qcode::session::load_session_history_parsed(session->id);

    // Set up generation context (reasoning mode can change per request)
    {
        auto ws = qcode::session::get_session_workspace(session->id);
        if (ws.empty()) {
            ws = default_workspace;
        }
        session->ctx = qcode::GenerationContext{
            .session_id = session->id,
            .reasoning_mode = reasoning_mode,
            .workspace = ws
        };
    }

    // Subscribe to bus events for this session (once)
    if (!session->subs) {
        session->subs = std::make_shared<std::vector<qcode::bus::Subscription>>(
            subscribe_session(*g_bus, session));
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
        [session,
         provider, model, system_prompt, messages = std::move(messages),
         reasoning_mode](size_t /*offset*/, httplib::DataSink& sink) mutable -> bool
        {
            // Start generation in a background thread (once per session)
            if (!session->generation_started.exchange(true)) {
                // Send session.started event first
                nlohmann::json start_msg = {
                    {"type", "session.started"},
                    {"session_id", session->id}
                };
                std::string chunk = start_msg.dump() + "\n";
                if (!sink.write(chunk.data(), chunk.size())) {
                    return false;
                }

                // Launch background thread for generation
                std::thread gen_thread([session, provider, model,
                                        system_prompt, messages = std::move(messages)]() {
                    try {
                        g_backend->run_generation(
                            provider, model, system_prompt,
                            messages, true, session->ctx);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Generation error: {}", e.what());
                        session->error = e.what();
                    }
                    session->done = true;
                });
                gen_thread.detach();
            }

            // Dispatch bus events to subscribers (bus requires explicit drain)
            g_bus->drain();

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
                if (evt.value("type", "") == "backend.reasoning.delta") {
                    session->reasoning_text += evt.value("text", "");
                }
                std::string chunk = evt.dump() + "\n";
                if (!sink.write(chunk.data(), chunk.size())) {
                    return false;
                }
            }

            // If generation is done and queue is drained, send final event
            if (session->done && events.empty()) {
                // One final drain to catch events queued between our drain and done becoming true
                g_bus->drain();
                {
                    std::lock_guard<std::mutex> lock(session->queue_mutex);
                    events.swap(session->event_queue);
                }
                for (const auto& evt : events) {
                    std::string chunk = evt.dump() + "\n";
                    if (!sink.write(chunk.data(), chunk.size())) {
                        return false;
                    }
                }
            }
            if (session->done && events.empty()) {
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
                    {"tool_call_count", session->ctx.tool_call_count},
                    {"total_tool_time_ms", session->ctx.total_tool_time_ms}
                };
                if (!session->error.empty()) {
                    final_msg["error"] = session->error;
                }
                std::string chunk = final_msg.dump() + "\n";
                if (!sink.write(chunk.data(), chunk.size())) {
                    return false;
                }
                sink.done();
                // cpp-httplib treats false as a cancelled provider. sink.done()
                // already emitted the terminating chunk, so report success.
                return true;
            }

            // Avoid a hot loop while waiting for the provider or a tool.
            if (events.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return true;
        }
    );
    res.set_header("Cache-Control", "no-cache, no-transform");
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
svr.Get("/sessions", [](const httplib::Request&, httplib::Response& res) {
    auto list = qcode::session::list_sessions_full();
    nlohmann::json j = nlohmann::json::array();
    for (const auto& s : list) {
        j.push_back({{"id", s.id}, {"title", s.title}, {"workspace", s.workspace},
                     {"provider", s.provider}, {"model", s.model}});
    }
    res.set_content(j.dump(2), "application/json");
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
        if (it->second->ctx.abort_flag) {
            it->second->ctx.abort_flag->store(true);
        }
        res.set_content(R"({"status":"cancelled"})", "application/json");
    } else {
        res.status = 404;
        res.set_content(R"({"error":"session not found"})", "application/json");
    }
});

// ── Terminal: create ──
svr.Post("/terminal/create", [](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }
    std::string workspace = body.value("workspace", "");
    auto ts = create_terminal(workspace);
    if (!ts) {
        res.status = 500;
        res.set_content(R"({"error":"failed to create terminal"})", "application/json");
        return;
    }
    res.set_content(nlohmann::json({{"id", ts->id}, {"workspace", workspace}}).dump(), "application/json");
});

// ── Terminal: destroy ──
svr.Delete("/terminal/([a-f0-9]+)", [](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    destroy_terminal(id);
    res.set_content(R"({"ok":true})", "application/json");
});

// ── Terminal: input (send keystrokes to PTY) ──
svr.Post("/terminal/([a-f0-9]+)/input", [](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); } catch (...) {
        res.status = 400; res.set_content(R"({"error":"invalid JSON"})", "application/json"); return;
    }
    std::string data = body.value("data", "");
    auto ts = find_terminal(id);
    if (!ts) { res.status = 404; res.set_content(R"({"error":"terminal not found"})", "application/json"); return; }
    if (ts->master_fd >= 0 && ts->alive) {
        write(ts->master_fd, data.c_str(), data.size());
    }
    res.set_content(R"({"ok":true})", "application/json");
});

// ── Terminal: output stream (long-poll read from PTY) ──
svr.Get("/terminal/([a-f0-9]+)/stream", [](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto ts = find_terminal(id);
    if (!ts) { res.status = 404; res.set_content(R"({"error":"terminal not found"})", "application/json"); return; }
    // Non-blocking read from PTY master, return available data
    std::string output;
    char buf[4096];
    // Set non-blocking
    int flags = fcntl(ts->master_fd, F_GETFL, 0);
    fcntl(ts->master_fd, F_SETFL, flags | O_NONBLOCK);
    while (true) {
        ssize_t n = read(ts->master_fd, buf, sizeof(buf));
        if (n <= 0) break;
        output.append(buf, n);
    }
    // Restore blocking
    fcntl(ts->master_fd, F_SETFL, flags);
    res.set_header("Content-Type", "text/plain; charset=utf-8");
    res.set_content(output, "text/plain");
});

// ── Get last active session (with history) for client auto-restore ──
svr.Get("/session/last", [](const httplib::Request&, httplib::Response& res) {
    std::string sid = qcode::session::get_last_active_session();
    if (sid.empty()) {
        res.set_content(R"({"id":""})", "application/json");
        return;
    }
    nlohmann::json info = {{"id", sid}};
    for (const auto& s : qcode::session::list_sessions_full()) {
        if (s.id == sid) {
            info["title"] = s.title;
            info["workspace"] = s.workspace;
            info["provider"] = s.provider;
            info["model"] = s.model;
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

// ── Get session info ──
svr.Get("/session/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    auto list = qcode::session::list_sessions_full();
    for (const auto& s : list) {
        if (s.id == sid) {
            res.set_content(nlohmann::json({{"id", s.id}, {"title", s.title},
                {"workspace", s.workspace}, {"provider", s.provider}, {"model", s.model}}).dump(), "application/json");
            return;
        }
    }
    res.status = 404;
    res.set_content(R"({"error":"session not found"})", "application/json");
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
            if (session->ctx.abort_flag) {
                session->ctx.abort_flag->store(true);
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

// ── Compact session ──
svr.Post("/session/([^/]+)/compact", [providers_list](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }

    nlohmann::json body;
    try {
        if (!req.body.empty()) {
            body = nlohmann::json::parse(req.body);
        }
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid JSON"})", "application/json");
        return;
    }

    int keep = body.value("keep", 5);

    // Load conversation messages from DB
    auto snapshot = qcode::session::load_session_history_parsed(sid);
    if (snapshot.size() <= 2) {
        res.status = 400;
        res.set_content(R"({"error":"Nothing to compact: conversation is too short."})", "application/json");
        return;
    }

    // Get session info to know which provider/model to use for compaction
    std::string provider_id = "";
    std::string model_id = "";
    for (const auto& s : qcode::session::list_sessions_full()) {
        if (s.id == sid) {
            provider_id = s.provider;
            model_id = s.model;
            break;
        }
    }

    if (provider_id.empty() || model_id.empty()) {
        // Fallback to default provider/model
        provider_id = (*providers_list)[0].id;
        model_id = (*providers_list)[0].models[0].id;
    }

    // Find the ProviderInfo
    int sp = -1;
    for (int i = 0; i < static_cast<int>(providers_list->size()); ++i) {
        if ((*providers_list)[i].id == provider_id || (*providers_list)[i].name == provider_id) {
            sp = i;
            break;
        }
    }
    if (sp == -1) sp = 0;

    int sm = -1;
    for (int i = 0; i < static_cast<int>((*providers_list)[sp].models.size()); ++i) {
        if ((*providers_list)[sp].models[i].id == model_id || (*providers_list)[sp].models[i].name == model_id) {
            sm = i;
            break;
        }
    }
    if (sm == -1) sm = 0;

    const auto& sel = (*providers_list)[sp];

    // Format the transcript for compaction
    std::ostringstream transcript;
    for (const auto& m : snapshot) {
        std::string role_str;
        if (m.role == qcode::kMessageRoleUser) role_str = "User";
        else if (m.role == qcode::kMessageRoleAssistant) role_str = "Assistant";
        else if (m.role == qcode::kMessageRoleSystem) role_str = "System";
        else role_str = "Message";
        std::string text;
        for (const auto& part : m.content) {
            if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                text += tp->text + "\n";
            } else if (const auto* tcp = std::get_if<qcode::ToolCallContentPart>(&part)) {
                text += "[Tool call: " + tcp->tool_name + "]\n";
            } else if (std::holds_alternative<qcode::ToolResultContentPart>(part)) {
                text += "[Tool result]\n";
            } else if (const auto* rcp = std::get_if<qcode::ReasoningContentPart>(&part)) {
                if (!rcp->text.empty()) text += "[Reasoning: " + rcp->text + "]\n";
            }
        }
        transcript << role_str << ": " << text << "\n";
    }

    const std::string compaction_instruction =
        "Tools are available when needed, including bash for bounded read-only "
        "inspection. Do not modify files or create notes while generating the "
        "handoff summary.\n\n"
        "Summarize the following conversation into a concise handoff packet so a "
        "fresh session can take over. Preserve the next actionable task, verified "
        "evidence, blockers, and concise facts about the code, APIs, data "
        "structures, files, and user preferences that matter for the request.\n\n"
        "Keep only task state and concise facts.\n\n"
        "Output only:\n"
        "## Tasks\n"
        "## Systems\n\n"
        "Use tools only when they improve summary accuracy; otherwise answer directly.";

    qcode::providers::register_authenticated_providers();
    qcode::providers::ProviderOptions provider_options;
    provider_options.base_url = sel.api_url;
    provider_options.api_key = sel.api_key;
    provider_options.headers = sel.headers;
    provider_options.protocol = sel.protocol;
    provider_options.project_id = sel.project_id;
    auto resolution = qcode::providers::ProviderRegistry::instance().resolve(
        sel.id, provider_options);
    if (!resolution.ok()) {
        res.status = 500;
        nlohmann::json err_j; err_j["error"] = "Compaction failed: " + resolution.error; res.set_content(err_j.dump(), "application/json");
        return;
    }

    qcode::Client client = std::move(resolution.client);

    qcode::GenerateOptions opts;
    opts.model = (*providers_list)[sp].models[sm].id;
    opts.system = compaction_instruction;
    opts.messages = {qcode::Message::user(transcript.str())};

    qcode::GenerateResult gen_res = client.generate_text(opts);
    if (!gen_res.is_success() || (gen_res.error && !gen_res.error->empty())) {
        res.status = 500;
        std::string err = gen_res.error && !gen_res.error->empty() ? *gen_res.error : gen_res.error_message();
        nlohmann::json err_j; err_j["error"] = "Compaction failed: " + err; res.set_content(err_j.dump(), "application/json");
        return;
    }

    std::string summary = gen_res.text;
    if (summary.empty()) {
        res.status = 500;
        res.set_content(R"({"error":"Compaction failed: empty summary from model."})", "application/json");
        return;
    }

    std::string notes_root = qcode::get_notes_root();
    std::error_code ec;
    std::filesystem::create_directories(
        notes_root + "/scratchpad/task/qcode-tui/active", ec);
    std::string todo_path = notes_root +
                            "/scratchpad/task/qcode-tui/active/todo-" + sid + ".md";
    bool wrote = false;
    if (!ec) {
        std::ofstream out(todo_path);
        if (out) {
            out << "# qcode compacted handoff\n\n" << summary << "\n";
            wrote = true;
        }
    }

    std::string summary_body =
        "This conversation was compacted into a handoff packet" +
        (wrote ? (" written to: " + todo_path) : "") + ".\n\n" + summary;

    std::string note = "Conversation compacted: " + std::to_string(snapshot.size()) +
                       " messages -> handoff packet";
    note += wrote ? ("\nTodo file: " + todo_path) : " (todo file write failed)";

    // Replace history with the handoff summary + recent keep-tail messages.
    // Older turns are dropped so the message list and model context reset.
    qcode::Messages new_messages;
    new_messages.push_back(qcode::Message::system(note));
    new_messages.push_back(qcode::Message::user(summary_body));

    auto is_prior_compaction_msg = [](const qcode::Message& m) {
        if (m.role == qcode::kMessageRoleSystem) {
            for (const auto& part : m.content) {
                if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                    if (tp->text.find("Conversation compacted:") !=
                        std::string::npos) {
                        return true;
                    }
                }
            }
        }
        if (m.role == qcode::kMessageRoleUser) {
            for (const auto& part : m.content) {
                if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                    if (tp->text.find(
                            "This conversation was compacted into a "
                            "handoff packet") != std::string::npos) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    size_t keep_start = (snapshot.size() > static_cast<size_t>(keep))
                            ? snapshot.size() - static_cast<size_t>(keep)
                            : 0;
    for (size_t i = keep_start; i < snapshot.size(); ++i) {
        if (is_prior_compaction_msg(snapshot[i])) continue;
        new_messages.push_back(snapshot[i]);
    }

    // Overwrite history in SQLite database to persist the compact set
    qcode::session::overwrite_session_history(sid, new_messages);

    nlohmann::json res_j;
    res_j["status"] = "success";
    res_j["summary"] = summary;
    res_j["todo_path"] = todo_path;
    res_j["wrote"] = wrote;
    res_j["original_size"] = snapshot.size();
    res_j["keep"] = keep;
    res.set_content(res_j.dump(), "application/json");
});

// ── Get aggregate session statistics ──
svr.Get("/session/([^/]+)/stats", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    // Pull live counters from an in-memory generation session, if present.
    // Token totals are persisted per-turn in the DB (the cumulative source of
    // truth), so pass 0 for the live token args; only the tool-call counters
    // (reconstructed from stored messages for completed turns + in-flight ctx
    // for the current turn) need the live values.
    int live_tool_calls = 0;
    double live_tool_time_ms = 0.0;
    {
        std::lock_guard<std::mutex> lock(g_sessions_mutex);
        auto it = g_sessions.find(sid);
        if (it != g_sessions.end()) {
            live_tool_calls = it->second->ctx.tool_call_count;
            live_tool_time_ms = it->second->ctx.total_tool_time_ms;
        }
    }
    auto st = qcode::session::get_session_stats(sid, live_tool_calls, live_tool_time_ms,
                                             0, 0, 0);
    nlohmann::json j = {
        {"id", st.id}, {"title", st.title}, {"workspace", st.workspace},
        {"provider", st.provider}, {"model", st.model},
        {"created_at", st.created_at}, {"message_count", st.message_count},
        {"user_messages", st.user_messages}, {"assistant_messages", st.assistant_messages},
        {"tool_calls", st.tool_calls}, {"prompt_tokens", st.prompt_tokens},
        {"completion_tokens", st.completion_tokens}, {"total_tokens", st.total_tokens},
        {"total_tool_time_ms", st.total_tool_time_ms}
    };
    res.set_content(j.dump(2), "application/json");
});

// ── Get git working-tree status for a session's workspace ──
// Returns the unified diff of all modified/new files plus a file list with
// change type + insertion/deletion counts.
svr.Get("/session/([^/]+)/files", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    std::string ws = qcode::session::get_session_workspace(sid);
    if (!ws.empty()) ws = expand_tilde(ws);
    if (ws.empty()) {
        // Fall back to the server's current working directory.
        char cwd[4096] = {0};
        if (getcwd(cwd, sizeof(cwd))) ws = cwd;
    }

    nlohmann::json j;
    j["workspace"] = ws;
    j["is_git_repo"] = false;
    j["files"] = nlohmann::json::array();
    j["diff"] = "";

    if (ws.empty()) {
        res.set_content(j.dump(2), "application/json");
        return;
    }

    // Detect a git repository (walk up to find .git).
    bool is_git = false;
    std::string probe = ws;
    for (int i = 0; i < 64; i++) {
        std::string dotgit = probe + "/.git";
        struct stat st;
        if (stat(dotgit.c_str(), &st) == 0) { is_git = true; break; }
        auto pos = probe.find_last_of('/');
        if (pos == std::string::npos) break;
        probe = probe.substr(0, pos);
    }
    j["is_git_repo"] = is_git;

    if (is_git) {
        // Porcelain status: type, insertions, deletions, file path.
        std::string status_cmd = "cd " + shell_quote(ws) +
            " && git -c core.quotepath=false status --porcelain=v1 --branch 2>/dev/null";
        std::string status_out = exec_capture(status_cmd);
        int insertions = 0, deletions = 0, untracked = 0, modified = 0, staged = 0;
        for (const auto& line : split_lines(status_out)) {
            if (line.empty()) continue;
            // Skip the "## branch..." header line.
            if (line.rfind("## ", 0) == 0) continue;
            std::string xy = line.substr(0, 2);
            std::string path = line.substr(3);
            // Handle renamed "R  old -> new".
            std::string fname = path;
            auto arrow = path.find(" -> ");
            if (arrow != std::string::npos) fname = path.substr(arrow + 4);
            std::string type = "modified";
            if (xy[0] == '?' && xy[1] == '?') { type = "untracked"; untracked++; }
            else if (xy[0] == 'A' || xy[1] == 'A') { type = "added"; staged++; }
            else if (xy[0] == 'M' || xy[1] == 'M') { type = "modified"; modified++; }
            else if (xy[0] == 'D' || xy[1] == 'D') { type = "deleted"; }
            else if (xy[0] == 'R') { type = "renamed"; }
            nlohmann::json fobj = {{"path", fname}, {"type", type},
                                   {"x", std::string(1, xy[0])}, {"y", std::string(1, xy[1])}};
            j["files"].push_back(fobj);
        }

        // Per-file insertion/deletion counts (ignores chmod noise).
        std::string numstat_cmd = "cd " + shell_quote(ws) +
            " && git --no-pager add -N . >/dev/null 2>&1; "
            "git --no-pager diff HEAD --numstat -- . 2>/dev/null";
        std::map<std::string, std::pair<int,int>> per_file;
        for (const auto& line : split_lines(exec_capture(numstat_cmd))) {
            if (line.empty()) continue;
            // Format: "<add>\t<del>\t<path>" (binary shows '-' for counts).
            auto t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            auto t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            std::string add_s = line.substr(0, t1);
            std::string del_s = line.substr(t1 + 1, t2 - t1 - 1);
            std::string fpath = line.substr(t2 + 1);
            int a = (add_s == "-" || add_s.empty()) ? 0 : std::atoi(add_s.c_str());
            int d = (del_s == "-" || del_s.empty()) ? 0 : std::atoi(del_s.c_str());
            per_file[fpath] = {a, d};
            insertions += a;
            deletions += d;
        }

        // Attach counts to the file list.
        for (auto& fobj : j["files"]) {
            std::string fp = fobj.value("path", "");
            auto it = per_file.find(fp);
            if (it != per_file.end()) {
                fobj["insertions"] = it->second.first;
                fobj["deletions"] = it->second.second;
            }
        }

        // Unified diff of all changes (tracked + untracked), with stat summary.
        // Split into per-file sections and drop sections that have no real
        // content (pure chmod / mode-only changes) so the diff stays useful.
        std::string diff_cmd = "cd " + shell_quote(ws) +
            " && git --no-pager add -N . >/dev/null 2>&1; "
            "git --no-pager diff HEAD -- . 2>/dev/null";
        std::string raw_diff = exec_capture(diff_cmd);

        std::vector<std::vector<std::string>> sections;
        std::vector<std::string> cur;
        for (const auto& line : split_lines(raw_diff)) {
            if (line.rfind("diff --git ", 0) == 0) {
                if (!cur.empty()) sections.push_back(cur);
                cur.clear();
            }
            // Skip chmod-only noise lines entirely.
            if (line.rfind("old mode ", 0) == 0) continue;
            if (line.rfind("new mode ", 0) == 0) continue;
            if (line.rfind("new file mode ", 0) == 0) continue;
            if (line.rfind("deleted file mode ", 0) == 0) continue;
            cur.push_back(line);
        }
        if (!cur.empty()) sections.push_back(cur);

        std::string filtered_diff;
        for (const auto& sec : sections) {
            // A section is "real" if it has a hunk header (@@) or a +/- line.
            bool has_content = false;
            for (const auto& l : sec) {
                if (l.rfind("@@", 0) == 0) { has_content = true; break; }
                if (l.rfind("+", 0) == 0 && l.rfind("+++", 0) != 0) { has_content = true; break; }
                if (l.rfind("-", 0) == 0 && l.rfind("---", 0) != 0) { has_content = true; break; }
            }
            if (!has_content) continue;  // skip mode-only file sections
            for (const auto& l : sec) filtered_diff += l + "\n";
        }
        j["diff"] = filtered_diff;
        j["insertions"] = insertions;
        j["deletions"] = deletions;
        j["untracked"] = untracked;
        j["modified"] = modified;
        j["staged"] = staged;
    } else {
        j["diff"] = "";
    }

    res.set_content(j.dump(2), "application/json");
});

// ── Workspace filesystem: list directory ──
// GET /session/:id/fs/list?path=relative/dir
svr.Get("/session/([^/]+)/fs/list", [](const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    std::string ws = resolve_session_workspace(sid);
    std::string rel = req.has_param("path") ? req.get_param_value("path") : "";
    // Strip leading "./"
    while (rel.rfind("./", 0) == 0) rel = rel.substr(2);
    if (rel == ".") rel.clear();

    std::string abs, rel_norm;
    std::string err = resolve_workspace_path(ws, rel, abs, rel_norm);
    if (!err.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json({{"error", err}}).dump(), "application/json");
        return;
    }

    nlohmann::json j;
    j["workspace"] = ws;
    j["path"] = rel_norm;
    j["entries"] = nlohmann::json::array();

    std::error_code ec;
    if (!fs::exists(abs, ec) || !fs::is_directory(abs, ec)) {
        res.status = 404;
        res.set_content(nlohmann::json({{"error", "not a directory"}, {"path", rel_norm}}).dump(),
                        "application/json");
        return;
    }

    std::vector<nlohmann::json> dirs, files;
    for (const auto& entry : fs::directory_iterator(abs, ec)) {
        if (ec) break;
        const auto& p = entry.path();
        std::string name = p.filename().string();
        if (name.empty() || name[0] == '.') continue;  // skip hidden
        bool is_dir = entry.is_directory(ec);
        nlohmann::json e = {
            {"name", name},
            {"type", is_dir ? "dir" : "file"},
            {"path", rel_norm.empty() ? name : (rel_norm + "/" + name)}
        };
        if (!is_dir) {
            auto sz = entry.file_size(ec);
            if (!ec) e["size"] = static_cast<std::uint64_t>(sz);
        }
        if (is_dir) dirs.push_back(std::move(e));
        else files.push_back(std::move(e));
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const nlohmann::json& a, const nlohmann::json& b) {
                  return a["name"].get<std::string>() < b["name"].get<std::string>();
              });
    std::sort(files.begin(), files.end(),
              [](const nlohmann::json& a, const nlohmann::json& b) {
                  return a["name"].get<std::string>() < b["name"].get<std::string>();
              });
    for (auto& e : dirs) j["entries"].push_back(std::move(e));
    for (auto& e : files) j["entries"].push_back(std::move(e));
    res.set_content(j.dump(2), "application/json");
});

// ── Workspace filesystem: read file ──
// GET /session/:id/fs/read?path=relative/file
svr.Get("/session/([^/]+)/fs/read", [](const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    if (!req.has_param("path") || req.get_param_value("path").empty()) {
        res.status = 400;
        res.set_content(R"({"error":"path required"})", "application/json");
        return;
    }
    std::string rel = req.get_param_value("path");
    while (rel.rfind("./", 0) == 0) rel = rel.substr(2);

    std::string ws = resolve_session_workspace(sid);
    std::string abs, rel_norm;
    std::string err = resolve_workspace_path(ws, rel, abs, rel_norm);
    if (!err.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json({{"error", err}}).dump(), "application/json");
        return;
    }

    std::error_code ec;
    if (!fs::exists(abs, ec) || !fs::is_regular_file(abs, ec)) {
        res.status = 404;
        res.set_content(nlohmann::json({{"error", "not a file"}, {"path", rel_norm}}).dump(),
                        "application/json");
        return;
    }
    auto sz = fs::file_size(abs, ec);
    if (ec) {
        res.status = 500;
        res.set_content(R"({"error":"stat failed"})", "application/json");
        return;
    }
    if (sz > kMaxFsFileBytes) {
        res.status = 413;
        res.set_content(nlohmann::json({
            {"error", "file too large"},
            {"size", static_cast<std::uint64_t>(sz)},
            {"max", static_cast<std::uint64_t>(kMaxFsFileBytes)}
        }).dump(), "application/json");
        return;
    }

    std::ifstream in(abs, std::ios::binary);
    if (!in) {
        res.status = 500;
        res.set_content(R"({"error":"open failed"})", "application/json");
        return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    if (looks_binary(content)) {
        res.status = 415;
        res.set_content(nlohmann::json({
            {"error", "binary file"},
            {"path", rel_norm},
            {"size", static_cast<std::uint64_t>(sz)},
            {"is_binary", true}
        }).dump(), "application/json");
        return;
    }

    nlohmann::json j = {
        {"workspace", ws},
        {"path", rel_norm},
        {"size", static_cast<std::uint64_t>(sz)},
        {"content", content}
    };
    res.set_content(j.dump(2), "application/json");
});


// ── Workspace filesystem: raw file stream ──
// GET /session/:id/fs/raw?path=relative/file
svr.Get("/session/([^/]+)/fs/raw", [](const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content("invalid session_id", "text/plain");
        return;
    }
    if (!req.has_param("path") || req.get_param_value("path").empty()) {
        res.status = 400;
        res.set_content("path required", "text/plain");
        return;
    }
    std::string rel = req.get_param_value("path");
    while (rel.rfind("./", 0) == 0) rel = rel.substr(2);

    std::string ws = resolve_session_workspace(sid);
    std::string abs, rel_norm;
    std::string err = resolve_workspace_path(ws, rel, abs, rel_norm);
    if (!err.empty()) {
        res.status = 400;
        res.set_content(err, "text/plain");
        return;
    }

    std::error_code ec;
    if (!fs::exists(abs, ec) || !fs::is_regular_file(abs, ec)) {
        res.status = 404;
        res.set_content("not a file", "text/plain");
        return;
    }

    std::ifstream in(abs, std::ios::binary);
    if (!in) {
        res.status = 500;
        res.set_content("open failed", "text/plain");
        return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    std::string mime = get_mime_type(rel_norm);
    res.set_content(content, mime);
});

// ── Workspace filesystem: write file ──
// PUT /session/:id/fs/write  body: { "path": "rel/file", "content": "..." }
svr.Put("/session/([^/]+)/fs/write", [](const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid json"})", "application/json");
        return;
    }
    if (!body.contains("path") || !body["path"].is_string() ||
        body["path"].get<std::string>().empty()) {
        res.status = 400;
        res.set_content(R"({"error":"path required"})", "application/json");
        return;
    }
    if (!body.contains("content") || !body["content"].is_string()) {
        res.status = 400;
        res.set_content(R"({"error":"content required string"})", "application/json");
        return;
    }
    std::string rel = body["path"].get<std::string>();
    std::string content = body["content"].get<std::string>();
    while (rel.rfind("./", 0) == 0) rel = rel.substr(2);
    if (content.size() > kMaxFsFileBytes) {
        res.status = 413;
        res.set_content(nlohmann::json({
            {"error", "content too large"},
            {"max", static_cast<std::uint64_t>(kMaxFsFileBytes)}
        }).dump(), "application/json");
        return;
    }

    std::string ws = resolve_session_workspace(sid);
    std::string abs, rel_norm;
    std::string err = resolve_workspace_path(ws, rel, abs, rel_norm);
    if (!err.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json({{"error", err}}).dump(), "application/json");
        return;
    }
    if (rel_norm.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"cannot write workspace root"})", "application/json");
        return;
    }

    std::error_code ec;
    // Refuse to overwrite directories.
    if (fs::exists(abs, ec) && fs::is_directory(abs, ec)) {
        res.status = 400;
        res.set_content(R"({"error":"path is a directory"})", "application/json");
        return;
    }
    // Create parent dirs if needed.
    fs::path parent = fs::path(abs).parent_path();
    if (!parent.empty() && !fs::exists(parent, ec)) {
        fs::create_directories(parent, ec);
        if (ec) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", "mkdir failed"},
                                            {"detail", ec.message()}}).dump(),
                            "application/json");
            return;
        }
    }

    std::ofstream out(abs, std::ios::binary | std::ios::trunc);
    if (!out) {
        res.status = 500;
        res.set_content(R"({"error":"open for write failed"})", "application/json");
        return;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (!out) {
        res.status = 500;
        res.set_content(R"({"error":"write failed"})", "application/json");
        return;
    }

    nlohmann::json j = {
        {"ok", true},
        {"workspace", ws},
        {"path", rel_norm},
        {"size", content.size()}
    };
    res.set_content(j.dump(2), "application/json");
});

// ── Workspace filesystem: lookup path → absolute+relative ──
// GET /session/:id/fs/exists?path=relative
svr.Get("/session/([^/]+)/fs/exists", [](const httplib::Request& req, httplib::Response& res) {
    std::string sid = url_decode(req.matches[1]);
    if (!qcode::session::is_valid_session_id(sid)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid session_id"})", "application/json");
        return;
    }
    if (!req.has_param("path") || req.get_param_value("path").empty()) {
        res.status = 400;
        res.set_content(R"({"error":"path required"})", "application/json");
        return;
    }
    std::string rel = req.get_param_value("path");
    while (rel.rfind("./", 0) == 0) rel = rel.substr(2);
    if (rel == ".") rel.clear();

    std::string ws = resolve_session_workspace(sid);
    std::string abs, rel_norm;
    std::string err = resolve_workspace_path(ws, rel, abs, rel_norm);
    if (!err.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json({{"error", err}}).dump(), "application/json");
        return;
    }

    std::error_code ec;
    bool exists = fs::exists(abs, ec);
    bool is_dir = exists && fs::is_directory(abs, ec);
    nlohmann::json j = {
        {"ok", true},
        {"workspace", ws},
        {"path", rel_norm},
        {"exists", exists},
        {"type", exists ? (is_dir ? "dir" : "file") : "none"},
        {"full_path", abs}
    };
    if (exists && !is_dir) {
        auto sz = fs::file_size(abs, ec);
        if (!ec) j["size"] = static_cast<std::uint64_t>(sz);
    }
    res.set_content(j.dump(2), "application/json");
});


  // ── Study Buddy routes ────────────────────────────────────────────
  svr.Get("/study/courses", [](const httplib::Request&, httplib::Response& res) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : qcode::study::list_courses()) {
      arr.push_back({{"id", c.id},
                     {"title", c.title},
                     {"root_path", c.root_path},
                     {"created_at", c.created_at}});
    }
    res.set_content(arr.dump(2), "application/json");
  });

  svr.Post("/study/courses/ingest", [](const httplib::Request& req,
                                       httplib::Response& res) {
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid json"})", "application/json");
      return;
    }
    const std::string root =
        body.value("root_path", "/sdcard/Documents/notes");
    const std::string title = body.value("course_title", "");
    const std::string course_id = qcode::study::ingest_vault(root, title);
    if (course_id.empty()) {
      res.status = 500;
      res.set_content(R"({"error":"ingest failed"})", "application/json");
      return;
    }
    auto course = qcode::study::get_course(course_id);
    nlohmann::json chapters = nlohmann::json::array();
    for (const auto& ch : qcode::study::list_chapters(course_id)) {
      chapters.push_back({{"id", ch.id},
                          {"slug", ch.slug},
                          {"title", ch.title},
                          {"path", ch.path},
                          {"order_index", ch.order_index},
                          {"mastery", ch.mastery}});
    }
    nlohmann::json out = {
        {"ok", true},
        {"course",
         {{"id", course_id},
          {"title", course ? course->title : title},
          {"root_path", course ? course->root_path : root}}},
        {"chapters", chapters},
    };
    res.set_content(out.dump(2), "application/json");
  });

  svr.Get("/study/courses/([^/]+)/chapters",
          [](const httplib::Request& req, httplib::Response& res) {
            const std::string course_id = req.matches[1];
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& ch : qcode::study::list_chapters(course_id)) {
              arr.push_back({{"id", ch.id},
                             {"course_id", ch.course_id},
                             {"slug", ch.slug},
                             {"title", ch.title},
                             {"path", ch.path},
                             {"order_index", ch.order_index},
                             {"mastery", ch.mastery}});
            }
            res.set_content(arr.dump(2), "application/json");
          });

  svr.Get("/study/courses/([^/]+)/next",
          [](const httplib::Request& req, httplib::Response& res) {
            const std::string course_id = req.matches[1];
            int limit = 8;
            if (req.has_param("limit")) {
              try {
                limit = std::stoi(req.get_param_value("limit"));
              } catch (...) {
              }
            }
            auto plan = qcode::study::next_weak_topics(course_id, limit);
            nlohmann::json topics = nlohmann::json::array();
            for (size_t i = 0; i < plan.weak_topic_ids.size(); ++i) {
              topics.push_back({{"id", plan.weak_topic_ids[i]},
                                {"name", plan.weak_topic_names[i]}});
            }
            nlohmann::json out = {{"topics", topics},
                                  {"suggested_count", plan.suggested_count}};
            res.set_content(out.dump(2), "application/json");
          });

  svr.Post("/study/chapters/([^/]+)/prepare",
           [providers_list](const httplib::Request& req,
                            httplib::Response& res) {
             const std::string chapter_id = req.matches[1];
             nlohmann::json body;
             try {
               body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
             } catch (...) {
               res.status = 400;
               res.set_content(R"({"error":"invalid json"})",
                               "application/json");
               return;
             }
             std::string provider = body.value("provider", "");
             std::string model = body.value("model", "");
             if ((provider.empty() || model.empty()) && providers_list &&
                 !providers_list->empty()) {
               const auto& p = (*providers_list)[0];
               if (provider.empty()) provider = p.id;
               if (model.empty() && !p.models.empty()) model = p.models[0].id;
             }
             if (provider.empty() || model.empty()) {
               res.status = 400;
               res.set_content(R"({"error":"provider/model required"})",
                               "application/json");
               return;
             }
             auto prepared = qcode::study::prepare_chapter_with_llm(
                 chapter_id, provider, model,
                 providers_list ? *providers_list
                                : std::vector<qcode::ProviderInfo>{});
             if (!prepared.ok) {
               res.status = 500;
               res.set_content(
                   nlohmann::json({{"error", prepared.error}}).dump(),
                   "application/json");
               return;
             }
             nlohmann::json out = {{"ok", true},
                                   {"summary_md", prepared.summary_md},
                                   {"quiz", prepared.quiz_json},
                                   {"question_count",
                                    prepared.quiz_json.is_array()
                                        ? prepared.quiz_json.size()
                                        : 0}};
             res.set_content(out.dump(2), "application/json");
           });

  svr.Get("/study/chapters/([^/]+)/quiz",
          [](const httplib::Request& req, httplib::Response& res) {
            const std::string chapter_id = req.matches[1];
            auto chapter = qcode::study::get_chapter(chapter_id);
            if (!chapter) {
              res.status = 404;
              res.set_content(R"({"error":"chapter not found"})",
                              "application/json");
              return;
            }
            std::vector<std::string> topic_filter;
            if (req.has_param("topics")) {
              std::stringstream ss(req.get_param_value("topics"));
              std::string part;
              while (std::getline(ss, part, ',')) {
                if (!part.empty()) topic_filter.push_back(part);
              }
            }
            int limit = 20;
            if (req.has_param("limit")) {
              try {
                limit = std::stoi(req.get_param_value("limit"));
              } catch (...) {
              }
            }
            auto questions = topic_filter.empty()
                                 ? qcode::study::list_chapter_questions(chapter_id)
                                 : qcode::study::list_questions_for_topics(
                                       chapter_id, topic_filter, limit);
            if (static_cast<int>(questions.size()) > limit) {
              questions.resize(limit);
            }
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& q : questions) {
              nlohmann::json choices = nlohmann::json::array();
              try {
                choices = nlohmann::json::parse(
                    q.choices_json.empty() ? "[]" : q.choices_json);
              } catch (...) {
              }
              arr.push_back(
                  {{"id", q.id},
                   {"type", qcode::study::question_type_to_string(q.type)},
                   {"prompt_html", q.prompt_html},
                   {"choices", choices},
                   {"topic", q.topic_name},
                   {"topic_id", q.topic_id},
                   {"difficulty", q.difficulty}});
            }
            nlohmann::json out = {{"chapter_id", chapter_id},
                                  {"title", chapter->title},
                                  {"path", chapter->path},
                                  {"questions", arr}};
            res.set_content(out.dump(2), "application/json");
          });

  svr.Post("/study/quiz/submit",
           [providers_list](const httplib::Request& req,
                            httplib::Response& res) {
             nlohmann::json body;
             try {
               body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
             } catch (...) {
               res.status = 400;
               res.set_content(R"({"error":"invalid json"})",
                               "application/json");
               return;
             }
             std::string provider = body.value("provider", "");
             std::string model = body.value("model", "");
             if ((provider.empty() || model.empty()) && providers_list &&
                 !providers_list->empty()) {
               const auto& p = (*providers_list)[0];
               if (provider.empty()) provider = p.id;
               if (model.empty() && !p.models.empty()) model = p.models[0].id;
             }
             nlohmann::json answers = body.contains("answers") &&
                                              body["answers"].is_array()
                                          ? body["answers"]
                                          : nlohmann::json::array();
             if (answers.empty() && body.contains("question_id")) {
               answers.push_back(
                   {{"question_id", body.value("question_id", "")},
                    {"answer", body.value("answer",
                                          body.value("student_answer", ""))}});
             }
             nlohmann::json results = nlohmann::json::array();
             for (const auto& a : answers) {
               const std::string qid = a.value("question_id", "");
               const std::string ans = a.value(
                   "answer", a.value("student_answer", std::string{}));
               auto q = qcode::study::get_question(qid);
               if (!q) {
                 results.push_back({{"question_id", qid},
                                    {"error", "question not found"}});
                 continue;
               }
               auto graded = qcode::study::grade_answer(
                   *q, ans, provider, model,
                   providers_list ? *providers_list
                                  : std::vector<qcode::ProviderInfo>{});
               results.push_back({{"question_id", graded.question_id},
                                  {"correct", graded.correct},
                                  {"score", graded.score},
                                  {"feedback", graded.feedback}});
             }
             res.set_content(
                 nlohmann::json({{"results", results}}).dump(2),
                 "application/json");
           });

}

}  // namespace server
}  // namespace qcode
