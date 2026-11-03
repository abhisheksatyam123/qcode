#include <clipboard.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

namespace qcode {
namespace tui {

std::string base64_encode(std::string_view data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8) |
                           static_cast<unsigned char>(data[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
        out.push_back(kAlphabet[v & 63]);
    }
    const size_t rest = data.size() - i;
    if (rest == 1) {
        const unsigned v = static_cast<unsigned char>(data[i]) << 16;
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out += "==";
    } else if (rest == 2) {
        const unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::string osc52_copy_sequence(std::string_view text, bool inside_tmux) {
    std::string seq = "\033]52;c;" + base64_encode(text) + "\a";
    if (!inside_tmux) return seq;
    // tmux passthrough: ESC P tmux; <seq with every ESC doubled> ESC '\'.
    std::string wrapped = "\033Ptmux;";
    for (char c : seq) {
        if (c == '\033') wrapped.push_back('\033');
        wrapped.push_back(c);
    }
    wrapped += "\033\\";
    return wrapped;
}

namespace {

bool on_path(const char* tool) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string_view rest(path);
    while (!rest.empty()) {
        const size_t colon = rest.find(':');
        const std::string_view dir = rest.substr(0, colon);
        if (!dir.empty()) {
            std::error_code ec;
            if (std::filesystem::exists(
                    std::filesystem::path(std::string(dir)) / tool, ec)) {
                return true;
            }
        }
        if (colon == std::string_view::npos) break;
        rest.remove_prefix(colon + 1);
    }
    return false;
}

// Picks a desktop clipboard command once per process ("" = none).
const std::string& clipboard_command() {
    static const std::string cmd = [] {
#if defined(__APPLE__)
        if (on_path("pbcopy")) return std::string("pbcopy");
#endif
        if (std::getenv("WAYLAND_DISPLAY") && on_path("wl-copy")) {
            return std::string("wl-copy");
        }
        if (std::getenv("DISPLAY")) {
            if (on_path("xclip")) return std::string("xclip -selection clipboard");
            if (on_path("xsel")) return std::string("xsel --clipboard --input");
        }
        return std::string();
    }();
    return cmd;
}

}  // namespace

void copy_to_clipboard(const std::string& text, std::string& out_sequence) {
    out_sequence = osc52_copy_sequence(text, std::getenv("TMUX") != nullptr);
    const std::string& cmd = clipboard_command();
    if (cmd.empty()) return;
    // The tool can block (xclip waits for the X server), so never on the UI
    // thread. Output is discarded so it cannot scribble over the TUI.
    std::thread([cmd, text] {
        const std::string full = cmd + " >/dev/null 2>&1";
        if (FILE* pipe = ::popen(full.c_str(), "w")) {
            std::fwrite(text.data(), 1, text.size(), pipe);
            ::pclose(pipe);
        }
    }).detach();
}

}  // namespace tui
}  // namespace qcode
