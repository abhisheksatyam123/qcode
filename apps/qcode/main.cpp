/**
 * qcode: one executable with three runtime modes.
 *
 *   qcode                 interactive TUI (default)
 *   qcode tui             interactive TUI
 *   qcode serve           headless HTTP API + WebUI
 *   qcode prompt <text>   one-shot HTTP client
 */
#include <qcode_app/frontends.h>

#include <iostream>
#include <string_view>
#include <vector>

#ifndef QCODE_VERSION
#define QCODE_VERSION "0.0.0"
#endif

namespace {

void print_help() {
    std::cout
        << "Usage: qcode [mode] [options]\n"
        << "\n"
        << "Modes:\n"
        << "  tui               Interactive terminal UI (default)\n"
        << "  serve             Headless HTTP API and WebUI\n"
        << "  prompt <text>     One-shot prompt through qcode serve\n"
        << "\n"
        << "Global options:\n"
        << "  -h, --help        Show this help and exit\n"
        << "  --version         Show the version and exit\n"
        << "\n"
        << "Mode help:\n"
        << "  qcode tui --help\n"
        << "  qcode serve --help\n"
        << "  qcode prompt --help\n"
        << "\n"
        << "The TUI runs generation in-process. The prompt mode is an HTTP\n"
        << "client and requires a running `qcode serve` process.\n";
}

// Keep argv[0] from the real process, then append the mode's arguments.
std::vector<char*> mode_argv(int argc, char* argv[], int first) {
    std::vector<char*> args;
    args.reserve(static_cast<size_t>(argc - first) + 1);
    args.push_back(argv[0]);
    for (int i = first; i < argc; ++i) args.push_back(argv[i]);
    args.push_back(nullptr);
    return args;
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string_view command = argc > 1 ? argv[1] : "";

    if (command.empty()) {
        return qcode::app::run_tui(1, argv);
    }
    if (command == "-h" || command == "--help" || command == "help") {
        print_help();
        return 0;
    }
    if (command == "--version") {
        std::cout << "qcode " << QCODE_VERSION << '\n';
        return 0;
    }

    if (command == "tui") {
        auto args = mode_argv(argc, argv, 2);
        return qcode::app::run_tui(argc - 1, args.data());
    }
    if (command == "serve") {
        auto args = mode_argv(argc, argv, 2);
        return qcode::app::run_server(argc - 1, args.data());
    }
    if (command == "prompt") {
        auto args = mode_argv(argc, argv, 2);
        return qcode::app::run_cli(argc - 1, args.data());
    }

    std::cerr << "Unknown mode: " << command << "\n\n";
    print_help();
    return 2;
}
