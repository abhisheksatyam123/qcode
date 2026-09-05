#include "routes_internal.h"
#include "terminal_pty.h"

#include <fcntl.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

namespace qcode {
namespace server {

void register_terminal_routes(httplib::Server& svr) {
    // ── Terminal: create ──
    svr.Post("/terminal/create", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"invalid JSON"})", "application/json");
            return;
        }
        std::string workspace = body.value("workspace", "");
        auto ts = create_terminal(workspace);
        if (!ts) {
            res.status = 500;
            res.set_content(R"({"error":"failed to create terminal"})", "application/json");
            return;
        }
        int cols = body.value("cols", 80);
        int rows = body.value("rows", 24);
        if (cols > 0 && rows > 0) {
            resize_terminal(ts->id, cols, rows);
        }
        res.set_content(nlohmann::json({{"id", ts->id}, {"workspace", workspace}}).dump(), "application/json");
    });

    // ── Terminal: destroy ──
    svr.Delete("/terminal/([a-f0-9]+)", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        destroy_terminal(id);
        res.set_content(R"({"ok":true})", "application/json");
    });

    // ── Terminal: input (send keystrokes to PTY) ──
    svr.Post("/terminal/([a-f0-9]+)/input", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"invalid JSON"})", "application/json"); return;
        }
        std::string data = body.value("data", "");
        auto ts = find_terminal(id);
        if (!ts) { res.status = 404; res.set_content(R"({"error":"terminal not found"})", "application/json"); return; }
        if (ts->master_fd >= 0 && ts->alive) {
            (void)write(ts->master_fd, data.c_str(), data.size());
        }
        res.set_content(R"({"ok":true})", "application/json");
    });

    // ── Terminal: resize ──
    svr.Post("/terminal/([a-f0-9]+)/resize", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"invalid JSON"})", "application/json"); return;
        }
        int cols = body.value("cols", 80);
        int rows = body.value("rows", 24);
        if (!resize_terminal(id, cols, rows)) {
            res.status = 404; res.set_content(R"({"error":"terminal not found or resize failed"})", "application/json"); return;
        }
        res.set_content(R"({"ok":true})", "application/json");
    });

    // ── Terminal: output stream (long-poll read from PTY) ──
    svr.Get("/terminal/([a-f0-9]+)/stream", [](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        auto ts = find_terminal(id);
        if (!ts) { res.status = 404; res.set_content(R"({"error":"terminal not found"})", "application/json"); return; }
        // Non-blocking read from PTY master, return available data
        std::string output;
        char buf[4096];
        // Set non-blocking
        int flags = fcntl(ts->master_fd, F_GETFL, 0);
        fcntl(ts->master_fd, F_SETFL, flags | O_NONBLOCK);
        while (true) {
            ssize_t n = read(ts->master_fd, buf, sizeof(buf));
            if (n <= 0) break;
            output.append(buf, n);
        }
        // Restore blocking
        fcntl(ts->master_fd, F_SETFL, flags);
        res.set_header("Content-Type", "text/plain; charset=utf-8");
        res.set_content(output, "text/plain");
    });
}

}  // namespace server
}  // namespace qcode
