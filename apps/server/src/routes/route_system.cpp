#include "routes_internal.h"

#include <qcode/core/logger.h>
#include <qcode/session/session_store.h>

#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace qcode {
namespace server {

void register_system_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers_list,
    const ServerSetupOptions& options) {
    if (!options.webui_dir.empty()) {
        if (fs::is_directory(options.webui_dir)) {
            LOG_INFO("Serving Web UI from {}", options.webui_dir);
            svr.set_mount_point("/", options.webui_dir);
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
#define QCODE_VERSION "0.1.0"
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
                    models.push_back({{"id", m.id}, {"name", m.name}});
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
            auto body = nlohmann::json::parse(req.body);
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
