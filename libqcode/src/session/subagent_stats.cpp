#include <qcode/session/subagent_stats.h>

#include "session_db_internal.h"

#include <algorithm>
#include <ctime>
#include <string_view>

namespace qcode::session {
namespace {

using routing::ArmStats;
using routing::Outcome;

const char* outcome_name(Outcome o) {
  switch (o) {
    case Outcome::kSuccess:
      return "success";
    case Outcome::kModelFailure:
      return "model_failure";
    case Outcome::kTransientError:
      return "transient";
    case Outcome::kAborted:
      return "aborted";
  }
  return "aborted";
}

Outcome parse_outcome(std::string_view s) {
  if (s == "success") return Outcome::kSuccess;
  if (s == "model_failure") return Outcome::kModelFailure;
  if (s == "transient") return Outcome::kTransientError;
  return Outcome::kAborted;
}

std::string text_col(sqlite3_stmt* s, int col) {
  const unsigned char* t = sqlite3_column_text(s, col);
  return t ? reinterpret_cast<const char*>(t) : "";
}

bool exec_sql(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) == SQLITE_OK) return true;
  LOG_ERROR("SQLite: '{}' failed: {}", sql, err ? err : "unknown");
  sqlite3_free(err);
  return false;
}

// Commits when every step succeeded, otherwise rolls the whole call back.
bool finish(sqlite3* db, bool ok) {
  if (ok && exec_sql(db, "COMMIT;")) return true;
  exec_sql(db, "ROLLBACK;");
  return false;
}

// The run's (provider:model, mode) arm and the model's "*" arm.
std::vector<std::string> arm_keys(const std::string& provider,
                                  const std::string& model,
                                  const std::string& mode) {
  std::vector<std::string> keys{routing::stats_key(provider, model, mode)};
  std::string all = routing::stats_key(provider, model, "*");
  if (all != keys.front()) keys.push_back(std::move(all));
  return keys;
}

constexpr const char* kArmColumns =
    "alpha, beta, runs, failures, rated, rating_sum, latency_ms, "
    "cooldown_until, consecutive_transient";

ArmStats read_arm(sqlite3_stmt* s, int col) {
  ArmStats a;
  a.alpha = sqlite3_column_double(s, col + 0);
  a.beta = sqlite3_column_double(s, col + 1);
  a.runs = sqlite3_column_int(s, col + 2);
  a.failures = sqlite3_column_int(s, col + 3);
  a.rated = sqlite3_column_int(s, col + 4);
  a.rating_sum = sqlite3_column_double(s, col + 5);
  a.latency_ms = sqlite3_column_double(s, col + 6);
  a.cooldown_until = sqlite3_column_int64(s, col + 7);
  a.consecutive_transient = sqlite3_column_int(s, col + 8);
  return a;
}

// Zeroed stats when the arm has no row yet; false on a read error, so a
// failed read never overwrites the arm with zeros.
bool load_arm(sqlite3* db, const std::string& key, ArmStats& out) {
  const std::string sql = std::string("SELECT ") + kArmColumns +
                          " FROM subagent_arms WHERE key = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (!prepare_stmt(db, sql.c_str(), &stmt)) return false;
  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
  const int rc = sqlite3_step(stmt);
  out = rc == SQLITE_ROW ? read_arm(stmt, 0) : ArmStats{};
  sqlite3_finalize(stmt);
  return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool save_arm(sqlite3* db, const std::string& key, const ArmStats& a) {
  const char* sql =
      "INSERT OR REPLACE INTO subagent_arms (key, alpha, beta, runs, "
      "failures, rated, rating_sum, latency_ms, cooldown_until, "
      "consecutive_transient, updated_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (!prepare_stmt(db, sql, &stmt)) return false;
  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_double(stmt, 2, a.alpha);
  sqlite3_bind_double(stmt, 3, a.beta);
  sqlite3_bind_int(stmt, 4, a.runs);
  sqlite3_bind_int(stmt, 5, a.failures);
  sqlite3_bind_int(stmt, 6, a.rated);
  sqlite3_bind_double(stmt, 7, a.rating_sum);
  sqlite3_bind_double(stmt, 8, a.latency_ms);
  sqlite3_bind_int64(stmt, 9, a.cooldown_until);
  sqlite3_bind_int(stmt, 10, a.consecutive_transient);
  sqlite3_bind_int64(stmt, 11, static_cast<int64_t>(std::time(nullptr)));
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) LOG_ERROR("SQLite: save arm '{}' failed: {}", key, sqlite3_errmsg(db));
  sqlite3_finalize(stmt);
  return ok;
}

// The quality an outcome folds into the arms; -1 when it folds none.
double outcome_quality(Outcome o) {
  switch (o) {
    case Outcome::kSuccess:
      return routing::kImplicitQuality;
    case Outcome::kModelFailure:
      return 0;
    default:
      return -1;
  }
}

// Folds one attempt into an arm. False when the arm stays as it was.
bool fold_run(ArmStats& a, const SubagentRun& run) {
  switch (run.outcome) {
    case Outcome::kSuccess: {
      const double q = routing::kImplicitQuality;
      a.alpha = a.alpha * routing::kDecay + q;
      a.beta = a.beta * routing::kDecay + (1 - q);
      a.latency_ms = (a.runs == 0 || a.latency_ms == 0)
                         ? run.latency_ms
                         : 0.7 * a.latency_ms + 0.3 * run.latency_ms;
      a.runs += 1;
      a.consecutive_transient = 0;
      a.cooldown_until = 0;
      return true;
    }
    case Outcome::kModelFailure:
      a.alpha *= routing::kDecay;
      a.beta = a.beta * routing::kDecay + 1;
      a.runs += 1;
      a.failures += 1;
      a.consecutive_transient = 0;
      return true;
    case Outcome::kTransientError: {
      // The cooldown starts when the failed attempt ended.
      const int64_t now =
          run.started_at > 0
              ? run.started_at + static_cast<int64_t>(run.latency_ms / 1000)
              : static_cast<int64_t>(std::time(nullptr));
      a.consecutive_transient += 1;
      a.cooldown_until =
          now + routing::cooldown_seconds(a.consecutive_transient, run.error);
      return true;
    }
    case Outcome::kAborted:
      return false;
  }
  return false;
}

bool insert_run(sqlite3* db, const SubagentRun& run) {
  const char* sql =
      "INSERT OR REPLACE INTO subagent_runs (task_id, attempt, "
      "parent_session_id, provider, model, mode, difficulty, outcome, error, "
      "latency_ms, started_at, rating, rating_note, quality) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (!prepare_stmt(db, sql, &stmt)) return false;
  sqlite3_bind_text(stmt, 1, run.task_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, run.attempt);
  sqlite3_bind_text(stmt, 3, run.parent_session_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, run.provider.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 5, run.model.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, run.mode.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 7, run.difficulty.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 8, outcome_name(run.outcome), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 9, run.error.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_double(stmt, 10, run.latency_ms);
  sqlite3_bind_int64(stmt, 11, run.started_at);
  if (run.rating) {
    sqlite3_bind_int(stmt, 12, *run.rating);
  } else {
    sqlite3_bind_null(stmt, 12);
  }
  sqlite3_bind_text(stmt, 13, run.rating_note.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_double(stmt, 14, outcome_quality(run.outcome));
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) {
    LOG_ERROR("SQLite: insert subagent run '{}' #{} failed: {}", run.task_id,
              run.attempt, sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
  return ok;
}

bool set_run_rating(sqlite3* db, int64_t row_id, int score,
                    const std::string& note, double quality) {
  const char* sql =
      "UPDATE subagent_runs SET rating = ?, rating_note = ?, quality = ? "
      "WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (!prepare_stmt(db, sql, &stmt)) return false;
  sqlite3_bind_int(stmt, 1, score);
  sqlite3_bind_text(stmt, 2, note.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_double(stmt, 3, quality);
  sqlite3_bind_int64(stmt, 4, row_id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

}  // namespace

routing::StatsTable load_subagent_stats() {
  routing::StatsTable table;
  auto db_lock = SharedDbHandle::instance().acquire();
  sqlite3* db = db_lock.db;
  if (!db) return table;

  const std::string sql =
      std::string("SELECT key, ") + kArmColumns + " FROM subagent_arms;";
  sqlite3_stmt* stmt = nullptr;
  if (!prepare_stmt(db, sql.c_str(), &stmt)) return table;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    table[text_col(stmt, 0)] = read_arm(stmt, 1);
  }
  sqlite3_finalize(stmt);
  return table;
}

void record_subagent_run(const SubagentRun& run) {
  auto db_lock = SharedDbHandle::instance().acquire();
  sqlite3* db = db_lock.db;
  // IMMEDIATE: take the write lock before reading the arms we rewrite.
  if (!db || !exec_sql(db, "BEGIN IMMEDIATE;")) return;

  bool ok = insert_run(db, run);
  for (const auto& key : arm_keys(run.provider, run.model, run.mode)) {
    ArmStats arm;
    ok = ok && load_arm(db, key, arm);
    if (ok && fold_run(arm, run)) ok = save_arm(db, key, arm);
  }
  if (!finish(db, ok)) {
    LOG_ERROR("SQLite: subagent run '{}' #{} rolled back", run.task_id,
              run.attempt);
  }
}

bool rate_subagent_run(const std::string& task_id, int score,
                       const std::string& note) {
  score = std::clamp(score, 1, 5);
  auto db_lock = SharedDbHandle::instance().acquire();
  sqlite3* db = db_lock.db;
  if (!db || !exec_sql(db, "BEGIN IMMEDIATE;")) return false;

  // The attempt that produced the report: the task's last successful one.
  const char* sql_find =
      "SELECT id, provider, model, mode, rating, quality FROM subagent_runs "
      "WHERE task_id = ? AND outcome = 'success' "
      "ORDER BY attempt DESC LIMIT 1;";
  sqlite3_stmt* stmt = nullptr;
  if (!prepare_stmt(db, sql_find, &stmt)) return finish(db, false);
  sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return finish(db, false);  // nothing to rate
  }
  const int64_t row_id = sqlite3_column_int64(stmt, 0);
  const std::string provider = text_col(stmt, 1);
  const std::string model = text_col(stmt, 2);
  const std::string mode = text_col(stmt, 3);
  const std::optional<int> old_rating =
      sqlite3_column_type(stmt, 4) == SQLITE_NULL
          ? std::nullopt
          : std::optional<int>(sqlite3_column_int(stmt, 4));
  const double row_q = sqlite3_column_double(stmt, 5);
  sqlite3_finalize(stmt);

  // Swap the quality folded in before for the rated one.
  const double old_q = row_q >= 0 ? row_q : routing::kImplicitQuality;
  const double new_q = routing::rating_quality(score);
  const double delta = new_q - old_q;

  bool ok = true;
  for (const auto& key : arm_keys(provider, model, mode)) {
    ArmStats arm;
    ok = load_arm(db, key, arm);
    if (!ok) break;
    arm.alpha = std::max(0.0, arm.alpha + delta);
    arm.beta = std::max(0.0, arm.beta - delta);
    if (old_rating) {
      arm.rating_sum += score - *old_rating;
    } else {
      arm.rated += 1;
      arm.rating_sum += score;
    }
    ok = save_arm(db, key, arm);
    if (!ok) break;
  }
  ok = ok && set_run_rating(db, row_id, score, note, new_q);
  return finish(db, ok);
}

std::vector<SubagentRun> list_subagent_runs(const std::string& parent_session_id,
                                            int limit) {
  std::vector<SubagentRun> out;
  auto db_lock = SharedDbHandle::instance().acquire();
  sqlite3* db = db_lock.db;
  if (!db) return out;

  const char* sql =
      "SELECT task_id, attempt, parent_session_id, provider, model, mode, "
      "difficulty, outcome, error, latency_ms, started_at, rating, "
      "rating_note, quality FROM subagent_runs WHERE parent_session_id = ? "
      "ORDER BY started_at DESC, id DESC LIMIT ?;";
  sqlite3_stmt* stmt = nullptr;
  if (!prepare_stmt(db, sql, &stmt)) return out;
  sqlite3_bind_text(stmt, 1, parent_session_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, limit);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SubagentRun r;
    r.task_id = text_col(stmt, 0);
    r.attempt = sqlite3_column_int(stmt, 1);
    r.parent_session_id = text_col(stmt, 2);
    r.provider = text_col(stmt, 3);
    r.model = text_col(stmt, 4);
    r.mode = text_col(stmt, 5);
    r.difficulty = text_col(stmt, 6);
    r.outcome = parse_outcome(text_col(stmt, 7));
    r.error = text_col(stmt, 8);
    r.latency_ms = sqlite3_column_double(stmt, 9);
    r.started_at = sqlite3_column_int64(stmt, 10);
    if (sqlite3_column_type(stmt, 11) != SQLITE_NULL) {
      r.rating = sqlite3_column_int(stmt, 11);
    }
    r.rating_note = text_col(stmt, 12);
    r.quality = sqlite3_column_double(stmt, 13);
    out.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return out;
}

}  // namespace qcode::session
