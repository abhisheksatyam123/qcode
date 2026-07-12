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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

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
    std::string assistant_text;  // accumulated (cumulative) assistant reply, for DB persistence
    std::shared_ptr<std::vector<ai::tui::bus::Subscription>> subs;
};

static std::mutex g_sessions_mutex;
static std::unordered_map<std::string, std::shared_ptr<GenSession>> g_sessions;

// ── Terminal PTY manager ──────────────────────────────────────────
struct TerminalSession {
    std::string id;
    int master_fd = -1;
    pid_t child_pid = -1;
    std::string workspace;
    bool alive = true;
};

static std::mutex g_terminal_mutex;
static std::unordered_map<std::string, std::shared_ptr<TerminalSession>> g_terminals;

static std::string generate_terminal_id() {
    static thread_local std::mt19937 eng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<uint64_t> dist;
    uint64_t v = dist(eng);
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return std::string(buf);
}

static std::shared_ptr<TerminalSession> create_terminal(const std::string& workspace) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) return nullptr;
    grantpt(master);
    unlockpt(master);
    char* sname = ptsname(master);
    if (!sname) { close(master); return nullptr; }

    pid_t pid = fork();
    if (pid < 0) { close(master); return nullptr; }

    if (pid == 0) {
        // Child process
        setsid();
        int slave = open(sname, O_RDWR);
        if (slave < 0) _exit(1);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > 2) close(slave);
        close(master);
        if (!workspace.empty()) chdir(workspace.c_str());
        setenv("TERM", "xterm-256color", 1);
        const char* shell = getenv("SHELL");
        if (!shell) shell = "/bin/sh";
        execlp(shell, shell, nullptr);
        _exit(1);
    }

    // Parent: master fd stays open
    auto session = std::make_shared<TerminalSession>();
    session->id = generate_terminal_id();
    session->master_fd = master;
    session->child_pid = pid;
    session->workspace = workspace;
    session->alive = true;

    std::lock_guard<std::mutex> lock(g_terminal_mutex);
    g_terminals[session->id] = session;
    return session;
}

static void destroy_terminal(const std::string& id) {
    std::shared_ptr<TerminalSession> ts;
    {
        std::lock_guard<std::mutex> lock(g_terminal_mutex);
        auto it = g_terminals.find(id);
        if (it == g_terminals.end()) return;
        ts = it->second;
        g_terminals.erase(it);
    }
    if (ts->alive) {
        ts->alive = false;
        if (ts->master_fd >= 0) close(ts->master_fd);
        if (ts->child_pid > 0) {
            kill(ts->child_pid, SIGHUP);
            waitpid(ts->child_pid, nullptr, WNOHANG);
        }
    }
}

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

    subs.push_back(bus.subscribe<TokenUsageUpdated>(
        [session](const TokenUsageUpdated::Payload& p) {
            auto j = ai::server::token_usage_to_json(p);
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

    // Ensure the SQLite schema (sessions + messages) exists. The server owns
    // its DB so all session state is durably persisted, independent of the TUI.
    ai::tui::db::init_database();

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
        bool created_this_request = false;

        {
            std::lock_guard<std::mutex> lock(g_sessions_mutex);
            if (!session_id.empty()) {
                auto it = g_sessions.find(session_id);
                if (it != g_sessions.end()) session = it->second;
            }
            if (!session) {
                session = std::make_shared<GenSession>();
                session->id = session_id.empty()
                    ? ai::tui::db::create_new_session(provider, model)
                    : session_id;
                g_sessions[session->id] = session;
                created_this_request = true;
            }
        }

        // Reset per-turn state so each /generate starts a fresh generation
        // thread (fixes broken multi-turn after the first reply).
        session->generation_started = false;
        session->done = false;
        session->error.clear();
        session->assistant_text.clear();
        if (session->ctx.abort_flag) {
            session->ctx.abort_flag->store(false);
        }
        {
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->event_queue.clear();
        }

        // If this session has prior history in the DB (resumed after a server
        // restart or loaded from another client), restore it so the model keeps
        // full context.
        if (created_this_request && !session_id.empty()) {
            for (const auto& [sender, content] : ai::tui::db::load_session_messages(session_id)) {
                if (sender == "User") session->messages.push_back(ai::Message::user(content));
                else if (sender == "Assistant") session->messages.push_back(ai::Message::assistant(content));
            }
        }

        // Set up generation context (reasoning mode can change per request)
        {
            auto ws = ai::tui::db::get_session_workspace(session->id);
            session->ctx = ai::tui::GenerationContext{
                .session_id = session->id,
                .reasoning_mode = reasoning_mode,
                .workspace = ws
            };
        }

        // Subscribe to bus events for this session (once)
        if (!session->subs) {
            session->subs = std::make_shared<std::vector<ai::tui::bus::Subscription>>(
                subscribe_session(*g_bus, session));
        }

        // Save user message and add to history
        ai::tui::db::set_session_provider_model(session->id, provider, model);
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
                        session->assistant_text = evt.value("text", session->assistant_text);
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
                        ai::tui::db::save_message(session->id, "Assistant", session->assistant_text);
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
                    sink.write(chunk.data(), chunk.size());
                    sink.done();
                    return false; // No more data
                }

                // Not done yet - will be called again
                return true;
            }
        );
    });

    // ── List sessions ──
    svr.Get("/sessions", [](const httplib::Request&, httplib::Response& res) {
        auto list = ai::tui::db::list_sessions_full();
        nlohmann::json j = nlohmann::json::array();
        for (const auto& s : list) {
            j.push_back({{"id", s.id}, {"title", s.title}, {"workspace", s.workspace},
                         {"provider", s.provider}, {"model", s.model}});
        }
        res.set_content(j.dump(2), "application/json");
    });

    // ── Create session ──
    svr.Post("/sessions", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"invalid JSON"})", "application/json");
            return;
        }
        std::string provider = body.value("provider", "");
        std::string model = body.value("model", "");
        std::string workspace = body.value("workspace", "");
        if (provider.empty() || model.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"provider and model required"})", "application/json");
            return;
        }
        auto id = ai::tui::db::create_new_session(provider, model, workspace);
        res.set_content(nlohmann::json({{"id", id}, {"workspace", workspace}}).dump(), "application/json");
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
        if (session_id.empty() || !ai::tui::db::is_valid_session_id(session_id)) {
            res.status = 400;
            res.set_content(R"({"error":"invalid session_id"})", "application/json");
            return;
        }
        if (new_title.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"title is required"})", "application/json");
            return;
        }
        ai::tui::db::rename_session(session_id, new_title);
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
        std::shared_ptr<TerminalSession> ts;
        {
            std::lock_guard<std::mutex> lock(g_terminal_mutex);
            auto it = g_terminals.find(id);
            if (it == g_terminals.end()) { res.status = 404; res.set_content(R"({"error":"terminal not found"})", "application/json"); return; }
            ts = it->second;
        }
        if (ts->master_fd >= 0 && ts->alive) {
            write(ts->master_fd, data.c_str(), data.size());
        }
        res.set_content(R"({"ok":true})", "application/json");
    });

    // ── Terminal: output stream (long-poll read from PTY) ──
    svr.Get("/terminal/([a-f0-9]+)/stream", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        std::shared_ptr<TerminalSession> ts;
        {
            std::lock_guard<std::mutex> lock(g_terminal_mutex);
            auto it = g_terminals.find(id);
            if (it == g_terminals.end()) { res.status = 404; res.set_content(R"({"error":"terminal not found"})", "application/json"); return; }
            ts = it->second;
        }
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

    // ── Get session info ──
    svr.Get("/session/([a-f0-9-]+)", [](const httplib::Request& req, httplib::Response& res) {
        std::string sid = req.matches[1];
        auto list = ai::tui::db::list_sessions_full();
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

    // ── Get session message history ──
    svr.Get("/session/([a-f0-9-]+)/messages", [](const httplib::Request& req, httplib::Response& res) {
        std::string sid = req.matches[1];
        if (!ai::tui::db::is_valid_session_id(sid)) {
            res.status = 400;
            res.set_content(R"({"error":"invalid session_id"})", "application/json");
            return;
        }
        auto hist = ai::tui::db::load_session_messages(sid);
        nlohmann::json j = nlohmann::json::array();
        for (const auto& [sender, content] : hist) {
            j.push_back({{"role", sender}, {"content", content}});
        }
        res.set_content(j.dump(2), "application/json");
    });

    // ── Get last active session (with history) for client auto-restore ──
    svr.Get("/session/last", [](const httplib::Request&, httplib::Response& res) {
        std::string sid = ai::tui::db::get_last_active_session();
        if (sid.empty()) {
            res.set_content(R"({"id":""})", "application/json");
            return;
        }
        nlohmann::json info = {{"id", sid}};
        for (const auto& s : ai::tui::db::list_sessions_full()) {
            if (s.id == sid) {
                info["title"] = s.title;
                info["workspace"] = s.workspace;
                info["provider"] = s.provider;
                info["model"] = s.model;
                break;
            }
        }
        nlohmann::json msgs = nlohmann::json::array();
        for (const auto& [sender, content] : ai::tui::db::load_session_messages(sid)) {
            msgs.push_back({{"role", sender}, {"content", content}});
        }
        info["messages"] = std::move(msgs);
        res.set_content(info.dump(2), "application/json");
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
