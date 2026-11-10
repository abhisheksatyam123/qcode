#include <qcode_app/frontends.h>
#include <qcode/core/file_logger.h>
#include <qcode/core/session_file_logger.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/session/session_title.h>
#include <qcode/tools/task_tool.h>
#include <qcode/config/config.h>
#include <qcode/session/session_store.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>

#include "server_routes.h"
#include "routes/session_runtime.h"

#include <httplib.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <random>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <charconv>
#include <iostream>
#include <system_error>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_shutdown_requested{false};
static std::shared_ptr<qcode::bus::BusRuntime> g_bus;

std::string log_dir() {
    if (const char* d = std::getenv("QCODE_LOG_DIR")) return d;
    if (const char* home = std::getenv("HOME")) {
        std::string p = std::string(home) + "/.qcode/logs";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        if (!ec) return p;
    }
    return "/tmp/qcode-logs";
}

static std::atomic<int> g_shutdown_signal{0};
static int g_crash_fd = -1;

// Runs in signal context: only an atomic store. Logging here (mutex, malloc)
// can deadlock or corrupt the heap if the signal lands inside the logger; the
// shutdown watcher thread reports the signal instead.
void handle_signal(int sig) {
    g_shutdown_signal.store(sig, std::memory_order_relaxed);
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}

// A SIGSEGV/SIGABRT/SIGBUS leaves no core when the system hands cores to apport
// (it skips a new report while an older one is pending), so write the stack
// ourselves: to stderr (journald) and to <log-dir>/qcode-crash.log.
void crash_handler(int sig) {
    static const char kHead[] = "\n*** qcode fatal signal, backtrace:\n";
    void* frames[64];
    const int n = backtrace(frames, 64);
    for (int fd : {2, g_crash_fd}) {
        if (fd < 0) continue;
        (void)!write(fd, kHead, sizeof kHead - 1);
        backtrace_symbols_fd(frames, n, fd);
    }
    signal(sig, SIG_DFL);  // keep the default exit status and any core dump
    raise(sig);
}

void install_crash_handler(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    g_crash_fd = open((dir + "/qcode-crash.log").c_str(),
                      O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    // Handle a stack overflow too: it needs its own stack.
    static char alt_stack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof alt_stack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_handler = crash_handler;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) sigaction(sig, &sa, nullptr);
    void* warm[1];
    backtrace(warm, 1);  // load libgcc now, not inside the handler
}

int qcode::app::run_server(int argc, char* argv[]) {
    constexpr const char* kUsage =
        "Usage: qcode serve [--port PORT] [--host ADDR] [--log-dir DIR]\n"
        "  --host ADDR  address to listen on (default 127.0.0.1). Optional\n"
        "               bearer token via QCODE_SERVER_TOKEN.\n";
    int port = 9080;
    std::string host = "127.0.0.1";
    std::string custom_log_dir;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << kUsage;
            return 0;
        }
        if (arg == "--port" || arg == "-p" || arg == "--log-dir" || arg == "--host") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a value\n" << kUsage;
                return 2;
            }
            const std::string value = argv[++i];
            if (arg == "--log-dir") {
                custom_log_dir = value;
                continue;
            }
            if (arg == "--host") {
                host = value;
                continue;
            }
            // std::stoi throws on junk ("abc") and silently accepts "9080x".
            int parsed = 0;
            const char* end = value.data() + value.size();
            const auto [ptr, ec] = std::from_chars(value.data(), end, parsed);
            if (ec != std::errc{} || ptr != end || parsed < 1 || parsed > 65535) {
                std::cerr << "Error: invalid port '" << value << "' (expected 1-65535)\n";
                return 2;
            }
            port = parsed;
            continue;
        }
        std::cerr << "Unknown option: " << arg << "\n\n" << kUsage;
        return 2;
    }

    // Loopback is the default. Setting QCODE_SERVER_TOKEN gates requests with a bearer token.
    const bool loopback_bind = host == "127.0.0.1" || host == "localhost" || host == "::1";
    const char* token_env = std::getenv("QCODE_SERVER_TOKEN");
    const std::string api_token = token_env ? token_env : "";
    // The token goes into the startup URL unescaped, so keep it to URL-safe characters.
    const bool url_safe = std::all_of(api_token.begin(), api_token.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
    });
    if (!api_token.empty() && !url_safe) {
        std::cerr << "Error: QCODE_SERVER_TOKEN may contain only letters, digits, '-', '.', '_' and '~'.\n";
        return 2;
    }

    // One log file per session: <dir>/qcode-server-<session_id>.log
    const std::string active_log_dir = !custom_log_dir.empty() ? custom_log_dir : log_dir();
    qcode::server::g_session_logger = qcode::install_session_file_logger(
        active_log_dir, "qcode-server", qcode::logger::LogLevel::kLogLevelDebug);
    qcode::logger::set_thread_name("server");
    install_crash_handler(active_log_dir);
    LOG_INFO("QCode server starting... (logs in {})", active_log_dir);

    g_bus = std::make_shared<qcode::bus::BusRuntime>();
    qcode::contract::register_all_events(*g_bus);
    // Subagent transitions go onto the bus for the lead's stream and panel.
    // The observer holds the bus weakly, so it is a no-op after shutdown.
    qcode::TaskTool::set_status_observer(
        [weak = std::weak_ptr<qcode::bus::BusRuntime>(g_bus)](
            const std::string& lead_session_id, const std::string& task_id,
            const std::string& status) {
            if (auto bus = weak.lock()) {
                bus->publish<qcode::contract::TaskStatusChanged>(
                    {lead_session_id, task_id, status});
            }
        });
    qcode::providers::register_authenticated_providers();
    qcode::session::init_database();

    auto providers_list = std::make_shared<std::vector<qcode::ProviderInfo>>(
        qcode::load_providers_from_config());
    qcode::session::seed_model_capabilities(*providers_list);
    if (providers_list->empty()) {
        LOG_ERROR("No providers configured. Add them to {}",
                  qcode::config_path());
        // Logs go to a file, so also tell the terminal user what to do.
        std::cerr << "Error: no providers configured. Add a provider with at least one "
                     "model to " << qcode::config_path() << ".\n"
                     "Its API key can go in the file or come from the environment, "
                     "for example OPENAI_API_KEY.\n";
        return 1;
    }
    LOG_INFO("Loaded {} provider(s)", providers_list->size());

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    httplib::Server svr;
    // Each connection holds a pool worker (CPPHTTPLIB_THREAD_POOL_COUNT) while
    // open, and a turn's NDJSON stream for the whole turn: release idle
    // keep-alive connections quickly so they do not starve real requests.
    svr.set_keep_alive_timeout(2);

    qcode::server::ServerSetupOptions options;
    options.security.loopback_bind = loopback_bind;
    options.security.token = api_token;
    if (!options.security.token.empty()) {
        // The session cookie is random, not the token: a 128-bit value from the OS.
        std::random_device rd;
        std::string cookie;
        for (int i = 0; i < 4; ++i) {
            char buf[9];
            std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(rd()));
            cookie += buf;
        }
        options.security.cookie_value = cookie;
    }
    {
        std::string webui_path = std::string(argv[0]);
        auto pos = webui_path.find_last_of("/\\");
        if (pos != std::string::npos) webui_path = webui_path.substr(0, pos);
        webui_path += "/webui";
        if (access(webui_path.c_str(), F_OK) != 0) {
            webui_path = QCODE_SERVER_WEBUI_DIR;
        }
        if (access(webui_path.c_str(), F_OK) != 0) {
            webui_path = "apps/webui/src";
        }
        options.webui_dir = webui_path;
    }

    qcode::server::setup_server_routes(svr, g_bus, providers_list, options);

    std::thread shutdown_watcher([&svr]() {
        while (!g_shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        LOG_INFO("Caught signal {}, shutting down...", g_shutdown_signal.load());
        svr.stop();
    });

    LOG_INFO("Starting HTTP server on port {}...", port);
    std::cout << "QCode server listening on http://" << host << ":" << port << "\n";
    if (!options.security.token.empty()) {
        const std::string shown_host = host == "0.0.0.0" ? "<this-machine-address>" : host;
        std::cout << "Sign in: http://" << shown_host << ":" << port << "/?token=" << api_token << "\n";
    }
    std::cout << "  Health:  GET /health\n";
    std::cout << "  Providers: GET /providers\n";
    std::cout << "  Generate:  POST /generate (body: {\"text\":\"...\", \"provider\":\"...\", \"model\":\"...\"})\n";
    std::cout << "    Response is NDJSON stream of events\n";
    svr.listen(host, port);

    g_shutdown_requested = true;
    if (shutdown_watcher.joinable()) {
        shutdown_watcher.join();
    }

    LOG_INFO("Server stopping, signalling active sessions...");
    qcode::server::shutdown_active_sessions();
    qcode::TaskTool::shutdown_background(std::chrono::seconds(3));
    qcode::session_title::shutdown(std::chrono::seconds(2));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Release session log handles after all workers have joined.
    if (qcode::server::g_session_logger) qcode::server::g_session_logger->close_all();

    // Drop the sessions (and their bus subscriptions) now, while the bus is alive.
    {
        std::unordered_map<std::string, std::shared_ptr<qcode::server::GenSession>> dying;
        {
            std::lock_guard<std::mutex> lock(qcode::server::g_sessions_mutex);
            dying.swap(qcode::server::g_sessions);
        }
    }

    LOG_INFO("Server shutdown complete");
    if (qcode::server::g_session_logger) qcode::server::g_session_logger->flush();
    std::cout.flush();
    std::cerr.flush();
    // Skip static destruction. Generation, tool and title threads are detached and
    // may still be unwinding after the grace period above; running the global
    // destructors (logger, bus, DB connections) under them is a use-after-free
    // that crashes the process during exit. Everything durable is already written.
    std::fflush(nullptr);
    _exit(0);
}
