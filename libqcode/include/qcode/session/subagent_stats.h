#pragma once

#include <qcode/tools/subagent_router.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Persisted evidence for the subagent router (see qcode/tools/subagent_router.h).
// Tables: subagent_runs (one row per attempt) and subagent_arms (one row per
// stats_key). Arms are keyed by provider:model, so the same model id on two
// providers (antigravity:claude-x vs anthropic:claude-x) never shares stats.

namespace qcode::session {

// One attempt of a `task` subagent on one model. A task that fell back has
// several attempts; the last successful one produced the report.
struct SubagentRun {
  std::string task_id;  // child session id (ses_...)
  int attempt = 1;      // 1-based within the task
  std::string parent_session_id;
  std::string provider;
  std::string model;
  std::string mode;        // explore | implement | verify
  std::string difficulty;  // easy | medium | hard
  routing::Outcome outcome = routing::Outcome::kSuccess;
  std::string error;  // empty on success
  double latency_ms = 0;
  int64_t started_at = 0;  // unix seconds
  std::optional<int> rating;  // lead rating 1-5
  std::string rating_note;
  double quality = -1;  // q folded into the arms; -1 = none (transient/abort)
};

// Every arm (one small query).
routing::StatsTable load_subagent_stats();

// Store the attempt and fold it into its (provider:model, mode) arm and the
// model's "*" arm: success adds kImplicitQuality, a model failure adds 0 and
// counts a failure, a transient error only sets the arm's cooldown, an abort
// changes nothing.
void record_subagent_run(const SubagentRun& run);

// Store the lead's rating (1-5) for the task's successful attempt and move
// its arms from the quality folded in before to rating_quality(score).
// False when the task has no successful attempt.
bool rate_subagent_run(const std::string& task_id, int score,
                       const std::string& note);

// Attempts under a parent session, newest first.
std::vector<SubagentRun> list_subagent_runs(const std::string& parent_session_id,
                                            int limit = 100);

}  // namespace qcode::session
