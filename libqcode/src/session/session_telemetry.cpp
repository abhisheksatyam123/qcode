#include <qcode/session/session_store.h>
#include "session_db_internal.h"

#include <ctime>
#include <string>
#include <vector>

namespace qcode {
namespace session {

void seed_model_capabilities(const std::vector<ProviderInfo>& providers) {
    auto db_lock = SharedDbHandle::instance().acquire();
    sqlite3* db = db_lock.db;
    if (!db) return;

    const char* sql_insert =
        "INSERT INTO model_capabilities ("
        "  model_id, provider, model_name, context_window, output_limit, "
        "  tool_call_supported"
        ") VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(model_id) DO UPDATE SET "
        "  provider=excluded.provider, "
        "  model_name=excluded.model_name, "
        "  context_window=excluded.context_window, "
        "  output_limit=excluded.output_limit, "
        "  tool_call_supported=excluded.tool_call_supported;";

    for (const auto& provider : providers) {
        for (const auto& model : provider.models) {
            if (model.id.empty()) continue;
            sqlite3_stmt* stmt = nullptr;
            if (!prepare_stmt(db, sql_insert, &stmt)) continue;
            sqlite3_bind_text(stmt, 1, model.id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, provider.id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, model.name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 4, model.context_window);
            sqlite3_bind_int(stmt, 5, model.output_limit);
            sqlite3_bind_int(stmt, 6, model.tool_call ? 1 : 0);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

void record_generation_turn(const std::string& model_id, const std::string& provider,
                            bool success, bool is_loop_failure, double latency_ms) {
    if (model_id.empty()) return;
    auto db_lock = SharedDbHandle::instance().acquire();
    sqlite3* db = db_lock.db;
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
    // shared db handle
}

void record_user_feedback(const std::string& model_id, bool is_positive) {
    if (model_id.empty()) return;
    auto db_lock = SharedDbHandle::instance().acquire();
    sqlite3* db = db_lock.db;
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
    // shared db handle
}

ModelPerformanceSummary get_model_performance_summary(const std::string& model_id, const std::string& provider) {
    ModelPerformanceSummary summary;
    summary.model_id = model_id;
    summary.provider = provider;
    if (model_id.empty()) return summary;

    auto db_lock = SharedDbHandle::instance().acquire();
    sqlite3* db = db_lock.db;
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
            auto safe_text = [](sqlite3_stmt* s, int col) -> std::string {
                const unsigned char* t = sqlite3_column_text(s, col);
                return t ? reinterpret_cast<const char*>(t) : "";
            };
            summary.model_id = safe_text(stmt, 0);
            summary.provider = safe_text(stmt, 1);
            summary.model_name = safe_text(stmt, 2);
            summary.architecture_info = safe_text(stmt, 3);
            summary.context_window = sqlite3_column_int(stmt, 4);
            summary.output_limit = sqlite3_column_int(stmt, 5);
            summary.tool_call_supported = (sqlite3_column_int(stmt, 6) != 0);
            summary.multi_turn_reliable = (sqlite3_column_int(stmt, 7) != 0);
            summary.verified_benchmark = safe_text(stmt, 8);
            summary.recommended_for = safe_text(stmt, 9);
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
    // shared db handle
    return summary;
}

std::vector<ModelPerformanceSummary> list_all_model_performance_summaries() {
    std::vector<ModelPerformanceSummary> result;
    auto db_lock = SharedDbHandle::instance().acquire();
    sqlite3* db = db_lock.db;
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
            auto safe_text = [](sqlite3_stmt* s, int col) -> std::string {
                const unsigned char* t = sqlite3_column_text(s, col);
                return t ? reinterpret_cast<const char*>(t) : "";
            };
            summary.model_id = safe_text(stmt, 0);
            summary.provider = safe_text(stmt, 1);
            summary.model_name = safe_text(stmt, 2);
            summary.architecture_info = safe_text(stmt, 3);
            summary.context_window = sqlite3_column_int(stmt, 4);
            summary.output_limit = sqlite3_column_int(stmt, 5);
            summary.tool_call_supported = (sqlite3_column_int(stmt, 6) != 0);
            summary.multi_turn_reliable = (sqlite3_column_int(stmt, 7) != 0);
            summary.verified_benchmark = safe_text(stmt, 8);
            summary.recommended_for = safe_text(stmt, 9);
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
    // shared db handle
    return result;
}


} // namespace session
} // namespace qcode
