#include "routes_internal.h"
#include "workspace_fs.h"

#include <qcode/core/logger.h>
#include <algorithm>
#include <cctype>
#include <set>
#include <utility>
#include <qcode/session/session_store.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string_view>

namespace fs = std::filesystem;

namespace qcode {
namespace server {

namespace {

// Content-Type for the pre-compressed WebUI files (see
// cmake/compress_webui.cmake). Only extensions that get a .gz are listed.
std::string_view webui_content_type(const std::string& path) {
    auto ends = [&](std::string_view ext) {
        return path.size() >= ext.size() &&
               path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
    };
    if (ends(".js")) return "application/javascript";
    if (ends(".css")) return "text/css";
    if (ends(".html")) return "text/html";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".json")) return "application/json";
    if (ends(".ttf")) return "font/ttf";
    return "application/octet-stream";
}

// Serves <webui>/<path>.gz with Content-Encoding: gzip when the client accepts
// gzip and the pre-compressed copy exists. The file is streamed from disk, so
// no per-request compression or full-file buffering happens. Anything else is
// left to the normal mount-point handler.
httplib::Server::HandlerResponse serve_precompressed(
    const std::string& webui_dir, const httplib::Request& req,
    httplib::Response& res) {
    if (req.method != "GET" && req.method != "HEAD") {
        return httplib::Server::HandlerResponse::Unhandled;
    }
    if (req.get_header_value("Accept-Encoding").find("gzip") == std::string::npos) {
        return httplib::Server::HandlerResponse::Unhandled;
    }
    std::string rel = req.path == "/" ? "/index.html" : req.path;
    if (rel.rfind("/api/", 0) == 0 || rel == "/health") {
        return httplib::Server::HandlerResponse::Unhandled;
    }
    if (rel.find("..") != std::string::npos || rel.find('\\') != std::string::npos) {
        return httplib::Server::HandlerResponse::Unhandled;
    }
    const std::string gz_path = webui_dir + rel + ".gz";
    std::error_code ec;
    if (!fs::is_regular_file(gz_path, ec)) {
        return httplib::Server::HandlerResponse::Unhandled;
    }
    const auto size = fs::file_size(gz_path, ec);
    if (ec) return httplib::Server::HandlerResponse::Unhandled;
    // A source edited after its .gz was made must win, or clients keep the old file.
    const auto gz_time = fs::last_write_time(gz_path, ec);
    const auto src_time = fs::last_write_time(webui_dir + rel, ec);
    if (!ec && src_time > gz_time) return httplib::Server::HandlerResponse::Unhandled;
    ec.clear();

    res.set_header("Content-Encoding", "gzip");
    res.set_header("Vary", "Accept-Encoding");
    const std::string name = rel.substr(rel.find_last_of('/') + 1);
    res.set_header("Cache-Control", name.rfind("vendor-", 0) == 0
                                        ? "public, max-age=86400"
                                        : "no-cache");
    if (req.method == "HEAD") {
        res.set_header("Content-Type", std::string(webui_content_type(rel)));
        res.set_header("Content-Length", std::to_string(size));
        return httplib::Server::HandlerResponse::Handled;
    }
    auto file = std::make_shared<std::ifstream>(gz_path, std::ios::binary);
    if (!*file) return httplib::Server::HandlerResponse::Unhandled;
    res.set_content_provider(
        static_cast<size_t>(size), std::string(webui_content_type(rel)),
        [file](size_t offset, size_t length, httplib::DataSink& sink) {
            std::vector<char> buf(length);
            file->clear();
            file->seekg(static_cast<std::streamoff>(offset));
            file->read(buf.data(), static_cast<std::streamsize>(length));
            const auto got = file->gcount();
            if (got <= 0) return false;
            return sink.write(buf.data(), static_cast<size_t>(got));
        });
    return httplib::Server::HandlerResponse::Handled;
}

}  // namespace

void register_system_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers_list,
    const ServerSetupOptions& options) {
    if (!options.webui_dir.empty()) {
        if (fs::is_directory(options.webui_dir)) {
            LOG_INFO("Serving Web UI from {}", options.webui_dir);
            svr.set_mount_point("/", options.webui_dir);
            // vendor-* libraries are cached for a day (their names carry no
            // version, so an upgrade must reach clients); the app's own files
            // (index.html, app.js, style.css, ...) are revalidated every load.
            svr.set_file_request_handler([](const httplib::Request& req, httplib::Response& res) {
                const std::string name = req.path.substr(req.path.find_last_of('/') + 1);
                res.set_header("Cache-Control", name.rfind("vendor-", 0) == 0
                                                    ? "public, max-age=86400"
                                                    : "no-cache");
            });
            svr.set_pre_routing_handler(
                [webui = options.webui_dir, policy = options.security](
                    const httplib::Request& req, httplib::Response& res) {
                    // This hook replaces the one in setup_server_routes, so the
                    // security gate and the session path check run here too.
                    if (security_gate(req, res, policy)) return httplib::Server::HandlerResponse::Handled;
                    if (rejected_session_path(req, res)) return httplib::Server::HandlerResponse::Handled;
                    return serve_precompressed(webui, req, res);
                });
        } else {
            LOG_WARN("Web UI directory not found at {}", options.webui_dir);
        }
    }

    // Health checks
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    svr.Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
#ifndef QCODE_VERSION
#define QCODE_VERSION "0.2.0"
#endif
        nlohmann::json j = {{"name", "qcode-server"}, {"version", QCODE_VERSION}};
        res.set_content(j.dump(), "application/json");
    });

    // Provider list
    svr.Get("/providers", [providers_list](const httplib::Request&, httplib::Response& res) {
        nlohmann::json j = nlohmann::json::array();
        if (providers_list) {
            for (const auto& p : *providers_list) {
                nlohmann::json models = nlohmann::json::array();
                for (const auto& m : p.models) {
                    const bool is_free = m.cost_configured && m.input_cost == 0.0 && m.output_cost == 0.0;
                    models.push_back({
                        {"id", m.id},
                        {"name", m.name},
                        {"context_window", m.context_window},
                        {"reasoning", m.reasoning},
                        {"tool_call", m.tool_call},
                        {"vision", m.vision},
                        {"input_cost", m.input_cost},
                        {"output_cost", m.output_cost},
                        {"cost_configured", m.cost_configured},
                        {"is_free", is_free},
                        {"reasoning_efforts", m.reasoning_efforts},
                        {"reasoning_default", m.reasoning_default}
                    });
                }
                j.push_back({{"id", p.id}, {"name", p.name}, {"models", models}});
            }
        }
        res.set_content(j.dump(2), "application/json");
    });

    // Workspace Suggestions matching user input directory
    std::string default_ws = options.default_workspace;
    svr.Get("/api/workspaces", [default_ws](const httplib::Request& req, httplib::Response& res) {
        std::string query = req.has_param("query") ? req.get_param_value("query") : "";
        while (!query.empty() && (query[0] == ' ' || query[0] == '\t')) query.erase(0, 1);
        while (!query.empty() && (query.back() == ' ' || query.back() == '\t')) query.pop_back();

        std::string q_lower = query;
        std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), [](unsigned char c) {
            return std::tolower(c);
        });

        std::set<std::string> seen;
        nlohmann::json list = nlohmann::json::array();

        auto add_ws = [&](const std::string& path_str, const std::string& source) {
            if (path_str.empty()) return;
            std::string exp = expand_tilde(path_str);
            std::error_code ec;
            if (!fs::exists(exp, ec) || !fs::is_directory(exp, ec)) return;
            if (seen.find(path_str) != seen.end()) return;

            fs::path p(exp);
            std::string name = p.filename().string();
            if (name.empty()) name = path_str;

            // Only suggest workspace names which match user input directory
            if (!q_lower.empty()) {
                std::string p_lower = path_str;
                std::transform(p_lower.begin(), p_lower.end(), p_lower.begin(), [](unsigned char c) {
                    return std::tolower(c);
                });
                std::string n_lower = name;
                std::transform(n_lower.begin(), n_lower.end(), n_lower.begin(), [](unsigned char c) {
                    return std::tolower(c);
                });
                std::string exp_lower = exp;
                std::transform(exp_lower.begin(), exp_lower.end(), exp_lower.begin(), [](unsigned char c) {
                    return std::tolower(c);
                });

                if (p_lower.find(q_lower) == std::string::npos &&
                    n_lower.find(q_lower) == std::string::npos &&
                    exp_lower.find(q_lower) == std::string::npos) {
                    return;
                }
            }

            seen.insert(path_str);
            list.push_back({
                {"path", path_str},
                {"name", name},
                {"source", source}
            });
        };

        // 1. From existing sessions
        auto sessions = qcode::session::list_sessions_full(false);
        for (const auto& s : sessions) {
            if (!s.workspace.empty()) {
                add_ws(s.workspace, "recent");
            }
        }

        // 2. Default workspace if configured
        if (!default_ws.empty()) {
            add_ws(default_ws, "default");
        }

        // 3. Scan project directories
        const char* home = std::getenv("HOME");
        std::vector<std::string> base_dirs;
        if (home && *home) {
            base_dirs.push_back(std::string(home) + "/project");
            base_dirs.push_back(std::string(home) + "/projects");
        }
        char cwd[4096] = {0};
        if (getcwd(cwd, sizeof(cwd))) {
            fs::path cp(cwd);
            if (cp.has_parent_path()) {
                base_dirs.push_back(cp.parent_path().string());
            }
        }

        // If user typed a directory path, scan children of that directory too
        if (!query.empty()) {
            std::string q_exp = expand_tilde(query);
            std::error_code ec;
            if (fs::is_directory(q_exp, ec)) {
                base_dirs.push_back(q_exp);
            } else {
                fs::path qp(q_exp);
                if (qp.has_parent_path() && fs::is_directory(qp.parent_path(), ec)) {
                    base_dirs.push_back(qp.parent_path().string());
                }
            }
        }

        for (const auto& base : base_dirs) {
            std::error_code ec;
            if (fs::is_directory(base, ec)) {
                for (const auto& entry : fs::directory_iterator(base, ec)) {
                    if (ec) break;
                    if (entry.is_directory(ec)) {
                        std::string fn = entry.path().filename().string();
                        if (!fn.empty() && fn[0] != '.') {
                            std::string rel = entry.path().string();
                            if (home && rel.rfind(home, 0) == 0) {
                                rel = "~" + rel.substr(strlen(home));
                            }
                            add_ws(rel, "discovered");
                        }
                    }
                }
            }
        }

        res.set_content(list.dump(2), "application/json");
    });

    // Child folders of one directory, for the New Session folder picker.
    // GET /api/dirs?path=<absolute or ~ path>. An empty path means the default
    // workspace, else $HOME. Returns absolute paths so the client can pass the
    // selection straight to POST /sessions. Hidden (dot) folders are skipped.
    svr.Get("/api/dirs", [default_ws](const httplib::Request& req, httplib::Response& res) {
        std::string raw = req.has_param("path") ? req.get_param_value("path") : "";
        while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.erase(0, 1);
        while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.pop_back();
        const char* home_env = std::getenv("HOME");
        std::string home = home_env ? home_env : "";
        if (raw.empty()) raw = !default_ws.empty() ? default_ws : (home.empty() ? "/" : home);

        std::string exp = expand_tilde(raw);
        auto reply = [&res](int status, const nlohmann::json& body) {
            res.status = status;
            res.set_content(body.dump(2, ' ', false, nlohmann::json::error_handler_t::replace),
                            "application/json");
        };
        if (exp.empty() || exp.front() != '/') {
            reply(400, {{"error", "path must be absolute or start with ~/"}});
            return;
        }
        std::error_code ec;
        fs::path dir = fs::weakly_canonical(fs::path(exp), ec);
        if (ec || !fs::is_directory(dir, ec)) {
            reply(404, {{"error", "not a directory"}, {"path", exp}});
            return;
        }

        struct Child { std::string lower, name, path; };
        std::vector<Child> dirs;
        bool truncated = false;
        constexpr std::size_t kMaxDirs = 1000;
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            reply(403, {{"error", "cannot read directory"}, {"path", dir.string()}});
            return;
        }
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const fs::path child = it->path();
            std::string name = child.filename().string();
            if (name.empty() || name[0] == '.') continue;
            std::error_code dec;
            if (!it->is_directory(dec) || dec) continue;
            if (dirs.size() >= kMaxDirs) { truncated = true; break; }
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            dirs.push_back({std::move(lower), std::move(name), child.string()});
        }
        std::sort(dirs.begin(), dirs.end(),
                  [](const Child& a, const Child& b) { return a.lower < b.lower; });

        nlohmann::json list = nlohmann::json::array();
        for (const auto& c : dirs) list.push_back({{"name", c.name}, {"path", c.path}});
        nlohmann::json parent = nullptr;
        if (dir.has_parent_path() && dir.parent_path() != dir) parent = dir.parent_path().string();

        reply(200, {{"path", dir.string()},
                    {"parent", parent},
                    {"home", home},
                    {"dirs", list},
                    {"truncated", truncated}});
    });

    // Model Performance & Telemetry List
    svr.Get("/api/models/performance", [](const httplib::Request&, httplib::Response& res) {
        auto summaries = qcode::session::list_all_model_performance_summaries();
        nlohmann::json j = nlohmann::json::array();
        for (const auto& s : summaries) {
            j.push_back({
                {"model_id", s.model_id},
                {"provider", s.provider},
                {"model_name", s.model_name},
                {"architecture_info", s.architecture_info},
                {"context_window", s.context_window},
                {"output_limit", s.output_limit},
                {"tool_call_supported", s.tool_call_supported},
                {"multi_turn_reliable", s.multi_turn_reliable},
                {"verified_benchmark", s.verified_benchmark},
                {"recommended_for", s.recommended_for},
                {"total_turns", s.total_turns},
                {"successful_turns", s.successful_turns},
                {"failed_turns", s.failed_turns},
                {"tool_loop_failures", s.tool_loop_failures},
                {"avg_latency_ms", s.avg_latency_ms},
                {"positive_feedback_count", s.positive_feedback_count},
                {"negative_feedback_count", s.negative_feedback_count},
                {"success_rate", s.success_rate()}
            });
        }
        res.set_content(j.dump(2), "application/json");
    });

    // Model User Feedback
    svr.Post("/api/models/feedback", [](const httplib::Request& req, httplib::Response& res) {
        try {
            nlohmann::json body;
            if (!parse_object_body(req.body, res, body)) return;
            if (!check_fields(body, res, {{"model_id", FieldType::String}, {"rating", FieldType::String}})) return;
            std::string model_id = body.value("model_id", "");
            std::string rating = body.value("rating", "up");
            if (model_id.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"model_id is required"})", "application/json");
                return;
            }
            qcode::session::record_user_feedback(model_id, rating == "up");
            res.set_content(R"({"status":"success"})", "application/json");
        } catch (const std::exception&) {
            res.status = 400;
            res.set_content(R"({"error":"invalid json"})", "application/json");
        }
    });

    register_payment_routes(svr);
}

}  // namespace server
}  // namespace qcode
