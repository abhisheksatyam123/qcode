#pragma once

// Internal SQLite helpers shared by session_store.cpp and session_telemetry.cpp.
// Not a public API.

#include <qcode/session/session_store.h>
#include <qcode/core/logger.h>

#include <sqlite3.h>
#include <cstdlib>
#include <mutex>
#include <string>

namespace qcode {
namespace session {

inline std::string get_db_path() {
    if (const char* p = std::getenv("QCODE_DB_PATH")) return p;
    if (const char* p = std::getenv("OPENCODE_DB_PATH")) return p;
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.qcode.db" : ".qcode.db";
}

inline sqlite3* open_database(std::string& out_path) {
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

class SharedDbHandle final {
public:
    static SharedDbHandle& instance() {
        static SharedDbHandle inst;
        return inst;
    }

    struct DbLock {
        std::unique_lock<std::mutex> lock;
        sqlite3* db = nullptr;
        explicit operator bool() const { return db != nullptr; }
    };

    DbLock acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        const std::string expected_path = get_db_path();
        if (db_ && current_path_ != expected_path) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        if (!db_) {
            std::string actual_path;
            db_ = open_database(actual_path);
            current_path_ = expected_path;
        }
        return DbLock{std::move(lock), db_};
    }

    sqlite3* peek() const { return db_; }

private:
    SharedDbHandle() = default;
    ~SharedDbHandle() {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }
    sqlite3* db_ = nullptr;
    std::string current_path_;
    std::mutex mutex_;
};

inline bool prepare_stmt(sqlite3* db, const char* sql, sqlite3_stmt** stmt) {
    if (sqlite3_prepare_v2(db, sql, -1, stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("SQLite: prepare failed: {}", sqlite3_errmsg(db));
        return false;
    }
    return true;
}

}  // namespace session
}  // namespace qcode
