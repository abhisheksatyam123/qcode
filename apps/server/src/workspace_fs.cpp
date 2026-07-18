#include "workspace_fs.h"

#include <qcode/session/session_store.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace qcode {
namespace server {

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string exec_capture(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) result += buf;
    pclose(pipe);
    return result;
}

std::string expand_tilde(const std::string& s) {
    if (s == "~" || s.rfind("~/", 0) == 0) {
        const char* home = std::getenv("HOME");
        std::string base = home ? std::string(home) : "";
        if (s.size() == 1) return base;
        return base + s.substr(1);
    }
    return s;
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}


std::string resolve_session_workspace(const std::string& sid) {
    std::string ws = qcode::session::get_session_workspace(sid);
    if (!ws.empty()) ws = expand_tilde(ws);
    if (ws.empty()) {
        char cwd[4096] = {0};
        if (getcwd(cwd, sizeof(cwd))) ws = cwd;
    }
    return ws;
}

// Resolve a client-relative path against the session workspace. Rejects
// absolute paths and any path that escapes the workspace root.
// On success: sets abs_out to the absolute path and rel_out to the
// normalized relative path ("" for workspace root). Returns error message or "".
std::string resolve_workspace_path(const std::string& workspace,
                                          const std::string& rel_in,
                                          std::string& abs_out,
                                          std::string& rel_out) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (workspace.empty()) return "no workspace";
    if (!rel_in.empty() && rel_in[0] == '/') return "absolute paths not allowed";
    if (rel_in.find('\0') != std::string::npos) return "invalid path";

    fs::path root = fs::weakly_canonical(fs::path(workspace), ec);
    if (ec || root.empty()) {
        // Workspace may not exist yet — fall back to absolute() if present.
        root = fs::absolute(fs::path(workspace), ec);
        if (ec) return "invalid workspace";
    }

    fs::path candidate = root;
    if (!rel_in.empty() && rel_in != ".") {
        candidate = root / fs::path(rel_in);
    }
    candidate = fs::weakly_canonical(candidate, ec);
    if (ec) {
        // Parent may exist while leaf does not (for create/write).
        candidate = fs::absolute(root / fs::path(rel_in.empty() ? "." : rel_in), ec);
        if (ec) return "invalid path";
    }

    std::string root_s = root.string();
    std::string cand_s = candidate.string();
    // Ensure cand is root or under root/
    if (cand_s != root_s) {
        std::string prefix = root_s;
        if (!prefix.empty() && prefix.back() != '/') prefix += '/';
        if (cand_s.rfind(prefix, 0) != 0) return "path escapes workspace";
    }

    abs_out = cand_s;
    if (cand_s == root_s) {
        rel_out.clear();
    } else {
        rel_out = cand_s.substr(root_s.size() + 1);
    }
    return "";
}

bool looks_binary(const std::string& data) {
    // NUL byte or high ratio of non-printable bytes → treat as binary.
    size_t n = std::min(data.size(), size_t(8192));
    if (n == 0) return false;
    size_t bad = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        if (c == 0) return true;
        if (c < 9 || (c > 13 && c < 32)) bad++;
    }
    return bad * 10 > n;  // >10% control chars
}


}  // namespace server
}  // namespace qcode
