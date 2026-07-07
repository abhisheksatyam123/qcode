#include <ai/tui/db.h>

#include <sqlite3.h>
#include <cstdlib>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>
#include <filesystem>

#include <ai/logger.h>

namespace ai {
namespace tui {
namespace db {

static std::string get_db_path() {
    const char* home = std::getenv("HOME");
    std::string path = home ? std::string(home) + "/.qcode.db" : ".qcode.db";
    return path;
}

static std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; ++i) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; ++i) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; ++i) ss << dis(gen);
    ss << "-";
    ss << dis(gen) % 4 + 8;
    for (int i = 0; i < 3; ++i) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; ++i) ss << dis(gen);
    return ss.str();
}

void init_database() {
    std::string path = get_db_path();
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) {
        ai::logger::log_info("SQLite: failed to open database at {}", path);
        if (db) sqlite3_close(db);
        return;
    }

    char* err_msg = nullptr;
    const char* schema_sessions = 
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  id TEXT PRIMARY KEY,"
        "  title TEXT,"
        "  provider TEXT,"
        "  model TEXT,"
        "  created_at INTEGER"
        ");";
    rc = sqlite3_exec(db, schema_sessions, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        ai::logger::log_info("SQLite: create sessions table error: {}", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
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
    rc = sqlite3_exec(db, schema_messages, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        ai::logger::log_info("SQLite: create messages table error: {}", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
    }

    sqlite3_close(db);
}

std::string create_new_session(const std::string& provider, const std::string& model) {
    std::string path = get_db_path();
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return "";
    }

    std::string uuid = generate_uuid();
    auto now = std::chrono::system_clock::now();
    long long created_at = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    
    // Default title is "Session - <model_name>"
    std::string title = "Session - " + model;

    const char* sql = "INSERT INTO sessions (id, title, provider, model, created_at) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, provider.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, model.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 5, created_at);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return uuid;
}

std::string get_last_active_session() {
    std::string path = get_db_path();
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return "";
    }

    std::string uuid = "";
    const char* sql = "SELECT id FROM sessions ORDER BY created_at DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) uuid = reinterpret_cast<const char*>(val);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return uuid;
}

void save_message(const std::string& session_id, const std::string& sender, const std::string& content) {
    if (session_id.empty()) return;

    std::string path = get_db_path();
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return;
    }

    auto now = std::chrono::system_clock::now();
    long long created_at = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    const char* sql = "INSERT INTO messages (session_id, sender, content, created_at) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, sender.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, created_at);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

void reload_session_history(const std::string& session_id, ChatState& state) {
    if (session_id.empty()) return;

    std::string path = get_db_path();
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return;
    }

    state.chat_history->clear();
    state.messages_history->clear();

    const char* sql = "SELECT sender, content FROM messages WHERE session_id = ? ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* sender_txt = sqlite3_column_text(stmt, 0);
            const unsigned char* content_txt = sqlite3_column_text(stmt, 1);
            
            std::string sender = sender_txt ? reinterpret_cast<const char*>(sender_txt) : "";
            std::string content = content_txt ? reinterpret_cast<const char*>(content_txt) : "";
            
            state.chat_history->push_back({sender, content});
            
            // Build ai::Message object for AI API context
            if (sender == "User") {
                state.messages_history->push_back(ai::Message::user(content));
            } else if (sender == "Assistant") {
                state.messages_history->push_back(ai::Message::assistant(content));
            }
            // System/Tool messages are not directly pushed to messages_history unless they represent tool execution 
            // sequence, but here they can just be loaded in UI history.
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

std::vector<std::pair<std::string, std::string>> list_sessions() {
    std::vector<std::pair<std::string, std::string>> sessions;
    std::string path = get_db_path();
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return sessions;
    }

    const char* sql = "SELECT id, title FROM sessions ORDER BY created_at DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
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

void delete_session(const std::string& session_id) {
    if (session_id.empty()) return;

    std::string path = get_db_path();
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return;
    }

    // Enable foreign keys to cascade deletes
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    const char* sql = "DELETE FROM sessions WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

} // namespace db
} // namespace tui
} // namespace ai
