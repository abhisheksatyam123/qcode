#include "routes_internal.h"
#include "http_utils.h"
#include "workspace_fs.h"

#include <qcode/session/session_store.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace qcode {
namespace server {

namespace {

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

} // namespace

void register_fs_routes(httplib::Server& svr) {
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



}

}  // namespace server
}  // namespace qcode
