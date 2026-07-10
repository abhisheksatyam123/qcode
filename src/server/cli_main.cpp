/**
 * qcode-cli: Simple CLI client for the qcode-server.
 *
 * Usage:
 *   qcode-cli --prompt "Hello" [--server http://localhost:9080] [--verbose]
 *
 * Streams assistant response to stdout. Tool info to stderr (with --verbose).
 */
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

struct Config {
    std::string server = "http://localhost:9080";
    std::string provider;
    std::string model;
    std::string prompt;
    std::string reasoning_mode = "off";
    bool verbose = false;
};

static void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " --prompt <text> [options]\n"
              << "Options:\n"
              << "  --server <url>    Server URL (default: http://localhost:9080)\n"
              << "  --provider <id>   Provider ID\n"
              << "  --model <id>      Model ID\n"
              << "  --reasoning <mode> Reasoning mode\n"
              << "  --verbose, -v     Print tool calls to stderr\n"
              << "  --help, -h        Show this help\n";
}

static Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--prompt" && i + 1 < argc) cfg.prompt = argv[++i];
        else if (arg == "--server" && i + 1 < argc) cfg.server = argv[++i];
        else if (arg == "--provider" && i + 1 < argc) cfg.provider = argv[++i];
        else if (arg == "--model" && i + 1 < argc) cfg.model = argv[++i];
        else if (arg == "--reasoning" && i + 1 < argc) cfg.reasoning_mode = argv[++i];
        else if (arg == "--verbose" || arg == "-v") cfg.verbose = true;
        else if (arg == "--help" || arg == "-h") { print_usage(argv[0]); exit(0); }
    }
    if (cfg.prompt.empty()) {
        std::cerr << "Error: --prompt is required\n\n";
        print_usage(argv[0]);
        exit(1);
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    httplib::Client cli(cfg.server);

    // Fetch providers if needed
    if (cfg.provider.empty() || cfg.model.empty()) {
        auto prov_res = cli.Get("/providers");
        if (prov_res && prov_res->status == 200) {
            auto providers = nlohmann::json::parse(prov_res->body);
            if (!providers.empty()) {
                if (cfg.provider.empty()) cfg.provider = providers[0]["id"];
                if (cfg.model.empty()) {
                    auto& models = providers[0]["models"];
                    if (!models.empty()) cfg.model = models[0]["id"];
                }
            }
        }
    }

    // Build request
    nlohmann::json body = {
        {"text", cfg.prompt},
        {"provider", cfg.provider},
        {"model", cfg.model},
        {"reasoning_mode", cfg.reasoning_mode}
    };

    // Send POST and get streaming response
    auto res = cli.Post("/generate", body.dump(), "application/json");

    if (!res) {
        std::cerr << "Error: " << httplib::to_string(res.error()) << "\n";
        return 1;
    }

    if (res->status != 200) {
        std::cerr << "Error: HTTP " << res->status << " " << res->body << "\n";
        return 1;
    }

    // Parse NDJSON response
    std::string full_text;
    std::string body_str = res->body;
    size_t pos = 0;
    while (pos < body_str.size()) {
        auto nl = body_str.find('\n', pos);
        if (nl == std::string::npos) break;
        std::string line = body_str.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.empty()) continue;

        try {
            auto evt = nlohmann::json::parse(line);
            std::string type = evt.value("type", "");

            if (type == "backend.message.delta") {
                full_text = evt.value("text", "");
                std::cout << "\r" << full_text << std::flush;
            } else if (type == "backend.tool.call.started" && cfg.verbose) {
                std::cerr << "\n[Tool] " << evt.value("tool_name", "?") << " started\n";
            } else if (type == "backend.tool.call.completed" && cfg.verbose) {
                std::string st = evt.value("is_error", false) ? "failed" : "success";
                double dur = evt.value("duration_ms", 0.0);
                std::cerr << "[Tool] " << evt.value("tool_name", "?")
                          << " " << st << " (" << (int)dur << "ms)\n";
            } else if (type == "backend.error.occurred") {
                std::cerr << "\n[Error] " << evt.value("message", "") << "\n";
            }
        } catch (...) {}
    }

    std::cout << "\n";
    return 0;
}
