#include "session/session_db_internal.h"
#include <gtest/gtest.h>
#include <httplib.h>

#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>
#include <qcode/session/session_store.h>
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

    // GET /session/:id/messages
    auto res_msgs = client_->Get("/session/" + session_id + "/messages");
    ASSERT_TRUE(res_msgs != nullptr);
    EXPECT_EQ(res_msgs->status, 200);
    EXPECT_TRUE(nlohmann::json::parse(res_msgs->body).is_array());

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

TEST_F(ServerRoutesTest, TerminalLifecycleAndResize) {
    nlohmann::json req_term = {
        {"workspace", test_workspace_dir_},
        {"cols", 80},
        {"rows", 24}
    };
    auto res_create = client_->Post("/terminal/create", req_term.dump(), "application/json");
    ASSERT_TRUE(res_create != nullptr);
    EXPECT_EQ(res_create->status, 200);
    auto j_term = nlohmann::json::parse(res_create->body);
    std::string term_id = j_term.value("id", "");
    ASSERT_FALSE(term_id.empty());

    // Input to terminal
    nlohmann::json req_in = {{"data", "echo hello\n"}};
    auto res_in = client_->Post("/terminal/" + term_id + "/input", req_in.dump(), "application/json");
    ASSERT_TRUE(res_in != nullptr);
    EXPECT_EQ(res_in->status, 200);

    // Resize terminal
    nlohmann::json req_resize = {{"cols", 120}, {"rows", 40}};
    auto res_resize = client_->Post("/terminal/" + term_id + "/resize", req_resize.dump(), "application/json");
    ASSERT_TRUE(res_resize != nullptr);
    EXPECT_EQ(res_resize->status, 200);

    // Delete terminal
    auto res_del = client_->Delete("/terminal/" + term_id);
    ASSERT_TRUE(res_del != nullptr);
    EXPECT_EQ(res_del->status, 200);

    // Resize non-existent terminal returns 404
    auto res_bad_resize = client_->Post("/terminal/" + term_id + "/resize", req_resize.dump(), "application/json");
    ASSERT_TRUE(res_bad_resize != nullptr);
    EXPECT_EQ(res_bad_resize->status, 404);
}
