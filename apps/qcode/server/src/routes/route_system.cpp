#include "routes_internal.h"

#include <qcode/core/logger.h>
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
}

}  // namespace server
}  // namespace qcode
