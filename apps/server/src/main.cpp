#include <qcode/core/file_logger.h>
#include <qcode/core/session_file_logger.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/config/config.h>
#include <qcode/session/session_store.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>

#include "server_routes.h"
#include "routes/session_runtime.h"

#include <httplib.h>
#include <unistd.h>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_shutdown_requested{false};
static std::shared_ptr<qcode::bus::BusRuntime> g_bus;

std::string log_dir() {
    if (const char* d = std::getenv("QCODE_LOG_DIR")) return d;
    return "/tmp/qcode-logs";
}

void handle_signal(int) {
    g_shutdown_requested = true;
}

int main(int argc, char* argv[]) {
    int port = 9080;
    std::string custom_log_dir;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        if (arg == "-p" && i + 1 < argc) port = std::stoi(argv[++i]);
        if (arg == "--log-dir" && i + 1 < argc) custom_log_dir = argv[++i];
        if (arg == "--help" || arg == "-h") {
            std::cerr << "Usage: qcode-server [--port PORT] [--log-dir DIR]\n";
            return 0;
        }
    }

    // One log file per session: <dir>/qcode-server-<session_id>.log
    const std::string active_log_dir = !custom_log_dir.empty() ? custom_log_dir : log_dir();
    qcode::server::g_session_logger = qcode::install_session_file_logger(
        active_log_dir, "qcode-server", qcode::logger::LogLevel::kLogLevelDebug);
    qcode::logger::set_thread_name("server");
    LOG_INFO("QCode server starting... (logs in {})", active_log_dir);

    g_bus = std::make_shared<qcode::bus::BusRuntime>();
    qcode::contract::register_all_events(*g_bus);
    qcode::providers::register_authenticated_providers();
    qcode::session::init_database();

    auto providers_list = std::make_shared<std::vector<qcode::ProviderInfo>>(
        qcode::load_providers_from_config());
    qcode::session::seed_model_capabilities(*providers_list);
    if (providers_list->empty()) {
        LOG_ERROR("No providers configured. Add them to {}",
                  qcode::config_path());
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
        LOG_INFO("Shutdown requested, stopping server...");
        svr.stop();
    });

    LOG_INFO("Starting HTTP server on port {}...", port);
    std::cout << "QCode server listening on http://0.0.0.0:" << port << "\n";
    std::cout << "  Health:  GET /health\n";
    std::cout << "  Providers: GET /providers\n";
    std::cout << "  Generate:  POST /generate (body: {\"text\":\"...\", \"provider\":\"...\", \"model\":\"...\"})\n";
    std::cout << "    Response is NDJSON stream of events\n";
    svr.listen("0.0.0.0", port);

    g_shutdown_requested = true;
    if (shutdown_watcher.joinable()) {
        shutdown_watcher.join();
    }

    LOG_INFO("Server stopping, signalling active sessions...");
    qcode::server::shutdown_active_sessions();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Release session log handles after all workers have joined.
    if (qcode::server::g_session_logger) qcode::server::g_session_logger->close_all();

    LOG_INFO("Server shutdown complete");
    return 0;
}
