#include <ai/file_logger.h>
#include <ai/tui/chat_bus.h>
#include <ai/tui/provider_registry_init.h>
#include <ai/tui/config.h>
#include <ai/tui/db.h>
#include <ai/tui/state.h>
#include <ai/tui/system_prompt.h>
#include <ai/tui/bus/impl.h>
#include <ai/tui/contract/event.h>
#include <ai/server/json_codec.h>
#include <ai/utils/uuid.h>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace ai::tui::contract;

// ── Globals ──
static std::atomic<bool> g_running{true};
static std::shared_ptr<ai::tui::bus::BusRuntime> g_bus;

void handle_signal(int) { g_running = false; }

// ── Per-generation session state ──
struct GenSession {
    std::string id;
    ai::tui::GenerationContext ctx;
    ai::Messages messages;
    std::vector<nlohmann::json> event_queue;
    std::mutex queue_mutex;
    std::atomic<bool> done{false};
    std::atomic<bool> generation_started{false};
    std::string error;
    std::shared_ptr<std::vector<ai::tui::bus::Subscription>> subs;
};

static std::mutex g_sessions_mutex;
static std::unordered_map<std::string, std::shared_ptr<GenSession>> g_sessions;

// ── Event collector: subscribes to bus and queues events for a session ──
static std::vector<ai::tui::bus::Subscription> subscribe_session(
    ai::tui::bus::BusRuntime& bus,
    std::shared_ptr<GenSession> session)
{
    std::vector<ai::tui::bus::Subscription> subs;

    subs.push_back(bus.subscribe<MessageDelta>(
        [session](const MessageDelta::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = ai::server::message_delta_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ToolCallStarted>(
        [session](const ToolCallStarted::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = ai::server::tool_call_started_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ToolCallCompleted>(
        [session](const ToolCallCompleted::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = ai::server::tool_call_completed_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<SessionStatusChanged>(
        [session](const SessionStatusChanged::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = ai::server::session_status_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ErrorOccurred>(
        [session](const ErrorOccurred::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = ai::server::error_occurred_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    subs.push_back(bus.subscribe<ReasoningDelta>(
        [session](const ReasoningDelta::Payload& p) {
            if (p.session_id != session->ctx.session_id) return;
            auto j = ai::server::reasoning_delta_to_json(p);
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.push_back(std::move(j));
        }
    ));

    return subs;
}

int main(int argc, char* argv[]) {
    ai::install_file_logger("/tmp/qcode-server.log", ai::logger::LogLevel::kLogLevelDebug);
    ai::logger::set_thread_name("server");
    LOG_INFO("QCode server starting...");

    // ── Parse args ──
    int port = 9080;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        if (arg == "-p" && i + 1 < argc) port = std::stoi(argv[++i]);
        if (arg == "--help" || arg == "-h") {
            std::cerr << "Usage: qcode-server [--port PORT]\n";
            return 0;
        }
    }

    // ── Init bus, providers ──
    g_bus = std::make_shared<ai::tui::bus::BusRuntime>();
    register_all_events(*g_bus);
    ai::providers::register_tui_providers();

    // Load providers
    auto providers_list = ai::tui::load_providers_from_config();
    if (providers_list.empty()) {
        LOG_ERROR("No providers configured. Create ~/.config/qcode/providers.json");
        return 1;
    }
    LOG_INFO("Loaded {} provider(s)", providers_list.size());

    std::string default_provider = providers_list[0].id;
    std::string default_model = providers_list[0].models[0].id;

    // Backend service (uses the shared bus)
    ai::tui::BackendService backend(*g_bus, providers_list);

    // ── Signal handling ──
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // ── HTTP server ──
    httplib::Server svr;

    // Serve Web UI static files
    {
        std::string webui_path = std::string(argv[0]);
        auto pos = webui_path.find_last_of("/\\");
        if (pos != std::string::npos) webui_path = webui_path.substr(0, pos);
        webui_path += "/webui";
        if (access(webui_path.c_str(), F_OK) != 0) {
            webui_path = AI_SERVER_WEBUI_DIR;
        }
        if (access(webui_path.c_str(), F_OK) == 0) {
            LOG_INFO("Serving Web UI from {}", webui_path);
            svr.set_mount_point("/", webui_path);
        } else {
            LOG_WARN("Web UI directory not found at {}", webui_path);
        }
    }

    // Health check
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // Provider list
    svr.Get("/providers", [&providers_list](const httplib::Request&, httplib::Response& res) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& p : providers_list) {
            nlohmann::json models = nlohmann::json::array();
            for (const auto& m : p.models) {
                models.push_back({{"id", m.id}, {"name", m.name}});
            }
            j.push_back({{"id", p.id}, {"name", p.name}, {"models", models}});
        }
        res.set_content(j.dump(2), "application/json");
    });

    // ── Generate endpoint (streaming NDJSON) ──
    svr.Post("/generate", [&backend, &providers_list](const httplib::Request& req, httplib::Response& res) {
        // Parse request body
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
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

        std::string provider = body.value("provider", providers_list[0].id);
        std::string model = body.value("model", providers_list[0].models[0].id);
        std::string system_prompt = body.value("system_prompt",
            ai::tui::SystemPrompt::build_default());
        std::string reasoning_mode = body.value("reasoning_mode", "off");

        // ── Resolve or create session for multi-turn ──
        std::string session_id = body.value("session_id", "");
        std::shared_ptr<GenSession> session;

        {
            std::lock_guard<std::mutex> lock(g_sessions_mutex);
            if (!session_id.empty()) {
                auto it = g_sessions.find(session_id);
                if (it != g_sessions.end()) session = it->second;
            }
            if (!session) {
                session = std::make_shared<GenSession>();
                session->id = session_id.empty() ? ai::utils::generate_uuid() : session_id;
                {
                    std::lock_guard<std::mutex> lock2(g_sessions_mutex);
                    g_sessions[session->id] = session;
                }
            }
        }

        // Set up generation context (reasoning mode can change per request)
        session->ctx = ai::tui::GenerationContext{
            .session_id = session->id,
            .reasoning_mode = reasoning_mode
        };

        // Subscribe to bus events for this session (once)
        if (!session->subs) {
            session->subs = std::make_shared<std::vector<ai::tui::bus::Subscription>>(
                subscribe_session(*g_bus, session));
        }

        // Save user message and add to history
        ai::tui::db::save_message(session->id, "User", text);
        session->messages.push_back(ai::Message::user(text));

        // Keep a local copy of messages for the generation thread
        ai::Messages messages = session->messages;

        // ── Set up streaming response ──
        // Generation runs in a background thread. The chunked provider
        // callback polls the event queue and writes events as they arrive,
        // giving true real-time streaming to the client.
        res.set_chunked_content_provider("application/x-ndjson",
            [session, &backend,
             provider, model, system_prompt, messages = std::move(messages),
             reasoning_mode](size_t offset, httplib::DataSink& sink) mutable -> bool
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
                    std::thread gen_thread([session, &backend, provider, model,
                                            system_prompt, messages = std::move(messages)]() {
                        try {
                            backend.run_generation(
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

                // Drain queued events
                std::vector<nlohmann::json> events;
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

                // If generation is done and queue is drained, send final event
                if (session->done && events.empty()) {
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
                    sink.write(chunk.data(), chunk.size());
                    sink.done();
                    return false; // No more data
                }

                // Not done yet - will be called again
                return true;
            }
        );
    });

    // ── Start server ──
    LOG_INFO("Starting HTTP server on port {}...", port);
    std::cout << "QCode server listening on http://0.0.0.0:" << port << "\n";
    std::cout << "  Health:  GET /health\n";
    std::cout << "  Providers: GET /providers\n";
    std::cout << "  Generate:  POST /generate (body: {\"text\":\"...\", \"provider\":\"...\", \"model\":\"...\"})\n";
    std::cout << "    Response is NDJSON stream of events\n";
    svr.listen("0.0.0.0", port);

    LOG_INFO("Server shutting down");
    return 0;
}
