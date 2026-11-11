#include <qcode/core/session_file_logger.h>
#include <qcode/core/logger.h>
#include "routes/session_runtime.h"
#include "routes/routes_internal.h"
#include "session/session_db_internal.h"
#include <gtest/gtest.h>
#include <httplib.h>

#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>
#include <qcode/session/session_store.h>
#include <qcode/session/usage_stats.h>
#include <qcode/tools/task_tool.h>
#include "server_routes.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

class ServerRoutesTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create an isolated sqlite database for this test run
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        test_db_path_ = "/tmp/qcode_routes_test_" + std::to_string(getpid()) + "_" + std::to_string(now) + ".db";
        setenv("QCODE_DB_PATH", test_db_path_.c_str(), 1);
        qcode::session::init_database();

        // Create a temporary workspace directory
        test_workspace_dir_ = "/tmp/qcode_routes_ws_" + std::to_string(getpid()) + "_" + std::to_string(now);
        fs::create_directories(test_workspace_dir_);

        // Setup mock providers
        providers_ = std::make_shared<std::vector<qcode::ProviderInfo>>();
        qcode::ProviderInfo p;
        p.id = "mock-provider";
        p.name = "Mock Provider";
        qcode::ModelInfo m;
        m.id = "mock-model";
        m.name = "Mock Model";
        p.models.push_back(m);
        providers_->push_back(p);
        qcode::session::seed_model_capabilities(*providers_);

        // Setup bus runtime
        bus_ = std::make_shared<qcode::bus::BusRuntime>();
        qcode::contract::register_all_events(*bus_);

        // Configure options with webui source directory
        qcode::server::ServerSetupOptions options;
#ifdef QCODE_SERVER_WEBUI_DIR
        options.webui_dir = QCODE_SERVER_WEBUI_DIR;
#else
        options.webui_dir = "apps/webui/src";
#endif
        options.default_workspace = test_workspace_dir_;

        server_ = std::make_unique<httplib::Server>();
        qcode::server::setup_server_routes(*server_, bus_, providers_, options);

        port_ = server_->bind_to_any_port("127.0.0.1");
        ASSERT_GT(port_, 0);

        server_thread_ = std::thread([this]() {
            server_->listen_after_bind();
        });

        client_ = std::make_unique<httplib::Client>("127.0.0.1", port_);
        client_->set_connection_timeout(std::chrono::seconds(5));
        client_->set_read_timeout(std::chrono::seconds(5));
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        qcode::session::SharedDbHandle::instance().reset();
        std::error_code ec;
        fs::remove(test_db_path_, ec);
        fs::remove_all(test_workspace_dir_, ec);
    }

    std::string test_db_path_;
    std::string test_workspace_dir_;
    int port_ = 0;
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers_;
    std::shared_ptr<qcode::bus::BusRuntime> bus_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    std::unique_ptr<httplib::Client> client_;
};

TEST_F(ServerRoutesTest, WebUiStaticFilesServed) {
    auto res = client_->Get("/");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(res->body.find("QCode Chat"), std::string::npos);

    auto res_js = client_->Get("/app.js");
    ASSERT_TRUE(res_js != nullptr);
    EXPECT_EQ(res_js->status, 200);
    EXPECT_NE(res_js->get_header_value("Content-Type").find("javascript"), std::string::npos);

    auto res_css = client_->Get("/style.css");
    ASSERT_TRUE(res_css != nullptr);
    EXPECT_EQ(res_css->status, 200);
    EXPECT_NE(res_css->get_header_value("Content-Type").find("css"), std::string::npos);

    auto res_vendor = client_->Get("/vendor-marked.min.js");
    ASSERT_TRUE(res_vendor != nullptr);
    EXPECT_EQ(res_vendor->status, 200);

    auto res_404 = client_->Get("/nonexistent_test_asset.xyz");
    ASSERT_TRUE(res_404 != nullptr);
    EXPECT_EQ(res_404->status, 404);
}

TEST_F(ServerRoutesTest, SystemHealthAndVersion) {
    auto res_health = client_->Get("/health");
    ASSERT_TRUE(res_health != nullptr);
    EXPECT_EQ(res_health->status, 200);
    EXPECT_EQ(nlohmann::json::parse(res_health->body).value("status", ""), "ok");

    auto res_api_health = client_->Get("/api/health");
    ASSERT_TRUE(res_api_health != nullptr);
    EXPECT_EQ(res_api_health->status, 200);
    EXPECT_EQ(nlohmann::json::parse(res_api_health->body).value("status", ""), "ok");

    auto res_ver = client_->Get("/api/version");
    ASSERT_TRUE(res_ver != nullptr);
    EXPECT_EQ(res_ver->status, 200);
    auto j_ver = nlohmann::json::parse(res_ver->body);
    EXPECT_EQ(j_ver.value("name", ""), "qcode-server");
    EXPECT_FALSE(j_ver.value("version", "").empty());
}

TEST_F(ServerRoutesTest, ProvidersAndTelemetryRoutes) {
    auto res_prov = client_->Get("/providers");
    ASSERT_TRUE(res_prov != nullptr);
    EXPECT_EQ(res_prov->status, 200);
    auto j_prov = nlohmann::json::parse(res_prov->body);
    ASSERT_TRUE(j_prov.is_array());
    ASSERT_FALSE(j_prov.empty());
    EXPECT_EQ(j_prov[0].value("id", ""), "mock-provider");
    ASSERT_TRUE(j_prov[0]["models"].is_array());
    ASSERT_FALSE(j_prov[0]["models"].empty());
    EXPECT_TRUE(j_prov[0]["models"][0].contains("reasoning"));
    EXPECT_TRUE(j_prov[0]["models"][0].contains("vision"));
    EXPECT_TRUE(j_prov[0]["models"][0].contains("context_window"));
    EXPECT_TRUE(j_prov[0]["models"][0].contains("is_free"));

    auto res_tele = client_->Get("/api/models/performance");
    ASSERT_TRUE(res_tele != nullptr);
    EXPECT_EQ(res_tele->status, 200);
    auto j_tele = nlohmann::json::parse(res_tele->body);
    ASSERT_TRUE(j_tele.is_array());

    // Feedback valid
    nlohmann::json fb = {{"model_id", "mock-model"}, {"rating", "up"}};
    auto res_fb = client_->Post("/api/models/feedback", fb.dump(), "application/json");
    ASSERT_TRUE(res_fb != nullptr);
    EXPECT_EQ(res_fb->status, 200);
    EXPECT_EQ(nlohmann::json::parse(res_fb->body).value("status", ""), "success");

    // Feedback invalid (missing model_id)
    nlohmann::json bad_fb = {{"rating", "down"}};
    auto res_bad_fb = client_->Post("/api/models/feedback", bad_fb.dump(), "application/json");
    ASSERT_TRUE(res_bad_fb != nullptr);
    EXPECT_EQ(res_bad_fb->status, 400);
}

TEST_F(ServerRoutesTest, SessionLifecycleAndStats) {
    auto res_initial = client_->Get("/sessions");
    ASSERT_TRUE(res_initial != nullptr);
    EXPECT_EQ(res_initial->status, 200);
    EXPECT_EQ(nlohmann::json::parse(res_initial->body).size(), 0);

    // Create session
    nlohmann::json req_create = {
        {"provider", "mock-provider"},
        {"model", "mock-model"},
        {"workspace", test_workspace_dir_}
    };
    auto res_create = client_->Post("/sessions", req_create.dump(), "application/json");
    ASSERT_TRUE(res_create != nullptr);
    EXPECT_EQ(res_create->status, 200);
    auto j_created = nlohmann::json::parse(res_create->body);
    std::string session_id = j_created.value("id", "");
    ASSERT_FALSE(session_id.empty());
    EXPECT_EQ(j_created.value("title", ""), "Session - mock-model");
    EXPECT_NE(j_created.value("title", ""), session_id);

    // Create session with custom title and workspace
    nlohmann::json req_custom = {
        {"provider", "mock-provider"},
        {"model", "mock-model"},
        {"workspace", test_workspace_dir_},
        {"title", "My Custom Project"}
    };
    auto res_custom = client_->Post("/sessions", req_custom.dump(), "application/json");
    ASSERT_TRUE(res_custom != nullptr);
    EXPECT_EQ(res_custom->status, 200);
    auto j_custom = nlohmann::json::parse(res_custom->body);
    std::string custom_sid = j_custom.value("id", "");
    ASSERT_FALSE(custom_sid.empty());
    EXPECT_EQ(j_custom.value("title", ""), "My Custom Project");
    EXPECT_EQ(j_custom.value("workspace", ""), test_workspace_dir_);

    // Clean up the custom session so subsequent tests have single session
    auto res_del_custom = client_->Delete("/session/" + custom_sid);
    ASSERT_TRUE(res_del_custom != nullptr);
    EXPECT_EQ(res_del_custom->status, 200);

    // Verify session in /sessions
    auto res_list = client_->Get("/sessions");
    ASSERT_TRUE(res_list != nullptr);
    auto j_list = nlohmann::json::parse(res_list->body);
    EXPECT_EQ(j_list.size(), 1);
    EXPECT_EQ(j_list[0].value("id", ""), session_id);

    // GET /session/:id
    auto res_get = client_->Get("/session/" + session_id);
    ASSERT_TRUE(res_get != nullptr);
    EXPECT_EQ(res_get->status, 200);
    auto j_get = nlohmann::json::parse(res_get->body);
    EXPECT_EQ(j_get.value("id", ""), session_id);

    // GET /session/last
    auto res_last = client_->Get("/session/last");
    ASSERT_TRUE(res_last != nullptr);
    EXPECT_EQ(res_last->status, 200);
    EXPECT_EQ(nlohmann::json::parse(res_last->body).value("id", ""), session_id);

    // POST /rename
    nlohmann::json req_rename = {
        {"session_id", session_id},
        {"title", "My Renamed Session"}
    };
    auto res_rename = client_->Post("/rename", req_rename.dump(), "application/json");
    ASSERT_TRUE(res_rename != nullptr);
    EXPECT_EQ(res_rename->status, 200);

    auto res_get_renamed = client_->Get("/session/" + session_id);
    ASSERT_TRUE(res_get_renamed != nullptr);
    EXPECT_EQ(nlohmann::json::parse(res_get_renamed->body).value("title", ""), "My Renamed Session");

    // GET /session/:id/stats
    auto res_stats = client_->Get("/session/" + session_id + "/stats");
    ASSERT_TRUE(res_stats != nullptr);
    EXPECT_EQ(res_stats->status, 200);
    auto j_stats = nlohmann::json::parse(res_stats->body);
    EXPECT_EQ(j_stats.value("id", ""), session_id);
    EXPECT_EQ(j_stats.value("title", ""), "My Renamed Session");
    EXPECT_EQ(j_stats.value("provider", ""), "mock-provider");
    EXPECT_EQ(j_stats.value("model", ""), "mock-model");
    ASSERT_TRUE(j_stats.contains("usage"));
    EXPECT_EQ(j_stats["usage"].value("model_calls", -1), 0);
    ASSERT_TRUE(j_stats["model_info"].is_object());
    EXPECT_EQ(j_stats["model_info"].value("id", ""), "mock-model");

    // Per-call usage: each call keeps the price of the model that served it.
    {
        qcode::ModelInfo opus;
        opus.id = "opus";
        opus.input_cost = 4.0;
        opus.output_cost = 20.0;
        qcode::ModelInfo haiku;
        haiku.id = "haiku";
        haiku.input_cost = 0.1;
        haiku.output_cost = 0.5;
        qcode::session::ModelCallUsage first{.model_ms = 2000.0, .ttft_ms = 500.0,
                                             .input_tokens = 1000000, .output_tokens = 10000,
                                             .effort = "high", .variant = "high",
                                             .provider = "mock-provider", .model = "opus", .cost = {}};
        first.cost = qcode::session::price_call(first, opus);
        qcode::session::record_session_model_call(session_id, first);
        qcode::session::ModelCallUsage second{.model_ms = 1000.0, .ttft_ms = -1.0,
                                              .input_tokens = 2000, .output_tokens = 1000,
                                              .effort = "low", .variant = "low",
                                              .provider = "mock-provider", .model = "haiku", .cost = {}};
        second.cost = qcode::session::price_call(second, haiku);
        qcode::session::record_session_model_call(session_id, second);
    }
    auto res_usage = client_->Get("/session/" + session_id + "/stats");
    ASSERT_TRUE(res_usage != nullptr);
    ASSERT_EQ(res_usage->status, 200);
    const auto j_usage = nlohmann::json::parse(res_usage->body);
    const auto& usage = j_usage["usage"];
    EXPECT_EQ(usage.value("model_calls", 0), 2);
    // opus: $4 + $0.2; haiku: $0.0002 + $0.0005
    EXPECT_NEAR(usage["session_cost"]["total"].get<double>(), 4.2007, 1e-9);
    EXPECT_FALSE(usage["session_cost"]["estimated"].get<bool>());
    EXPECT_TRUE(usage["by_model"].contains("mock-provider/opus"));
    EXPECT_TRUE(usage["by_model"].contains("mock-provider/haiku"));
    EXPECT_DOUBLE_EQ(usage["avg_call_ms"].get<double>(), 1500.0);
    EXPECT_EQ(j_usage["context"].value("used", 0), 2000);  // latest prompt
    EXPECT_EQ(j_usage["context"].value("window", -1), 0);  // no limit.context

    // GET /session/:id/messages
    qcode::session::save_message(session_id, "user", "msg1");
    qcode::session::save_message(session_id, "assistant", "msg2");
    auto res_msgs = client_->Get("/session/" + session_id + "/messages");
    ASSERT_TRUE(res_msgs != nullptr);
    EXPECT_EQ(res_msgs->status, 200);
    EXPECT_TRUE(nlohmann::json::parse(res_msgs->body).is_array());
    EXPECT_EQ(res_msgs->body.find("[\n"), std::string::npos);  // compact JSON

    // Query parameters: tail and limit
    auto res_tail = client_->Get("/session/" + session_id + "/messages?tail=1");
    ASSERT_TRUE(res_tail != nullptr);
    EXPECT_EQ(res_tail->status, 200);
    auto j_tail = nlohmann::json::parse(res_tail->body);
    ASSERT_TRUE(j_tail.is_array());
    EXPECT_EQ(j_tail.size(), 1u);
    EXPECT_EQ(j_tail[0]["content"], "msg2");

    auto res_limit = client_->Get("/session/" + session_id + "/messages?limit=1");
    ASSERT_TRUE(res_limit != nullptr);
    EXPECT_EQ(res_limit->status, 200);
    auto j_limit = nlohmann::json::parse(res_limit->body);
    ASSERT_TRUE(j_limit.is_array());
    EXPECT_EQ(j_limit.size(), 1u);
    EXPECT_EQ(j_limit[0]["content"], "msg1");

    // POST /session/:id/clear
    auto res_clear = client_->Post("/session/" + session_id + "/clear", "{}", "application/json");
    ASSERT_TRUE(res_clear != nullptr);
    EXPECT_EQ(res_clear->status, 200);

    // DELETE /session/:id
    auto res_del = client_->Delete("/session/" + session_id);
    ASSERT_TRUE(res_del != nullptr);
    EXPECT_EQ(res_del->status, 200);

    // Verify 404 after deletion
    auto res_after = client_->Get("/session/" + session_id);
    ASSERT_TRUE(res_after != nullptr);
    EXPECT_EQ(res_after->status, 404);
}

TEST_F(ServerRoutesTest, WorkspaceFsOperations) {
    nlohmann::json req_create = {
        {"provider", "mock-provider"},
        {"model", "mock-model"},
        {"workspace", test_workspace_dir_}
    };
    auto res_create = client_->Post("/sessions", req_create.dump(), "application/json");
    ASSERT_TRUE(res_create != nullptr);
    std::string sid = nlohmann::json::parse(res_create->body).value("id", "");
    ASSERT_FALSE(sid.empty());

    // PUT /session/:id/fs/write
    nlohmann::json req_write = {
        {"path", "note.txt"},
        {"content", "Hello WebUI Integration!"}
    };
    auto res_write = client_->Put("/session/" + sid + "/fs/write", req_write.dump(), "application/json");
    ASSERT_TRUE(res_write != nullptr);
    EXPECT_EQ(res_write->status, 200);

    // GET /session/:id/fs/exists
    auto res_exists = client_->Get("/session/" + sid + "/fs/exists?path=note.txt");
    ASSERT_TRUE(res_exists != nullptr);
    EXPECT_EQ(res_exists->status, 200);
    EXPECT_TRUE(nlohmann::json::parse(res_exists->body).value("exists", false));

    // GET /session/:id/fs/read
    auto res_read = client_->Get("/session/" + sid + "/fs/read?path=note.txt");
    ASSERT_TRUE(res_read != nullptr);
    EXPECT_EQ(res_read->status, 200);
    EXPECT_EQ(nlohmann::json::parse(res_read->body).value("content", ""), "Hello WebUI Integration!");

    // GET /session/:id/fs/list
    auto res_list = client_->Get("/session/" + sid + "/fs/list");
    ASSERT_TRUE(res_list != nullptr);
    EXPECT_EQ(res_list->status, 200);
    auto j_list = nlohmann::json::parse(res_list->body);
    auto entries = j_list.value("entries", nlohmann::json::array());
    bool found_note = false;
    for (const auto& e : entries) {
        if (e.value("name", "") == "note.txt") found_note = true;
    }
    EXPECT_TRUE(found_note);

    // Path traversal rejection
    auto res_escape_read = client_->Get("/session/" + sid + "/fs/read?path=../../etc/passwd");
    ASSERT_TRUE(res_escape_read != nullptr);
    EXPECT_EQ(res_escape_read->status, 400);

    nlohmann::json req_escape_write = {
        {"path", "../escape.txt"},
        {"content", "bad"}
    };
    auto res_escape_write = client_->Put("/session/" + sid + "/fs/write", req_escape_write.dump(), "application/json");
    ASSERT_TRUE(res_escape_write != nullptr);
    EXPECT_EQ(res_escape_write->status, 400);
}

// T24: a session whose workspace was moved/deleted must report "folder not
// found" on the Changes endpoint (same as the Explorer), not "not a git repo".
TEST_F(ServerRoutesTest, GitChangesReportsMissingWorkspaceAsNotFound) {
    nlohmann::json req_create = {
        {"provider", "mock-provider"},
        {"model", "mock-model"},
        {"workspace", test_workspace_dir_}
    };
    auto res_create = client_->Post("/sessions", req_create.dump(), "application/json");
    ASSERT_TRUE(res_create != nullptr);
    ASSERT_EQ(res_create->status, 200);
    std::string sid = nlohmann::json::parse(res_create->body).value("id", "");
    ASSERT_FALSE(sid.empty());

    // Workspace exists: the Changes endpoint answers normally.
    auto res_ok = client_->Get("/session/" + sid + "/files");
    ASSERT_TRUE(res_ok != nullptr);
    EXPECT_EQ(res_ok->status, 200);

    // Simulate the workspace being moved away.
    std::error_code ec;
    fs::remove_all(test_workspace_dir_, ec);
    ASSERT_FALSE(fs::exists(test_workspace_dir_));

    auto res_missing = client_->Get("/session/" + sid + "/files");
    ASSERT_TRUE(res_missing != nullptr);
    EXPECT_EQ(res_missing->status, 404);
    auto j = nlohmann::json::parse(res_missing->body);
    EXPECT_EQ(j.value("error", ""), "directory not found");
    EXPECT_EQ(j.value("workspace", ""), test_workspace_dir_);
    EXPECT_FALSE(j.value("is_git_repo", true));

    // Recreate for TearDown's cleanup symmetry.
    fs::create_directories(test_workspace_dir_);
}

TEST_F(ServerRoutesTest, TasksAndSubagentSessionLookup) {
    auto res_tasks = client_->Get("/tasks");
    ASSERT_TRUE(res_tasks);
    EXPECT_EQ(res_tasks->status, 200);
    auto tasks_json = nlohmann::json::parse(res_tasks->body);
    ASSERT_TRUE(tasks_json.contains("metadata"));
    ASSERT_TRUE(tasks_json["metadata"].contains("tasks"));
    EXPECT_TRUE(tasks_json["metadata"]["tasks"].is_array());

    qcode::session::ensure_session_row(
        "ses_child_webui_test", "Child explore", "mock-provider", "mock-model",
        test_workspace_dir_);

    auto res_parents = client_->Get("/sessions");
    ASSERT_TRUE(res_parents);
    EXPECT_EQ(res_parents->status, 200);
    auto parents = nlohmann::json::parse(res_parents->body);
    bool parent_has_child = false;
    for (const auto& s : parents) {
        if (s.value("id", "") == "ses_child_webui_test") parent_has_child = true;
    }
    EXPECT_FALSE(parent_has_child);

    auto res_all = client_->Get("/sessions?include_subagents=1");
    ASSERT_TRUE(res_all);
    EXPECT_EQ(res_all->status, 200);
    auto all = nlohmann::json::parse(res_all->body);
    bool listed = false;
    for (const auto& s : all) {
        if (s.value("id", "") == "ses_child_webui_test") {
            listed = true;
            EXPECT_EQ(s.value("agent_mode", ""), "subagent");
        }
    }
    EXPECT_TRUE(listed);

    auto res_get = client_->Get("/session/ses_child_webui_test");
    ASSERT_TRUE(res_get);
    EXPECT_EQ(res_get->status, 200);
    auto info = nlohmann::json::parse(res_get->body);
    EXPECT_EQ(info.value("agent_mode", ""), "subagent");
    EXPECT_EQ(info.value("title", ""), "Child explore");

    auto res_create = client_->Post(
        "/sessions",
        nlohmann::json({{"provider", "mock-provider"}, {"model", "mock-model"}}).dump(),
        "application/json");
    ASSERT_TRUE(res_create);
    ASSERT_EQ(res_create->status, 200);
    std::string sid = nlohmann::json::parse(res_create->body).value("id", "");
    ASSERT_FALSE(sid.empty());

    // Mode switch route updates agent_mode and reasoning_mode. Only normal
    // and orchestrator exist; the retired "autonomous" maps to orchestrator.
    auto res_mode = client_->Post(
        "/session/" + sid + "/mode",
        nlohmann::json({{"agent_mode", "autonomous"}}).dump(),
        "application/json");
    ASSERT_TRUE(res_mode);
    EXPECT_EQ(res_mode->status, 200);
    auto after = nlohmann::json::parse(client_->Get("/session/" + sid)->body);
    EXPECT_EQ(after.value("agent_mode", ""), "orchestrator");

    auto res_normal = client_->Post(
        "/session/" + sid + "/mode",
        nlohmann::json({{"agent_mode", "plan"}}).dump(),
        "application/json");
    ASSERT_TRUE(res_normal);
    auto after_normal = nlohmann::json::parse(client_->Get("/session/" + sid)->body);
    EXPECT_EQ(after_normal.value("agent_mode", ""), "normal");
    EXPECT_TRUE(after.contains("reasoning_mode"));
}


TEST_F(ServerRoutesTest, SessionLogsEndpointReturnsIsolatedSessionLogs) {
    std::string test_log_dir = "/tmp/qcode_routes_log_test_" + std::to_string(getpid()) + "_" +
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    fs::create_directories(test_log_dir, ec);

    qcode::server::g_session_logger = qcode::install_session_file_logger(
        test_log_dir, "qcode-server", qcode::logger::LogLevel::kLogLevelDebug);

    // Create session 1
    auto res_create1 = client_->Post(
        "/sessions",
        nlohmann::json({{"provider", "mock-provider"}, {"model", "mock-model"}}).dump(),
        "application/json");
    ASSERT_TRUE(res_create1);
    ASSERT_EQ(res_create1->status, 200);
    std::string sid1 = nlohmann::json::parse(res_create1->body).value("id", "");
    ASSERT_FALSE(sid1.empty());

    // Create session 2
    auto res_create2 = client_->Post(
        "/sessions",
        nlohmann::json({{"provider", "mock-provider"}, {"model", "mock-model"}}).dump(),
        "application/json");
    ASSERT_TRUE(res_create2);
    ASSERT_EQ(res_create2->status, 200);
    std::string sid2 = nlohmann::json::parse(res_create2->body).value("id", "");
    ASSERT_FALSE(sid2.empty());

    // Emit logs under sid1
    qcode::logger::set_thread_session_id(sid1);
    LOG_INFO("UniqueLogMessageForSessionOne_12345");

    // Emit logs under sid2
    qcode::logger::set_thread_session_id(sid2);
    LOG_INFO("UniqueLogMessageForSessionTwo_67890");

    // Emit unscoped server log
    qcode::logger::set_thread_session_id("");
    LOG_INFO("UniqueLogMessageForUnscopedServer_99999");

    // Query sid1 logs via JSON
    auto res_log1 = client_->Get("/session/" + sid1 + "/logs");
    ASSERT_TRUE(res_log1);
    EXPECT_EQ(res_log1->status, 200);
    auto log1_json = nlohmann::json::parse(res_log1->body);
    EXPECT_EQ(log1_json.value("session_id", ""), sid1);
    std::string log1_text = log1_json.value("log", "");
    EXPECT_NE(log1_text.find("UniqueLogMessageForSessionOne_12345"), std::string::npos);
    EXPECT_EQ(log1_text.find("UniqueLogMessageForSessionTwo_67890"), std::string::npos);
    EXPECT_EQ(log1_text.find("UniqueLogMessageForUnscopedServer_99999"), std::string::npos);

    // Query sid2 logs via raw format
    auto res_log2 = client_->Get("/session/" + sid2 + "/logs?raw=1");
    ASSERT_TRUE(res_log2);
    EXPECT_EQ(res_log2->status, 200);
    EXPECT_NE(res_log2->body.find("UniqueLogMessageForSessionTwo_67890"), std::string::npos);
    EXPECT_EQ(res_log2->body.find("UniqueLogMessageForSessionOne_12345"), std::string::npos);

    // Query unscoped server logs
    auto res_server_log = client_->Get("/logs");
    ASSERT_TRUE(res_server_log);
    EXPECT_EQ(res_server_log->status, 200);
    auto server_log_json = nlohmann::json::parse(res_server_log->body);
    std::string server_text = server_log_json.value("log", "");
    EXPECT_NE(server_text.find("UniqueLogMessageForUnscopedServer_99999"), std::string::npos);
    EXPECT_EQ(server_text.find("UniqueLogMessageForSessionOne_12345"), std::string::npos);

    // Query 404 for non-existent session
    auto res_log_none = client_->Get("/session/non_existent_sid_123/logs");
    ASSERT_TRUE(res_log_none);
    EXPECT_EQ(res_log_none->status, 404);

    // Delete session 1 and check that logger pruned its in-memory sink
    auto res_del = client_->Delete("/session/" + sid1);
    ASSERT_TRUE(res_del);
    EXPECT_EQ(res_del->status, 200);
    EXPECT_FALSE(qcode::server::g_session_logger->has_open_sink(sid1));

    qcode::server::g_session_logger->close_all();
    qcode::server::g_session_logger.reset();
    fs::remove_all(test_log_dir, ec);
}

TEST_F(ServerRoutesTest, UnknownModelIsRejectedInsteadOfSubstituted) {
    // A model name that is not in the provider's catalog must fail loudly.
    // Falling back to the provider's first model silently answers with
    // someone else's model (and breaks the prompt-cache prefix).
    nlohmann::json bad = {
        {"provider", "mock-provider"},
        {"model", "no-such-model"},
        {"workspace", test_workspace_dir_}
    };
    auto res_bad = client_->Post("/sessions", bad.dump(), "application/json");
    ASSERT_TRUE(res_bad != nullptr);
    EXPECT_EQ(res_bad->status, 400);
    auto j_bad = nlohmann::json::parse(res_bad->body);
    EXPECT_EQ(j_bad.value("provider", ""), "mock-provider");
    ASSERT_TRUE(j_bad["models"].is_array());
    EXPECT_EQ(j_bad["models"].size(), 1u);
    EXPECT_EQ(j_bad["models"][0].get<std::string>(), "mock-model");
    EXPECT_TRUE(j_bad.value("error", "").find("no-such-model") != std::string::npos);

    // No session was created by the rejected request.
    auto res_list = client_->Get("/sessions");
    ASSERT_TRUE(res_list != nullptr);
    EXPECT_EQ(nlohmann::json::parse(res_list->body).size(), 0);

    // An omitted model is still rejected as incomplete: this route requires both.
    nlohmann::json omitted = {
        {"provider", "mock-provider"},
        {"workspace", test_workspace_dir_}
    };
    auto res_omitted = client_->Post("/sessions", omitted.dump(), "application/json");
    ASSERT_TRUE(res_omitted != nullptr);
    EXPECT_EQ(res_omitted->status, 400);
    EXPECT_EQ(nlohmann::json::parse(res_omitted->body).value("error", ""),
              "provider and model required");

    // A provider that is not configured is rejected here too (it used to be
    // stored, and only failed later at generate time).
    nlohmann::json bad_provider = {
        {"provider", "ghost-provider"},
        {"model", "whatever"},
        {"workspace", test_workspace_dir_}
    };
    auto res_ghost = client_->Post("/sessions", bad_provider.dump(), "application/json");
    ASSERT_TRUE(res_ghost != nullptr);
    EXPECT_EQ(res_ghost->status, 400);
    EXPECT_TRUE(nlohmann::json::parse(res_ghost->body).value("error", "")
                    .find("ghost-provider") != std::string::npos);

    // The valid model still works, and no session leaked from the failures.
    nlohmann::json ok = {
        {"provider", "mock-provider"},
        {"model", "mock-model"},
        {"workspace", test_workspace_dir_}
    };
    auto res_ok = client_->Post("/sessions", ok.dump(), "application/json");
    ASSERT_TRUE(res_ok != nullptr);
    EXPECT_EQ(res_ok->status, 200);
    EXPECT_EQ(nlohmann::json::parse(res_ok->body).value("title", ""), "Session - mock-model");

    // Resolving by the model's display name keeps working (session rows store
    // display names, e.g. compaction resolving "MiMo-V2.6-Flash Free").
    nlohmann::json by_name = {
        {"provider", "mock-provider"},
        {"model", "Mock Model"},
        {"workspace", test_workspace_dir_}
    };
    auto res_name = client_->Post("/sessions", by_name.dump(), "application/json");
    ASSERT_TRUE(res_name != nullptr);
    EXPECT_EQ(res_name->status, 200);
    // The stored display name is kept verbatim in the title.
    EXPECT_EQ(nlohmann::json::parse(res_name->body).value("title", ""),
              "Session - Mock Model");
}

// ---- Razorpay Standard Checkout ------------------------------------------
// The secret below is a fixed dummy used only by these tests. The signature is
// HMAC_SHA256("order_TestOrder01|pay_TestPayment01", "unit-test-secret"),
// computed independently of the implementation.

namespace {
constexpr const char* kTestKeySecret = "unit-test-secret";
constexpr const char* kTestOrderId = "order_TestOrder01";
constexpr const char* kTestPaymentId = "pay_TestPayment01";
constexpr const char* kTestSignature = "f35683e148febf80ae3135b76a1c8358cf182fca8832f65cb7f6852c01a2205a";
}  // namespace

TEST(RazorpaySignatureTest, MatchesReferenceHmacAndRejectsOtherInputs) {
    using qcode::server::razorpay_signature_matches;
    EXPECT_TRUE(razorpay_signature_matches(kTestKeySecret, kTestOrderId, kTestPaymentId, kTestSignature));
    EXPECT_FALSE(razorpay_signature_matches("other-secret", kTestOrderId, kTestPaymentId, kTestSignature));
    EXPECT_FALSE(razorpay_signature_matches(kTestKeySecret, kTestOrderId, "pay_Other", kTestSignature));
    EXPECT_FALSE(razorpay_signature_matches(kTestKeySecret, "order_Other", kTestPaymentId, kTestSignature));
    EXPECT_FALSE(razorpay_signature_matches(kTestKeySecret, kTestOrderId, kTestPaymentId, ""));
    EXPECT_FALSE(razorpay_signature_matches(kTestKeySecret, kTestOrderId, kTestPaymentId,
                                            std::string(kTestSignature, 63)));
}

class PaymentRoutesTest : public ServerRoutesTest {
protected:
    void SetUp() override {
        ServerRoutesTest::SetUp();
        setenv("RAZORPAY_KEY_ID", "rzp_test_unit", 1);
        setenv("RAZORPAY_KEY_SECRET", kTestKeySecret, 1);
    }
    void TearDown() override {
        unsetenv("RAZORPAY_KEY_ID");
        unsetenv("RAZORPAY_KEY_SECRET");
        ServerRoutesTest::TearDown();
    }

    static std::string verify_body(const std::string& signature) {
        return nlohmann::json({{"razorpay_order_id", kTestOrderId},
                               {"razorpay_payment_id", kTestPaymentId},
                               {"razorpay_signature", signature}})
            .dump();
    }
};

TEST_F(PaymentRoutesTest, ConfigExposesKeyIdButNeverTheSecret) {
    auto res = client_->Get("/api/payments/config");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(nlohmann::json::parse(res->body).at("key_id"), "rzp_test_unit");
    EXPECT_EQ(res->body.find(kTestKeySecret), std::string::npos);
}

TEST_F(PaymentRoutesTest, ConfigFailsWhenKeyIdMissing) {
    unsetenv("RAZORPAY_KEY_ID");
    auto res = client_->Get("/api/payments/config");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 500);
}

TEST_F(PaymentRoutesTest, CreateOrderValidatesAmountBeforeCallingRazorpay) {
    const std::vector<std::string> bad_bodies = {
        "{\"amount\":99}",
        "{\"amount\":0}",
        "{\"amount\":-500}",
        "{\"amount\":100.5}",
        "{\"amount\":\"100\"}",
        "{}",
        "{\"amount\":100,\"currency\":\"inr\"}",
        "{\"amount\":100,\"currency\":\"INRR\"}",
        "{\"amount\":100,\"receipt\":\"" + std::string(41, 'r') + "\"}",
    };
    for (const auto& json_body : bad_bodies) {
        SCOPED_TRACE(json_body);
        auto res = client_->Post("/api/create-order", json_body, "application/json");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 400);
    }
}

TEST_F(PaymentRoutesTest, CreateOrderFailsWithoutCredentials) {
    unsetenv("RAZORPAY_KEY_SECRET");
    auto res = client_->Post("/api/create-order", "{\"amount\":100}", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 500);
    EXPECT_EQ(res->body.find(kTestKeySecret), std::string::npos);
}

TEST_F(PaymentRoutesTest, VerifyAcceptsMatchingSignature) {
    auto res = client_->Post("/api/verify-payment", verify_body(kTestSignature), "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    const auto parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed.at("status"), "verified");
    EXPECT_EQ(parsed.at("payment_id"), kTestPaymentId);
}

TEST_F(PaymentRoutesTest, VerifyRejectsSignatureMismatchAsNotPaid) {
    const std::string wrong = std::string(64, '0');
    auto res = client_->Post("/api/verify-payment", verify_body(wrong), "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const auto parsed = nlohmann::json::parse(res->body);
    EXPECT_TRUE(parsed.contains("error"));
    EXPECT_FALSE(parsed.contains("status"));
    EXPECT_EQ(res->body.find("verified"), std::string::npos);
}

TEST_F(PaymentRoutesTest, VerifyRejectsMissingOrEmptyFields) {
    auto missing = client_->Post(
        "/api/verify-payment",
        nlohmann::json({{"razorpay_order_id", kTestOrderId}, {"razorpay_payment_id", kTestPaymentId}}).dump(),
        "application/json");
    ASSERT_TRUE(missing != nullptr);
    EXPECT_EQ(missing->status, 400);

    auto empty = client_->Post("/api/verify-payment", verify_body(""), "application/json");
    ASSERT_TRUE(empty != nullptr);
    EXPECT_EQ(empty->status, 400);
}

TEST_F(PaymentRoutesTest, VerifyFailsWithoutSecret) {
    unsetenv("RAZORPAY_KEY_SECRET");
    auto res = client_->Post("/api/verify-payment", verify_body(kTestSignature), "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 500);
}

TEST_F(PaymentRoutesTest, CheckoutPageAndScriptAreServed) {
    auto page = client_->Get("/checkout.html");
    ASSERT_TRUE(page != nullptr);
    EXPECT_EQ(page->status, 200);
    EXPECT_NE(page->body.find("https://checkout.razorpay.com/v1/checkout.js"), std::string::npos);
    EXPECT_EQ(page->body.find(kTestKeySecret), std::string::npos);

    auto script = client_->Get("/checkout.js");
    ASSERT_TRUE(script != nullptr);
    EXPECT_EQ(script->status, 200);
}
