#include <ai/tui/db.h>

#include <sqlite3.h>
#include <uuid.h>
#include <cstdlib>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <mutex>
#include <cctype>

#include <ai/logger.h>
#include <ai/types/message.h>
#include <nlohmann/json.hpp>

namespace ai {
namespace tui {
namespace db {

static std::string get_db_path() {
    // Configurable via QCODE_DB_PATH / OPENCODE_DB_PATH; falls back to ~/.qcode.db.
    if (const char* p = std::getenv("QCODE_DB_PATH")) return p;
    if (const char* p = std::getenv("OPENCODE_DB_PATH")) return p;
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.qcode.db" : ".qcode.db";
}

// Open the database, enabling foreign-key enforcement on EVERY connection (PRAGMA
// foreign_keys is per-connection and not persisted), and report failures at ERROR
// level. Returns nullptr on failure.
static sqlite3* open_database(std::string& out_path) {
    std::string path = get_db_path();
    out_path = path;
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQLite: failed to open database at {}: {}", path,
                  db ? sqlite3_errmsg(db) : "unknown error");
        if (db) sqlite3_close(db);
        return nullptr;
    }
    // Contended writers (UI + background tool saves) need a real busy timeout
    // and WAL, otherwise prepare/insert intermittently fail with "database is locked".
    sqlite3_busy_timeout(db, 5000);
    char* pragma_err = nullptr;
    if (sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr,
                     &pragma_err) != SQLITE_OK) {
        LOG_WARN("SQLite: WAL mode unavailable: {}",
                 pragma_err ? pragma_err : "unknown");
        sqlite3_free(pragma_err);
    }
    pragma_err = nullptr;
    if (sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr,
                     &pragma_err) != SQLITE_OK) {
        sqlite3_free(pragma_err);
    }
    char* fk_err = nullptr;
    if (sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &fk_err) != SQLITE_OK) {
        LOG_ERROR("SQLite: failed to enable foreign keys: {}", fk_err ? fk_err : "unknown");
        sqlite3_free(fk_err);
    }
    return db;
}

static bool prepare_stmt(sqlite3* db, const char* sql, sqlite3_stmt** stmt) {
    if (sqlite3_prepare_v2(db, sql, -1, stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("SQLite: prepare failed: {}", sqlite3_errmsg(db));
        return false;
    }
    return true;
}

class MessageWriterConnection final {
public:
    MessageWriterConnection() {
        std::string path;
        db_ = open_database(path);
        if (!db_) return;
        sqlite3_busy_timeout(db_, 2000);
        constexpr auto kInsertSql =
            "INSERT INTO messages (session_id, sender, content, created_at) "
            "VALUES (?, ?, ?, ?);";
        if (!prepare_stmt(db_, kInsertSql, &insert_)) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    ~MessageWriterConnection() {
        if (insert_) sqlite3_finalize(insert_);
        if (db_) sqlite3_close(db_);
    }

    MessageWriterConnection(const MessageWriterConnection&) = delete;
    MessageWriterConnection& operator=(const MessageWriterConnection&) = delete;

    void write(const std::string& session_id, const std::string& sender,
               const std::string& content, long long created_at) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!insert_) return;
        sqlite3_reset(insert_);
        sqlite3_clear_bindings(insert_);
        sqlite3_bind_text(
            insert_, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_, 2, sender.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_, 3, content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(insert_, 4, created_at);
        if (sqlite3_step(insert_) != SQLITE_DONE) {
            LOG_ERROR("SQLite: message insert failed: {}", sqlite3_errmsg(db_));
        }
    }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* insert_ = nullptr;
    std::mutex mutex_;
};

static std::string generate_uuid() {
    // Thread-safe RFC 4122 v4 UUID via the vendored stduuid library. The previous
    // hand-rolled generator shared a single mt19937 across threads without locking.
    static thread_local std::mt19937 engine{std::random_device{}()};
    static thread_local uuids::uuid_random_generator gen{engine};
    return uuids::to_string(gen());
}

void init_database() {
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return;

    // Idempotent schema migrations, guarded by PRAGMA user_version.
    int user_version = 0;
    {
        sqlite3_stmt* vstmt = nullptr;
        if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &vstmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(vstmt) == SQLITE_ROW) user_version = sqlite3_column_int(vstmt, 0);
            sqlite3_finalize(vstmt);
        }
    }
    const int kSchemaVersion = 2;
    if (user_version < kSchemaVersion) {
        char* err_msg = nullptr;
        if (sqlite3_exec(db, "BEGIN;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            LOG_ERROR("SQLite: failed to begin schema transaction: {}", err_msg ? err_msg : "unknown");
            sqlite3_free(err_msg); err_msg = nullptr;
        }

        const char* schema_sessions =
            "CREATE TABLE IF NOT EXISTS sessions ("
            "  id TEXT PRIMARY KEY,"
            "  title TEXT,"
            "  provider TEXT,"
            "  model TEXT,"
            "  created_at INTEGER,"
            "  workspace TEXT DEFAULT ''"
            ");";
        if (sqlite3_exec(db, schema_sessions, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            LOG_ERROR("SQLite: create sessions table error: {}", err_msg ? err_msg : "unknown");
            sqlite3_free(err_msg); err_msg = nullptr;
        }

        const char* schema_messages =
            "CREATE TABLE IF NOT EXISTS messages ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  session_id TEXT,"
            "  sender TEXT,"
            "  content TEXT,"
            "  created_at INTEGER,"
            "  FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE"
            ");";
        if (sqlite3_exec(db, schema_messages, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            LOG_ERROR("SQLite: create messages table error: {}", err_msg ? err_msg : "unknown");
            sqlite3_free(err_msg); err_msg = nullptr;
        }

        if (sqlite3_exec(db, "PRAGMA user_version = 1;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            LOG_ERROR("SQLite: failed to set schema version: {}", err_msg ? err_msg : "unknown");
            sqlite3_free(err_msg); err_msg = nullptr;
        }
        if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            LOG_ERROR("SQLite: failed to commit schema transaction: {}", err_msg ? err_msg : "unknown");
            sqlite3_free(err_msg); err_msg = nullptr;
        }
    }

    // ── Migration v1 → v2: add workspace column ──
    if (user_version < 2) {
        sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "ALTER TABLE sessions ADD COLUMN workspace TEXT DEFAULT '';",
                     nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA user_version = 2;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    }

    LOG_INFO("SQLite: database opened successfully at {}", path);
    sqlite3_close(db);
}

std::string create_new_session(const std::string& provider, const std::string& model,
                               const std::string& workspace) {
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return "";
    }

    std::string uuid = generate_uuid();
    auto now = std::chrono::system_clock::now();
    long long created_at = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    
    // Default title is "Session - <model_name>"
    std::string title = "Session - " + model;

    const char* sql = "INSERT INTO sessions (id, title, provider, model, created_at, workspace) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, provider.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, model.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 5, created_at);
        sqlite3_bind_text(stmt, 6, workspace.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return uuid;
}

std::string get_last_active_session() {
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return "";
    }

    std::string uuid = "";
    const char* sql = "SELECT id FROM sessions ORDER BY created_at DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) uuid = reinterpret_cast<const char*>(val);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return uuid;
}

bool is_valid_session_id(const std::string& id) {
    // RFC 4122 v4 canonical form: 8-4-4-4-12 lowercase hex with dashes at 8/13/18/23.
    if (id.size() != 36) return false;
    constexpr int kDashPos[4] = {8, 13, 18, 23};
    for (int i = 0; i < 4; ++i) {
        if (id[kDashPos[i]] != '-') return false;
    }
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        const char c = id[i];
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

void save_message(const std::string& session_id, const std::string& sender, const std::string& content) {
    if (session_id.empty() || !is_valid_session_id(session_id)) {
        LOG_WARN("SQLite: refusing operation with invalid session id '{}'", session_id);
        return;
    }

    auto now = std::chrono::system_clock::now();
    long long created_at = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    static MessageWriterConnection writer;
    writer.write(session_id, sender, content, created_at);
}

void reload_session_history(const std::string& session_id, ChatState& state) {
    if (session_id.empty() || !is_valid_session_id(session_id)) {
        LOG_WARN("SQLite: refusing operation with invalid session id '{}'", session_id);
        return;
    }

    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return;
    }

    state.messages_history->clear();

    const char* sql = "SELECT sender, content FROM messages WHERE session_id = ? ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;
    int legacy_tool_seq = 0;
    if (prepare_stmt(db, sql, &stmt)) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* sender_txt = sqlite3_column_text(stmt, 0);
            const unsigned char* content_txt = sqlite3_column_text(stmt, 1);

            std::string sender = sender_txt ? reinterpret_cast<const char*>(sender_txt) : "";
            std::string content = content_txt ? reinterpret_cast<const char*>(content_txt) : "";

            if (sender == "User") {
                state.messages_history->push_back(ai::Message::user(content));
                continue;
            }
            if (sender == "Assistant") {
                state.messages_history->push_back(ai::Message::assistant(content));
                continue;
            }

            // Structured tool rows (and legacy text forms) → pretty ToolBlocks.
            if (sender == "ToolCall") {
                try {
                    auto j = nlohmann::json::parse(content);
                    if (j.is_object() && j.contains("id") && j.contains("name")) {
                        auto args = j.value("arguments", nlohmann::json::object());
                        state.messages_history->push_back(
                            ai::Message::assistant_with_tools(
                                "",
                                {{j.at("id").get<std::string>(),
                                  j.at("name").get<std::string>(),
                                  std::move(args)}}));
                        continue;
                    }
                } catch (...) {
                }
                // Legacy: bash({"command":"..."})
                const auto paren = content.find('(');
                if (paren != std::string::npos && content.back() == ')') {
                    try {
                        auto args = nlohmann::json::parse(
                            content.substr(paren + 1,
                                           content.size() - paren - 2));
                        const std::string tool_name = content.substr(0, paren);
                        const std::string id =
                            "legacy-call-" + std::to_string(++legacy_tool_seq);
                        state.messages_history->push_back(
                            ai::Message::assistant_with_tools(
                                "", {{id, tool_name, std::move(args)}}));
                        continue;
                    } catch (...) {
                    }
                }
                state.messages_history->push_back(ai::Message::system(content));
                continue;
            }

            if (sender == "ToolResult") {
                try {
                    auto j = nlohmann::json::parse(content);
                    if (j.is_object() && j.contains("tool_call_id")) {
                        auto result = j.contains("result") ? j.at("result")
                                                           : nlohmann::json();
                        state.messages_history->push_back(ai::Message::tool_results(
                            {{j.at("tool_call_id").get<std::string>(),
                              std::move(result),
                              j.value("is_error", false),
                              j.value("duration_ms", 0.0)}}));
                        continue;
                    }
                } catch (...) {
                }
                // Legacy: "  ✔ bash (4ms)" — pair with previous legacy call if possible.
                double duration_ms = 0.0;
                const auto open = content.rfind('(');
                const auto close = content.rfind(')');
                if (open != std::string::npos && close != std::string::npos &&
                    close > open) {
                    std::string dur = content.substr(open + 1, close - open - 1);
                    if (dur.size() > 2 &&
                        dur.compare(dur.size() - 2, 2, "ms") == 0) {
                        try {
                            duration_ms = std::stod(dur.substr(0, dur.size() - 2));
                        } catch (...) {
                        }
                    }
                }
                std::string call_id;
                if (!state.messages_history->empty()) {
                    const auto& prev = state.messages_history->back();
                    if (prev.has_tool_calls()) {
                        const auto calls = prev.get_tool_calls();
                        if (!calls.empty()) call_id = calls.back().id;
                    }
                }
                if (call_id.empty()) {
                    call_id = "legacy-result-" + std::to_string(++legacy_tool_seq);
                }
                const bool is_error =
                    content.find("failed") != std::string::npos ||
                    content.find("✖") != std::string::npos ||
                    content.find("✗") != std::string::npos;
                state.messages_history->push_back(ai::Message::tool_results(
                    {{call_id, nlohmann::json::object(), is_error, duration_ms}}));
                continue;
            }

            // Persisted tool/status rows are display history. They are kept
            // in the canonical UI list and filtered from model requests.
            state.messages_history->push_back(ai::Message::system(content));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

std::vector<std::pair<std::string, std::string>> load_session_messages(const std::string& session_id) {
    std::vector<std::pair<std::string, std::string>> out;
    if (session_id.empty() || !is_valid_session_id(session_id)) return out;

    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return out;

    const char* sql = "SELECT sender, content FROM messages WHERE session_id = ? ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* sender_txt = sqlite3_column_text(stmt, 0);
            const unsigned char* content_txt = sqlite3_column_text(stmt, 1);
            std::string sender = sender_txt ? reinterpret_cast<const char*>(sender_txt) : "";
            std::string content = content_txt ? reinterpret_cast<const char*>(content_txt) : "";
            out.emplace_back(std::move(sender), std::move(content));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return out;
}

std::vector<std::pair<std::string, std::string>> list_sessions() {
    std::vector<std::pair<std::string, std::string>> sessions;
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return sessions;
    }

    const char* sql = "SELECT id, title FROM sessions ORDER BY created_at DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* id_txt = sqlite3_column_text(stmt, 0);
            const unsigned char* title_txt = sqlite3_column_text(stmt, 1);
            
            std::string id = id_txt ? reinterpret_cast<const char*>(id_txt) : "";
            std::string title = title_txt ? reinterpret_cast<const char*>(title_txt) : "";
            sessions.push_back({id, title});
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return sessions;
}


std::vector<SessionInfo> list_sessions_full() {
    std::vector<SessionInfo> sessions;
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return sessions;
    }

    const char* sql = "SELECT id, title, COALESCE(workspace, ''), COALESCE(provider, ''), COALESCE(model, '') FROM sessions ORDER BY created_at DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SessionInfo info;
            const unsigned char* id_txt = sqlite3_column_text(stmt, 0);
            const unsigned char* title_txt = sqlite3_column_text(stmt, 1);
            const unsigned char* ws_txt = sqlite3_column_text(stmt, 2);
            const unsigned char* prov_txt = sqlite3_column_text(stmt, 3);
            const unsigned char* model_txt = sqlite3_column_text(stmt, 4);
            info.id = id_txt ? reinterpret_cast<const char*>(id_txt) : "";
            info.title = title_txt ? reinterpret_cast<const char*>(title_txt) : "";
            info.workspace = ws_txt ? reinterpret_cast<const char*>(ws_txt) : "";
            info.provider = prov_txt ? reinterpret_cast<const char*>(prov_txt) : "";
            info.model = model_txt ? reinterpret_cast<const char*>(model_txt) : "";
            sessions.push_back(std::move(info));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return sessions;
}

std::string get_session_workspace(const std::string& session_id) {
    if (session_id.empty() || !is_valid_session_id(session_id)) return "";
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return "";

    std::string result;
    const char* sql = "SELECT COALESCE(workspace, '') FROM sessions WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            result = val ? reinterpret_cast<const char*>(val) : "";
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return result;
}


void rename_session(const std::string& session_id, const std::string& new_title) {
    if (session_id.empty() || !is_valid_session_id(session_id)) {
        LOG_WARN("SQLite: refusing operation with invalid session id '{}'", session_id);
        return;
    }

    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return;
    }

    const char* sql = "UPDATE sessions SET title = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        sqlite3_bind_text(stmt, 1, new_title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

void set_session_provider_model(const std::string& session_id, const std::string& provider, const std::string& model) {
    if (session_id.empty() || !is_valid_session_id(session_id)) {
        LOG_WARN("SQLite: refusing operation with invalid session id '{}'", session_id);
        return;
    }

    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return;
    }

    const char* sql = "UPDATE sessions SET provider = ?, model = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        sqlite3_bind_text(stmt, 1, provider.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, model.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

void delete_session(const std::string& session_id) {
    if (session_id.empty() || !is_valid_session_id(session_id)) {
        LOG_WARN("SQLite: refusing operation with invalid session id '{}'", session_id);
        return;
    }

    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return;
    }

    // Enable foreign keys to cascade deletes
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    const char* sql = "DELETE FROM sessions WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

} // namespace db
} // namespace tui
} // namespace ai
