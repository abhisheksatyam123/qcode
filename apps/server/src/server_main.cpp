#include <qcode/core/file_logger.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/core/config.h>
#include <qcode/session/session_store.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>

#include "server_routes.h"

#include <httplib.h>
#include <unistd.h>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static std::atomic<bool> g_running{true};
static std::shared_ptr<qcode::bus::BusRuntime> g_bus;

void handle_signal(int) { g_running = false; }

int main(int argc, char* argv[]) {
    qcode::install_file_logger("/tmp/qcode-server.log", qcode::logger::LogLevel::kLogLevelDebug);
    qcode::logger::set_thread_name("server");
    LOG_INFO("QCode server starting...");

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

    g_bus = std::make_shared<qcode::bus::BusRuntime>();
    qcode::contract::register_all_events(*g_bus);
    qcode::providers::register_authenticated_providers();
    qcode::session::init_database();

    auto providers_list = std::make_shared<std::vector<qcode::ProviderInfo>>(
        qcode::load_providers_from_config());
    if (providers_list->empty()) {
        LOG_ERROR("No providers configured. Create ~/.config/qcode/providers.json");
        return 1;
    }
    LOG_INFO("Loaded {} provider(s)", providers_list->size());

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    httplib::Server svr;

    qcode::server::ServerSetupOptions options;
    {
        std::string webui_path = std::string(argv[0]);
        auto pos = webui_path.find_last_of("/\\");
        if (pos != std::string::npos) webui_path = webui_path.substr(0, pos);
        webui_path += "/webui";
        if (access(webui_path.c_str(), F_OK) != 0) {
            webui_path = QCODE_SERVER_WEBUI_DIR;
        }
        options.webui_dir = webui_path;
    }

    qcode::server::setup_server_routes(svr, g_bus, providers_list, options);

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
