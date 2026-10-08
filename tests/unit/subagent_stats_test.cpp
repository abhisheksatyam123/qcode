// Persistence of the subagent router's evidence: runs fold into the
// (provider:model, mode) arm and the model's "*" arm; ratings swap the
// quality folded in before.

#include "session/session_db_internal.h"

#include <qcode/session/session_store.h>
#include <qcode/session/subagent_stats.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace qcode::session {
namespace {

using routing::ArmStats;
using routing::Outcome;

constexpr double kEps = 1e-9;

class SubagentStatsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::error_code ec;
    std::filesystem::create_directories("/tmp/qcode_subagent_stats_test", ec);
    db_path_ = "/tmp/qcode_subagent_stats_test/test_" +
               std::to_string(::getpid()) + "_" +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()) +
               ".db";
    std::filesystem::remove(db_path_, ec);
    std::filesystem::remove(db_path_ + "-wal", ec);
    std::filesystem::remove(db_path_ + "-shm", ec);
    setenv("QCODE_DB_PATH", db_path_.c_str(), 1);
    init_database();
  }

  void TearDown() override {
    SharedDbHandle::instance().reset();
    std::error_code ec;
    std::filesystem::remove(db_path_, ec);
    std::filesystem::remove(db_path_ + "-wal", ec);
    std::filesystem::remove(db_path_ + "-shm", ec);
    unsetenv("QCODE_DB_PATH");
  }

  static SubagentRun make_run(const std::string& task_id, Outcome outcome,
                              const std::string& provider = "antigravity",
                              const std::string& model = "gemini-3-flash",
                              const std::string& mode = "explore") {
    SubagentRun r;
    r.task_id = task_id;
    r.parent_session_id = "ses_parent";
    r.provider = provider;
    r.model = model;
    r.mode = mode;
    r.difficulty = "medium";
    r.outcome = outcome;
    r.latency_ms = 1000;
    r.started_at = 1'700'000'000;
    return r;
  }

  // The arm, or a zeroed one when it has no row.
  static ArmStats arm(const std::string& provider, const std::string& model,
                      const std::string& mode) {
    const auto table = load_subagent_stats();
    auto it = table.find(routing::stats_key(provider, model, mode));
    return it == table.end() ? ArmStats{} : it->second;
  }

  static bool has_arm(const std::string& provider, const std::string& model,
                      const std::string& mode) {
    return load_subagent_stats().count(
               routing::stats_key(provider, model, mode)) > 0;
  }

  std::string db_path_;
};

TEST_F(SubagentStatsTest, MigrationCreatesTables) {
  auto db_lock = SharedDbHandle::instance().acquire();
  ASSERT_TRUE(db_lock);
  int tables = 0;
  sqlite3_stmt* stmt = nullptr;
  ASSERT_TRUE(prepare_stmt(db_lock.db,
                           "SELECT COUNT(*) FROM sqlite_master WHERE type = "
                           "'table' AND name IN ('subagent_runs', "
                           "'subagent_arms');",
                           &stmt));
  if (sqlite3_step(stmt) == SQLITE_ROW) tables = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  EXPECT_EQ(tables, 2);

  int version = 0;
  ASSERT_TRUE(prepare_stmt(db_lock.db, "PRAGMA user_version;", &stmt));
  if (sqlite3_step(stmt) == SQLITE_ROW) version = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  EXPECT_GE(version, 10);
}

TEST_F(SubagentStatsTest, SuccessUpdatesModeAndModelArms) {
  record_subagent_run(make_run("ses_a", Outcome::kSuccess));

  for (const char* mode : {"explore", "*"}) {
    const ArmStats a = arm("antigravity", "gemini-3-flash", mode);
    EXPECT_NEAR(a.alpha, 0.7, kEps) << mode;
    EXPECT_NEAR(a.beta, 0.3, kEps) << mode;
    EXPECT_EQ(a.runs, 1) << mode;
    EXPECT_EQ(a.failures, 0) << mode;
    EXPECT_NEAR(a.latency_ms, 1000, kEps) << mode;
  }
  EXPECT_FALSE(has_arm("antigravity", "gemini-3-flash", "implement"));

  const auto runs = list_subagent_runs("ses_parent");
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0].outcome, Outcome::kSuccess);
  EXPECT_NEAR(runs[0].quality, routing::kImplicitQuality, kEps);
  EXPECT_FALSE(runs[0].rating.has_value());
}

TEST_F(SubagentStatsTest, SecondSuccessDecaysEvidence) {
  record_subagent_run(make_run("ses_a", Outcome::kSuccess));
  SubagentRun second = make_run("ses_b", Outcome::kSuccess);
  second.latency_ms = 2000;
  record_subagent_run(second);

  const double k = routing::kDecay;
  const ArmStats a = arm("antigravity", "gemini-3-flash", "explore");
  EXPECT_NEAR(a.alpha, 0.7 * k + 0.7, kEps);
  EXPECT_NEAR(a.beta, 0.3 * k + 0.3, kEps);
  EXPECT_EQ(a.runs, 2);
  EXPECT_NEAR(a.latency_ms, 0.7 * 1000 + 0.3 * 2000, kEps);
}

TEST_F(SubagentStatsTest, ModelFailureCountsAgainstTheModel) {
  record_subagent_run(make_run("ses_a", Outcome::kSuccess));
  SubagentRun failed = make_run("ses_b", Outcome::kModelFailure);
  failed.error = "empty output";
  record_subagent_run(failed);

  const double k = routing::kDecay;
  for (const char* mode : {"explore", "*"}) {
    const ArmStats a = arm("antigravity", "gemini-3-flash", mode);
    EXPECT_NEAR(a.alpha, 0.7 * k, kEps) << mode;
    EXPECT_NEAR(a.beta, 0.3 * k + 1, kEps) << mode;
    EXPECT_EQ(a.runs, 2) << mode;
    EXPECT_EQ(a.failures, 1) << mode;
  }
  const auto runs = list_subagent_runs("ses_parent");
  ASSERT_EQ(runs.size(), 2u);
  EXPECT_EQ(runs[0].task_id, "ses_b");  // same started_at: higher id first
  EXPECT_EQ(runs[0].outcome, Outcome::kModelFailure);
  EXPECT_EQ(runs[0].error, "empty output");
  EXPECT_NEAR(runs[0].quality, 0, kEps);
}

TEST_F(SubagentStatsTest, TransientErrorOnlyRestsTheArm) {
  record_subagent_run(make_run("ses_a", Outcome::kSuccess));
  SubagentRun transient = make_run("ses_b", Outcome::kTransientError);
  transient.error = "HTTP 503 service unavailable";
  transient.latency_ms = 2500;
  record_subagent_run(transient);

  const int64_t ended = transient.started_at + 2;
  ArmStats a = arm("antigravity", "gemini-3-flash", "explore");
  EXPECT_NEAR(a.alpha, 0.7, kEps);
  EXPECT_NEAR(a.beta, 0.3, kEps);
  EXPECT_EQ(a.runs, 1);
  EXPECT_EQ(a.failures, 0);
  EXPECT_EQ(a.consecutive_transient, 1);
  EXPECT_EQ(a.cooldown_until,
            ended + routing::cooldown_seconds(1, transient.error));

  transient.task_id = "ses_c";
  record_subagent_run(transient);
  a = arm("antigravity", "gemini-3-flash", "*");
  EXPECT_EQ(a.consecutive_transient, 2);
  EXPECT_EQ(a.cooldown_until,
            ended + routing::cooldown_seconds(2, transient.error));
  EXPECT_EQ(a.runs, 1);

  record_subagent_run(make_run("ses_d", Outcome::kSuccess));
  a = arm("antigravity", "gemini-3-flash", "explore");
  EXPECT_EQ(a.consecutive_transient, 0);
  EXPECT_EQ(a.cooldown_until, 0);
  EXPECT_EQ(a.runs, 2);
}

TEST_F(SubagentStatsTest, AbortLeavesArmsUntouched) {
  record_subagent_run(make_run("ses_a", Outcome::kAborted));
  EXPECT_TRUE(load_subagent_stats().empty());

  record_subagent_run(make_run("ses_b", Outcome::kSuccess));
  record_subagent_run(make_run("ses_c", Outcome::kAborted));
  const ArmStats a = arm("antigravity", "gemini-3-flash", "explore");
  EXPECT_NEAR(a.alpha, 0.7, kEps);
  EXPECT_EQ(a.runs, 1);

  const auto runs = list_subagent_runs("ses_parent");
  ASSERT_EQ(runs.size(), 3u);
  EXPECT_EQ(runs[0].outcome, Outcome::kAborted);
  EXPECT_NEAR(runs[0].quality, -1, kEps);
}

TEST_F(SubagentStatsTest, RatingSwapsImplicitQualityForRatedOne) {
  record_subagent_run(make_run("ses_a", Outcome::kSuccess));

  ASSERT_TRUE(rate_subagent_run("ses_a", 5, "thorough"));
  const double d1 = routing::rating_quality(5) - routing::kImplicitQuality;
  for (const char* mode : {"explore", "*"}) {
    const ArmStats a = arm("antigravity", "gemini-3-flash", mode);
    EXPECT_NEAR(a.alpha, std::max(0.0, 0.7 + d1), kEps) << mode;
    EXPECT_NEAR(a.beta, std::max(0.0, 0.3 - d1), kEps) << mode;
    EXPECT_EQ(a.rated, 1) << mode;
    EXPECT_NEAR(a.rating_sum, 5, kEps) << mode;
    EXPECT_EQ(a.runs, 1) << mode;
  }
  auto runs = list_subagent_runs("ses_parent");
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0].rating, 5);
  EXPECT_EQ(runs[0].rating_note, "thorough");
  EXPECT_NEAR(runs[0].quality, routing::rating_quality(5), kEps);

  // Re-rating moves from the previous rating, not from the implicit one.
  const ArmStats before = arm("antigravity", "gemini-3-flash", "explore");
  ASSERT_TRUE(rate_subagent_run("ses_a", 2, "missed the bug"));
  const double d2 = routing::rating_quality(2) - routing::rating_quality(5);
  const ArmStats a = arm("antigravity", "gemini-3-flash", "explore");
  EXPECT_NEAR(a.alpha, std::max(0.0, before.alpha + d2), kEps);
  EXPECT_NEAR(a.beta, std::max(0.0, before.beta - d2), kEps);
  EXPECT_EQ(a.rated, 1);
  EXPECT_NEAR(a.rating_sum, 2, kEps);

  runs = list_subagent_runs("ses_parent");
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0].rating, 2);
  EXPECT_EQ(runs[0].rating_note, "missed the bug");
}

TEST_F(SubagentStatsTest, RatingTargetsTheLastSuccessfulAttempt) {
  EXPECT_FALSE(rate_subagent_run("ses_missing", 4, ""));

  SubagentRun failed = make_run("ses_a", Outcome::kModelFailure, "antigravity",
                                "gemini-3-flash");
  record_subagent_run(failed);
  EXPECT_FALSE(rate_subagent_run("ses_a", 4, ""));

  SubagentRun fallback =
      make_run("ses_a", Outcome::kSuccess, "opencode", "big-pickle");
  fallback.attempt = 2;
  record_subagent_run(fallback);
  const ArmStats flash_before = arm("antigravity", "gemini-3-flash", "explore");

  ASSERT_TRUE(rate_subagent_run("ses_a", 4, ""));
  const ArmStats flash = arm("antigravity", "gemini-3-flash", "explore");
  EXPECT_NEAR(flash.alpha, flash_before.alpha, kEps);
  EXPECT_EQ(flash.rated, 0);
  const ArmStats pickle = arm("opencode", "big-pickle", "explore");
  EXPECT_EQ(pickle.rated, 1);
  EXPECT_NEAR(pickle.rating_sum, 4, kEps);
}

TEST_F(SubagentStatsTest, SameModelIdOnTwoProvidersKeepsSeparateArms) {
  record_subagent_run(make_run("ses_a", Outcome::kSuccess, "antigravity",
                               "claude-sonnet-4-6", "implement"));
  record_subagent_run(make_run("ses_b", Outcome::kModelFailure, "anthropic",
                               "claude-sonnet-4-6", "implement"));

  for (const char* mode : {"implement", "*"}) {
    const ArmStats ag = arm("antigravity", "claude-sonnet-4-6", mode);
    EXPECT_NEAR(ag.alpha, 0.7, kEps) << mode;
    EXPECT_EQ(ag.failures, 0) << mode;
    const ArmStats an = arm("anthropic", "claude-sonnet-4-6", mode);
    EXPECT_NEAR(an.alpha, 0, kEps) << mode;
    EXPECT_NEAR(an.beta, 1, kEps) << mode;
    EXPECT_EQ(an.failures, 1) << mode;
  }
  EXPECT_EQ(load_subagent_stats().size(), 4u);
}

TEST_F(SubagentStatsTest, ListFiltersByParentNewestFirst) {
  SubagentRun r1 = make_run("ses_1", Outcome::kSuccess);
  r1.started_at = 100;
  SubagentRun r2 = make_run("ses_2", Outcome::kSuccess);
  r2.started_at = 300;
  SubagentRun r3 = make_run("ses_3", Outcome::kSuccess);
  r3.started_at = 200;
  SubagentRun other = make_run("ses_4", Outcome::kSuccess);
  other.parent_session_id = "ses_other";
  other.started_at = 400;
  for (const auto& r : {r1, r2, r3, other}) record_subagent_run(r);

  const auto runs = list_subagent_runs("ses_parent");
  ASSERT_EQ(runs.size(), 3u);
  EXPECT_EQ(runs[0].task_id, "ses_2");
  EXPECT_EQ(runs[1].task_id, "ses_3");
  EXPECT_EQ(runs[2].task_id, "ses_1");
  EXPECT_EQ(runs[0].provider, "antigravity");
  EXPECT_EQ(runs[0].model, "gemini-3-flash");
  EXPECT_EQ(runs[0].mode, "explore");
  EXPECT_EQ(runs[0].difficulty, "medium");
  EXPECT_EQ(runs[0].started_at, 300);

  EXPECT_EQ(list_subagent_runs("ses_parent", 2).size(), 2u);
  ASSERT_EQ(list_subagent_runs("ses_other").size(), 1u);
  EXPECT_TRUE(list_subagent_runs("ses_none").empty());
}

}  // namespace
}  // namespace qcode::session
