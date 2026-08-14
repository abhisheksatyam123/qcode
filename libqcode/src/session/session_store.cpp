#include <qcode/session/session_store.h>

#include <sqlite3.h>
#include <uuid.h>
#include <climits>
#include <cstdlib>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <mutex>
#include <cctype>

#include <qcode/logger/logger.h>
#include <qcode/types/message.h>
#include <nlohmann/json.hpp>

namespace qcode {
namespace session {

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
    const int kSchemaVersion = 4;
    if (user_version < 3) {
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

    // ── Migration v2 → v3: add model_capabilities and model_runtime_stats tables ──
    if (user_version < 3) {
        sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
        const char* schema_cap =
            "CREATE TABLE IF NOT EXISTS model_capabilities ("
            "  model_id TEXT PRIMARY KEY,"
            "  provider TEXT NOT NULL,"
            "  model_name TEXT NOT NULL,"
            "  architecture_info TEXT DEFAULT '',"
            "  context_window INTEGER DEFAULT 0,"
            "  output_limit INTEGER DEFAULT 0,"
            "  tool_call_supported INTEGER DEFAULT 1,"
            "  multi_turn_reliable INTEGER DEFAULT 1,"
            "  verified_benchmark TEXT DEFAULT '',"
            "  recommended_for TEXT DEFAULT ''"
            ");";
        sqlite3_exec(db, schema_cap, nullptr, nullptr, nullptr);

        const char* schema_rt =
            "CREATE TABLE IF NOT EXISTS model_runtime_stats ("
            "  model_id TEXT PRIMARY KEY,"
            "  total_turns INTEGER DEFAULT 0,"
            "  successful_turns INTEGER DEFAULT 0,"
            "  failed_turns INTEGER DEFAULT 0,"
            "  tool_loop_failures INTEGER DEFAULT 0,"
            "  avg_latency_ms REAL DEFAULT 0.0,"
            "  positive_feedback_count INTEGER DEFAULT 0,"
            "  negative_feedback_count INTEGER DEFAULT 0,"
            "  last_used INTEGER DEFAULT 0"
            ");";
        sqlite3_exec(db, schema_rt, nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA user_version = 3;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    }

    // ── Migration v3 → v4: study buddy tables ──
    if (user_version < 4) {
        sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS study_courses ("
            "  id TEXT PRIMARY KEY,"
            "  title TEXT NOT NULL,"
            "  root_path TEXT NOT NULL,"
            "  created_at INTEGER"
            ");", nullptr, nullptr, nullptr);
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS study_chapters ("
            "  id TEXT PRIMARY KEY,"
            "  course_id TEXT NOT NULL,"
            "  slug TEXT NOT NULL,"
            "  title TEXT NOT NULL,"
            "  path TEXT NOT NULL,"
            "  order_index INTEGER DEFAULT 0,"
            "  FOREIGN KEY(course_id) REFERENCES study_courses(id) ON DELETE CASCADE"
            ");", nullptr, nullptr, nullptr);
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS study_topics ("
            "  id TEXT PRIMARY KEY,"
            "  chapter_id TEXT NOT NULL,"
            "  name TEXT NOT NULL,"
            "  mastery REAL DEFAULT 0,"
            "  last_seen_at INTEGER DEFAULT 0,"
            "  FOREIGN KEY(chapter_id) REFERENCES study_chapters(id) ON DELETE CASCADE"
            ");", nullptr, nullptr, nullptr);
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS study_questions ("
            "  id TEXT PRIMARY KEY,"
            "  chapter_id TEXT NOT NULL,"
            "  topic_id TEXT,"
            "  type TEXT NOT NULL,"
            "  prompt_html TEXT NOT NULL,"
            "  choices_json TEXT DEFAULT '[]',"
            "  answer_key TEXT DEFAULT '',"
            "  difficulty INTEGER DEFAULT 1,"
            "  FOREIGN KEY(chapter_id) REFERENCES study_chapters(id) ON DELETE CASCADE,"
            "  FOREIGN KEY(topic_id) REFERENCES study_topics(id) ON DELETE SET NULL"
            ");", nullptr, nullptr, nullptr);
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS study_attempts ("
            "  id TEXT PRIMARY KEY,"
            "  question_id TEXT NOT NULL,"
            "  student_answer TEXT,"
            "  correct INTEGER DEFAULT 0,"
            "  score REAL DEFAULT 0,"
            "  feedback TEXT DEFAULT '',"
            "  created_at INTEGER,"
            "  FOREIGN KEY(question_id) REFERENCES study_questions(id) ON DELETE CASCADE"
            ");", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA user_version = 4;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        (void)kSchemaVersion;
    }

    LOG_INFO("SQLite: database opened successfully at {}", path);
    sqlite3_close(db);
    seed_model_capabilities_if_needed();
}

std::string create_new_session(const std::string& provider, const std::string& model,
                               const std::string& workspace,
                               const std::string& custom_id) {
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return "";
    }

    // Always use a UUID primary key. custom_id is the optional display title.
    const std::string final_id = generate_uuid();
    std::string title =
        custom_id.empty() ? ("Session - " + model) : custom_id;
    if (!custom_id.empty()) {
        std::string base_title = custom_id;
        int counter = 1;
        while (true) {
            const char* check_sql = "SELECT COUNT(*) FROM sessions WHERE title = ?;";
            sqlite3_stmt* check_stmt = nullptr;
            int count = 0;
            if (prepare_stmt(db, check_sql, &check_stmt)) {
                sqlite3_bind_text(check_stmt, 1, title.c_str(), -1, SQLITE_STATIC);
                if (sqlite3_step(check_stmt) == SQLITE_ROW) {
                    count = sqlite3_column_int(check_stmt, 0);
                }
                sqlite3_finalize(check_stmt);
            }
            if (count == 0) {
                break;
            }
            title = base_title + " (" + std::to_string(counter++) + ")";
        }
    }

    auto now = std::chrono::system_clock::now();
    long long created_at = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    const char* sql = "INSERT INTO sessions (id, title, provider, model, created_at, workspace) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        sqlite3_bind_text(stmt, 1, final_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, provider.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, model.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 5, created_at);
        sqlite3_bind_text(stmt, 6, workspace.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return final_id;
}

std::string get_last_active_session() {
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return "";
    }

    std::string uuid = "";
    const char* sql =
        "SELECT sessions.id "
        "FROM sessions "
        "LEFT JOIN ( "
        "    SELECT session_id, MAX(created_at) AS last_msg_time "
        "    FROM messages "
        "    GROUP BY session_id "
        ") m ON m.session_id = sessions.id "
        "ORDER BY COALESCE(m.last_msg_time, sessions.created_at) DESC "
        "LIMIT 1;";
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
    if (id.empty() || id.size() > 255) return false;
    for (char c : id) {
        if (c == '/' || c == '\\' || c == '?' || c == '#' || c == '%' || c == '*' ||
            static_cast<unsigned char>(c) < 32 || c == 127) {
            return false;
        }
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

std::vector<qcode::Message> load_session_history_parsed(const std::string& session_id) {
    std::vector<qcode::Message> history;
    if (session_id.empty() || !is_valid_session_id(session_id)) {
        LOG_WARN("SQLite: refusing operation with invalid session id '{}'", session_id);
        return history;
    }

    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return history;
    }

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
                history.push_back(qcode::Message::user(content));
                continue;
            }
            if (sender == "Assistant") {
                history.push_back(qcode::Message::assistant(content));
                continue;
            }

            // Structured tool rows (and legacy text forms) -> pretty ToolBlocks.
            if (sender == "ToolCall") {
                try {
                    auto j = nlohmann::json::parse(content);
                    if (j.is_object() && j.contains("id") && j.contains("name")) {
                        auto args = j.value("arguments", nlohmann::json::object());
                        history.push_back(
                            qcode::Message::assistant_with_tools(
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
                        history.push_back(
                            qcode::Message::assistant_with_tools(
                                "", {{id, tool_name, std::move(args)}}));
                        continue;
                    } catch (...) {
                    }
                }
                history.push_back(qcode::Message::system(content));
                continue;
            }

            if (sender == "ToolResult") {
                try {
                    auto j = nlohmann::json::parse(content);
                    if (j.is_object() && j.contains("tool_call_id")) {
                        auto result = j.contains("result") ? j.at("result")
                                                           : nlohmann::json();
                        history.push_back(qcode::Message::tool_results(
                            {{j.at("tool_call_id").get<std::string>(),
                              std::move(result),
                              j.value("is_error", false),
                              j.value("duration_ms", 0.0)}}));
                        continue;
                    }
                } catch (...) {
                }
                // Legacy: "  ✔ bash (4ms)" - pair with previous legacy call if possible.
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
                if (!history.empty()) {
                    const auto& prev = history.back();
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
                history.push_back(qcode::Message::tool_results(
                    {{call_id, nlohmann::json::object(), is_error, duration_ms}}));
                continue;
            }

            // Persisted tool/status rows are display history.
            history.push_back(qcode::Message::system(content));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return history;
}

void reload_session_history(const std::string& session_id, ChatState& state) {
    state.messages_history =
        std::make_shared<qcode::Messages>(load_session_history_parsed(session_id));
    // Live counters belong to the previous session — clear them so header/Stats
    // don't keep showing stale totals after a switch.
    if (state.total_prompt_tokens) *state.total_prompt_tokens = 0;
    if (state.total_completion_tokens) *state.total_completion_tokens = 0;
    if (state.total_tokens) *state.total_tokens = 0;
    if (state.current_context_tokens) *state.current_context_tokens = 0;
    if (state.tool_call_count) *state.tool_call_count = 0;
    if (state.total_tool_time_ms) *state.total_tool_time_ms = 0;
    if (state.last_actual_prompt_tokens) *state.last_actual_prompt_tokens = 0;
    if (state.last_estimated_tokens) *state.last_estimated_tokens = 0;
    if (state.consecutive_prunes) *state.consecutive_prunes = 0;
    if (state.history_window_start) *state.history_window_start = 0;
    if (state.auto_scroll) *state.auto_scroll = true;
    if (state.scroll_line) *state.scroll_line = INT_MAX;
    if (state.tool_collapse_state) state.tool_collapse_state->clear();
    if (state.tool_block_order) state.tool_block_order->clear();
    if (state.tool_arrow_boxes) state.tool_arrow_boxes->clear();
    state.files_detail_open = false;
}

void overwrite_session_history(const std::string& session_id, const std::vector<qcode::Message>& messages) {
    if (session_id.empty() || !is_valid_session_id(session_id)) {
        LOG_WARN("SQLite: refusing operation with invalid session id '{}'", session_id);
        return;
    }

    std::string path;
    sqlite3* db = open_database(path);
    if (!db) {
        return;
    }

    char* err_msg = nullptr;
    if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOG_ERROR("SQLite: failed to begin transaction: {}", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return;
    }

    const char* delete_sql = "DELETE FROM messages WHERE session_id = ?;";
    sqlite3_stmt* del_stmt = nullptr;
    if (prepare_stmt(db, delete_sql, &del_stmt)) {
        sqlite3_bind_text(del_stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(del_stmt);
        sqlite3_finalize(del_stmt);
    }

    const char* insert_sql =
        "INSERT INTO messages (session_id, sender, content, created_at) "
        "VALUES (?, ?, ?, ?);";
    sqlite3_stmt* ins_stmt = nullptr;
    if (prepare_stmt(db, insert_sql, &ins_stmt)) {
        auto now = std::chrono::system_clock::now();
        long long created_at = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        for (const auto& m : messages) {
            bool has_tools = false;
            bool has_results = false;
            for (const auto& part : m.content) {
                if (std::holds_alternative<qcode::ToolCallContentPart>(part)) {
                    has_tools = true;
                } else if (std::holds_alternative<qcode::ToolResultContentPart>(part)) {
                    has_results = true;
                }
            }

            if (has_tools) {
                for (const auto& part : m.content) {
                    if (const auto* tcp = std::get_if<qcode::ToolCallContentPart>(&part)) {
                        nlohmann::json call_json = {
                            {"id", tcp->id},
                            {"name", tcp->tool_name},
                            {"arguments", tcp->arguments},
                        };
                        std::string content = call_json.dump();
                        
                        sqlite3_reset(ins_stmt);
                        sqlite3_clear_bindings(ins_stmt);
                        sqlite3_bind_text(ins_stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins_stmt, 2, "ToolCall", -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins_stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(ins_stmt, 4, created_at);
                        if (sqlite3_step(ins_stmt) != SQLITE_DONE) {
                            LOG_ERROR("SQLite: overwrite insert ToolCall failed: {}", sqlite3_errmsg(db));
                        }
                    }
                }
            } else if (has_results) {
                for (const auto& part : m.content) {
                    if (const auto* trp = std::get_if<qcode::ToolResultContentPart>(&part)) {
                        nlohmann::json result_json = {
                            {"tool_call_id", trp->tool_call_id},
                            {"result", trp->result},
                            {"is_error", trp->is_error},
                            {"duration_ms", trp->duration_ms},
                        };
                        std::string content = result_json.dump();

                        sqlite3_reset(ins_stmt);
                        sqlite3_clear_bindings(ins_stmt);
                        sqlite3_bind_text(ins_stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins_stmt, 2, "ToolResult", -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(ins_stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(ins_stmt, 4, created_at);
                        if (sqlite3_step(ins_stmt) != SQLITE_DONE) {
                            LOG_ERROR("SQLite: overwrite insert ToolResult failed: {}", sqlite3_errmsg(db));
                        }
                    }
                }
            } else {
                std::string sender;
                if (m.role == qcode::kMessageRoleUser) sender = "User";
                else if (m.role == qcode::kMessageRoleAssistant) sender = "Assistant";
                else if (m.role == qcode::kMessageRoleSystem) sender = "System";

                std::string content;
                for (const auto& part : m.content) {
                    if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                        content += tp->text;
                    }
                }

                sqlite3_reset(ins_stmt);
                sqlite3_clear_bindings(ins_stmt);
                sqlite3_bind_text(ins_stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins_stmt, 2, sender.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins_stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins_stmt, 4, created_at);
                if (sqlite3_step(ins_stmt) != SQLITE_DONE) {
                    LOG_ERROR("SQLite: overwrite insert regular failed: {}", sqlite3_errmsg(db));
                }
            }
        }
        sqlite3_finalize(ins_stmt);
    }

    if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOG_ERROR("SQLite: failed to commit transaction: {}", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
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

    const char* sql =
        "SELECT sessions.id, sessions.title "
        "FROM sessions "
        "LEFT JOIN ( "
        "    SELECT session_id, MAX(created_at) AS last_msg_time "
        "    FROM messages "
        "    GROUP BY session_id "
        ") m ON m.session_id = sessions.id "
        "ORDER BY COALESCE(m.last_msg_time, sessions.created_at) DESC;";
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

    const char* sql =
        "SELECT sessions.id, sessions.title, COALESCE(sessions.workspace, ''), "
        "       COALESCE(sessions.provider, ''), COALESCE(sessions.model, ''), "
        "       COALESCE(m.last_msg_time, sessions.created_at) AS last_active, "
        "       COALESCE(m.msg_count, 0) AS msg_count "
        "FROM sessions "
        "LEFT JOIN ( "
        "    SELECT session_id, MAX(created_at) AS last_msg_time, COUNT(*) AS msg_count "
        "    FROM messages "
        "    GROUP BY session_id "
        ") m ON m.session_id = sessions.id "
        "ORDER BY last_active DESC;";
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
            info.last_active_at = sqlite3_column_int64(stmt, 5);
            info.message_count = sqlite3_column_int(stmt, 6);
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

std::string get_session_title(const std::string& session_id) {
    if (session_id.empty() || !is_valid_session_id(session_id)) return "";
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return "";

    std::string result;
    const char* sql = "SELECT COALESCE(title, '') FROM sessions WHERE id = ?;";
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

SessionStats get_session_stats(const std::string& session_id,
                               int live_tool_calls,
                               double live_tool_time_ms,
                               int live_prompt_tokens,
                               int live_completion_tokens,
                               int live_total_tokens) {
    SessionStats stats;
    stats.id = session_id;
    if (session_id.empty() || !is_valid_session_id(session_id)) return stats;

    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return stats;

    // ── Session metadata ──
    {
        const char* sql =
            "SELECT COALESCE(title, ''), COALESCE(workspace, ''), "
            "COALESCE(provider, ''), COALESCE(model, ''), "
            "COALESCE(created_at, 0) FROM sessions WHERE id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_stmt(db, sql, &stmt)) {
            sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* t = sqlite3_column_text(stmt, 0);
                const unsigned char* w = sqlite3_column_text(stmt, 1);
                const unsigned char* p = sqlite3_column_text(stmt, 2);
                const unsigned char* m = sqlite3_column_text(stmt, 3);
                stats.title = t ? reinterpret_cast<const char*>(t) : "";
                stats.workspace = w ? reinterpret_cast<const char*>(w) : "";
                stats.provider = p ? reinterpret_cast<const char*>(p) : "";
                stats.model = m ? reinterpret_cast<const char*>(m) : "";
                stats.created_at = sqlite3_column_int64(stmt, 4);
            }
            sqlite3_finalize(stmt);
        }
    }

    // ── Message counts + token/tool accumulation from stored JSON ──
    {
        const char* sql =
            "SELECT sender, content FROM messages WHERE session_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_stmt(db, sql, &stmt)) {
            sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* sender_txt = sqlite3_column_text(stmt, 0);
                const unsigned char* content_txt = sqlite3_column_text(stmt, 1);
                std::string sender = sender_txt ? reinterpret_cast<const char*>(sender_txt) : "";
                std::string content = content_txt ? reinterpret_cast<const char*>(content_txt) : "";
                stats.message_count++;
                if (sender == "User") stats.user_messages++;
                else if (sender == "Assistant") stats.assistant_messages++;
                else if (sender == "ToolCall") stats.tool_calls++;
                else if (sender == "ToolResult") {
                    // Try to read persisted token/tool counters from a prior run.
                    try {
                        auto j = nlohmann::json::parse(content);
                        if (j.is_object()) {
                            auto add = [&](const char* key, int& dst) {
                                if (j.contains(key) && j[key].is_number())
                                    dst += j[key].get<int>();
                            };
                            add("prompt_tokens", stats.prompt_tokens);
                            add("completion_tokens", stats.completion_tokens);
                            add("total_tokens", stats.total_tokens);
                            if (j.contains("duration_ms") && j["duration_ms"].is_number())
                                stats.total_tool_time_ms += j["duration_ms"].get<double>();
                        }
                    } catch (...) {}
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    sqlite3_close(db);

    // Live (current generation) counters take precedence / accumulate on top.
    stats.tool_calls += live_tool_calls;
    stats.total_tool_time_ms += live_tool_time_ms;
    // Use the larger of stored vs live token totals (latest generation dominates).
    stats.prompt_tokens = std::max(stats.prompt_tokens, live_prompt_tokens);
    stats.completion_tokens = std::max(stats.completion_tokens, live_completion_tokens);
    stats.total_tokens = std::max(stats.total_tokens, live_total_tokens);

    return stats;
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




void seed_model_capabilities_if_needed() {
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return;

    struct SeedCap {
        const char* model_id;
        const char* provider;
        const char* model_name;
        const char* arch;
        int context_window;
        int output_limit;
        int tool_call;
        int multi_turn;
        const char* bench;
        const char* rec;
    };

    static const SeedCap kDefaults[] = {
        {"nemotron-3-ultra-free", "opencode", "Nemotron 3 Ultra (Free)", "NVIDIA 550B MoE (55B Active)", 1000000, 128000, 1, 1, "Frontier Orchestration / 1M Context", "Massive Repository Context (1M) & Deep Reasoning"},
        {"deepseek-v4-flash-free", "opencode", "DeepSeek V4 Flash (Free)", "DeepSeek 284B MoE (13B Active)", 1000000, 65536, 1, 1, "High Function-Calling Fidelity", "Iterative Fast Multi-Turn Agent Tool Loops"},
        {"laguna-s-2.1-free", "opencode", "Laguna S 2.1 (Free)", "Poolside 118B MoE (8B Active)", 128000, 32768, 1, 1, "70.2% on Terminal-Bench 2.1", "Code Diffs & Precise File Modifications"},
        {"north-mini-code-free", "opencode", "North Mini Code (Free)", "Cohere 30B MoE (3B Active)", 256000, 64000, 1, 1, "Cohere Agentic Coding Debut", "Terminal Commands & Quick Shell Scripts"},
        {"mimo-v2.5-free", "opencode", "MiMo V2.5 (Free)", "Xiaomi MoE Instruction Model", 128000, 32768, 1, 0, "General Agentic Benchmark", "General Chat & Single-turn Instructions"},
        {"big-pickle", "opencode", "Big Pickle (Free)", "Community High-Payload Model", 512000, 32768, 1, 0, "Experimental High Context", "Large Text Ingestion & Bulk Payload Inspection"},

        {"claude-opus-4-6-thinking", "antigravity", "Claude Opus 4.6 Thinking", "Anthropic Reasoning Model", 200000, 8192, 1, 1, "SWE-bench Verified ~74.5%", "Complex Multi-File Refactoring & Architect Tasks"},
        {"claude-sonnet-4-6", "antigravity", "Claude Sonnet 4.6", "Anthropic Frontier Model", 200000, 8192, 1, 1, "SWE-bench Verified ~72.7%", "Production Agentic Workflows & Coding"},
        {"gemini-3.7-flash", "antigravity", "Gemini 3.7 Flash", "Google DeepMind Frontier Flash", 1000000, 65536, 1, 1, "Optimized Flash Agentic", "Adaptive Thinking Levels (Low/Med/High) & Multi-Step Tools"},
        {"gemini-3.1-pro", "antigravity", "Gemini 3.1 Pro", "Google DeepMind Dense/MoE", 2000000, 65536, 1, 1, "Top Tier (>60% SWE-bench)", "2M Context System Architecture & Large Repo Analysis"},
        {"gemini-3-flash", "antigravity", "Gemini 3 Flash", "Google DeepMind High-Efficiency", 1000000, 8192, 1, 1, "Low Latency Agentic", "Fast Command Execution & Lightweight Tool Calls"},
        {"gemini-2.5-flash", "antigravity", "Gemini 2.5 Flash", "Google DeepMind High-Speed", 1000000, 8192, 1, 1, "Efficient Flash Model", "Quick Single-Turn Questions & Standard Chat"},

        {"nvidia/nemotron-3-ultra-550b-a55b:free", "openrouter", "Nemotron 3 Ultra 550B (Free)", "NVIDIA 550B MoE (55B Active)", 1000000, 65536, 1, 1, "Frontier MoE 1M Context", "Deep Reasoning & Large File Inspection"},
        {"nvidia/nemotron-3.5-lightning:free", "openrouter", "Nemotron 3.5 Lightning (Free)", "NVIDIA High-Speed Reasoning", 1000000, 65536, 1, 1, "Ultra-Low Latency Agentic", "Fast Multi-Turn Tools & Real-Time Code Edits"},
        {"nvidia/nemotron-3-super-120b-a12b:free", "openrouter", "Nemotron 3 Super 120B (Free)", "NVIDIA 120B MoE (12B Active)", 262144, 32768, 1, 1, "High-Speed Code Assistant", "Fast Command Execution & Lightweight Tool Calls"},
        {"poolside/laguna-s-2.1:free", "openrouter", "Poolside Laguna S 2.1 (Free)", "Poolside 118B MoE (8B Active)", 262144, 32768, 1, 1, "70.2% on Terminal-Bench 2.1", "Iterative Code Modifications & Diffs"},
        {"cohere/north-mini-code:free", "openrouter", "Cohere North Mini Code (Free)", "Cohere 30B MoE (3B Active)", 256000, 32768, 1, 1, "Cohere Debut Sparse MoE", "Fast Command Execution & Auto-complete"}
    };

    const char* sql_insert =
        "INSERT INTO model_capabilities ("
        "  model_id, provider, model_name, architecture_info, context_window, output_limit, "
        "  tool_call_supported, multi_turn_reliable, verified_benchmark, recommended_for"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(model_id) DO UPDATE SET "
        "  provider=excluded.provider, "
        "  model_name=excluded.model_name, "
        "  architecture_info=excluded.architecture_info, "
        "  context_window=excluded.context_window, "
        "  output_limit=excluded.output_limit, "
        "  tool_call_supported=excluded.tool_call_supported, "
        "  multi_turn_reliable=excluded.multi_turn_reliable, "
        "  verified_benchmark=excluded.verified_benchmark, "
        "  recommended_for=excluded.recommended_for;";

    for (const auto& item : kDefaults) {
        sqlite3_stmt* stmt = nullptr;
        if (prepare_stmt(db, sql_insert, &stmt)) {
            sqlite3_bind_text(stmt, 1, item.model_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, item.provider, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, item.model_name, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, item.arch, -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 5, item.context_window);
            sqlite3_bind_int(stmt, 6, item.output_limit);
            sqlite3_bind_int(stmt, 7, item.tool_call);
            sqlite3_bind_int(stmt, 8, item.multi_turn);
            sqlite3_bind_text(stmt, 9, item.bench, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 10, item.rec, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    sqlite3_close(db);
}

void record_generation_turn(const std::string& model_id, const std::string& provider,
                            bool success, bool is_loop_failure, double latency_ms) {
    if (model_id.empty()) return;
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return;

    // Ensure model_capabilities entry exists
    const char* sql_ensure_cap =
        "INSERT INTO model_capabilities (model_id, provider, model_name) VALUES (?, ?, ?) "
        "ON CONFLICT(model_id) DO NOTHING;";
    sqlite3_stmt* stmt_cap = nullptr;
    if (prepare_stmt(db, sql_ensure_cap, &stmt_cap)) {
        sqlite3_bind_text(stmt_cap, 1, model_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_cap, 2, provider.empty() ? "opencode" : provider.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_cap, 3, model_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt_cap);
        sqlite3_finalize(stmt_cap);
    }

    long long now = static_cast<long long>(std::time(nullptr));
    const char* sql_upsert =
        "INSERT INTO model_runtime_stats ("
        "  model_id, total_turns, successful_turns, failed_turns, tool_loop_failures, avg_latency_ms, last_used"
        ") VALUES (?, 1, ?, ?, ?, ?, ?) "
        "ON CONFLICT(model_id) DO UPDATE SET "
        "  avg_latency_ms = (avg_latency_ms * total_turns + excluded.avg_latency_ms) / (total_turns + 1), "
        "  total_turns = total_turns + 1, "
        "  successful_turns = successful_turns + excluded.successful_turns, "
        "  failed_turns = failed_turns + excluded.failed_turns, "
        "  tool_loop_failures = tool_loop_failures + excluded.tool_loop_failures, "
        "  last_used = excluded.last_used;";

    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql_upsert, &stmt)) {
        sqlite3_bind_text(stmt, 1, model_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, success ? 1 : 0);
        sqlite3_bind_int(stmt, 3, success ? 0 : 1);
        sqlite3_bind_int(stmt, 4, is_loop_failure ? 1 : 0);
        sqlite3_bind_double(stmt, 5, latency_ms);
        sqlite3_bind_int64(stmt, 6, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

void record_user_feedback(const std::string& model_id, bool is_positive) {
    if (model_id.empty()) return;
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return;

    const char* sql_upsert =
        "INSERT INTO model_runtime_stats ("
        "  model_id, positive_feedback_count, negative_feedback_count"
        ") VALUES (?, ?, ?) "
        "ON CONFLICT(model_id) DO UPDATE SET "
        "  positive_feedback_count = positive_feedback_count + excluded.positive_feedback_count, "
        "  negative_feedback_count = negative_feedback_count + excluded.negative_feedback_count;";

    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql_upsert, &stmt)) {
        sqlite3_bind_text(stmt, 1, model_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, is_positive ? 1 : 0);
        sqlite3_bind_int(stmt, 3, is_positive ? 0 : 1);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

ModelPerformanceSummary get_model_performance_summary(const std::string& model_id, const std::string& provider) {
    ModelPerformanceSummary summary;
    summary.model_id = model_id;
    summary.provider = provider;
    if (model_id.empty()) return summary;

    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return summary;

    const char* sql =
        "SELECT c.model_id, c.provider, c.model_name, c.architecture_info, c.context_window, c.output_limit, "
        "       c.tool_call_supported, c.multi_turn_reliable, c.verified_benchmark, c.recommended_for, "
        "       COALESCE(r.total_turns, 0), COALESCE(r.successful_turns, 0), COALESCE(r.failed_turns, 0), "
        "       COALESCE(r.tool_loop_failures, 0), COALESCE(r.avg_latency_ms, 0.0), "
        "       COALESCE(r.positive_feedback_count, 0), COALESCE(r.negative_feedback_count, 0), "
        "       COALESCE(r.last_used, 0) "
        "FROM model_capabilities c "
        "LEFT JOIN model_runtime_stats r ON c.model_id = r.model_id "
        "WHERE c.model_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        sqlite3_bind_text(stmt, 1, model_id.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            summary.model_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            summary.provider = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            summary.model_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            summary.architecture_info = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            summary.context_window = sqlite3_column_int(stmt, 4);
            summary.output_limit = sqlite3_column_int(stmt, 5);
            summary.tool_call_supported = (sqlite3_column_int(stmt, 6) != 0);
            summary.multi_turn_reliable = (sqlite3_column_int(stmt, 7) != 0);
            summary.verified_benchmark = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            summary.recommended_for = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            summary.total_turns = sqlite3_column_int(stmt, 10);
            summary.successful_turns = sqlite3_column_int(stmt, 11);
            summary.failed_turns = sqlite3_column_int(stmt, 12);
            summary.tool_loop_failures = sqlite3_column_int(stmt, 13);
            summary.avg_latency_ms = sqlite3_column_double(stmt, 14);
            summary.positive_feedback_count = sqlite3_column_int(stmt, 15);
            summary.negative_feedback_count = sqlite3_column_int(stmt, 16);
            summary.last_used = sqlite3_column_int64(stmt, 17);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return summary;
}

std::vector<ModelPerformanceSummary> list_all_model_performance_summaries() {
    std::vector<ModelPerformanceSummary> result;
    std::string path;
    sqlite3* db = open_database(path);
    if (!db) return result;

    const char* sql =
        "SELECT c.model_id, c.provider, c.model_name, c.architecture_info, c.context_window, c.output_limit, "
        "       c.tool_call_supported, c.multi_turn_reliable, c.verified_benchmark, c.recommended_for, "
        "       COALESCE(r.total_turns, 0), COALESCE(r.successful_turns, 0), COALESCE(r.failed_turns, 0), "
        "       COALESCE(r.tool_loop_failures, 0), COALESCE(r.avg_latency_ms, 0.0), "
        "       COALESCE(r.positive_feedback_count, 0), COALESCE(r.negative_feedback_count, 0), "
        "       COALESCE(r.last_used, 0) "
        "FROM model_capabilities c "
        "LEFT JOIN model_runtime_stats r ON c.model_id = r.model_id;";

    sqlite3_stmt* stmt = nullptr;
    if (prepare_stmt(db, sql, &stmt)) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ModelPerformanceSummary summary;
            summary.model_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            summary.provider = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            summary.model_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            summary.architecture_info = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            summary.context_window = sqlite3_column_int(stmt, 4);
            summary.output_limit = sqlite3_column_int(stmt, 5);
            summary.tool_call_supported = (sqlite3_column_int(stmt, 6) != 0);
            summary.multi_turn_reliable = (sqlite3_column_int(stmt, 7) != 0);
            summary.verified_benchmark = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            summary.recommended_for = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            summary.total_turns = sqlite3_column_int(stmt, 10);
            summary.successful_turns = sqlite3_column_int(stmt, 11);
            summary.failed_turns = sqlite3_column_int(stmt, 12);
            summary.tool_loop_failures = sqlite3_column_int(stmt, 13);
            summary.avg_latency_ms = sqlite3_column_double(stmt, 14);
            summary.positive_feedback_count = sqlite3_column_int(stmt, 15);
            summary.negative_feedback_count = sqlite3_column_int(stmt, 16);
            summary.last_used = sqlite3_column_int64(stmt, 17);
            result.push_back(summary);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return result;
}


} // namespace session
} // namespace qcode
