#pragma once

#include <qcode/config/provider_info.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// Picks the model a `task` subagent runs on. The lead (usually a paid model)
// delegates to free models; the router learns from every finished run and
// from the lead's ratings which free model does which kind of job well.
//
// Evidence is kept per arm = (provider:model, mode). Each run adds a quality
// q in [0,1] to the arm: alpha += q, beta += 1 - q, after decaying the old
// evidence by kDecay, so recent runs count most. Ranking draws a success rate
// from Beta(prior + evidence) (Thompson sampling): strong models win most
// jobs, unproven ones still get tried. Hard jobs use a conservative estimate
// instead of a draw, so they go to proven models.

namespace qcode::routing {

// How hard the lead says a delegated job is.
enum class Difficulty { kEasy, kMedium, kHard };
Difficulty parse_difficulty(std::string_view s);  // unknown -> kMedium
std::string_view to_string(Difficulty d);

// Capability guess from the model name, used as the prior until the model has
// its own record: strong (opus, pro, ultra, sonnet, ...), fast (flash, mini,
// lightning, haiku, ...), standard otherwise.
enum class Tier { kStrong, kStandard, kFast };
Tier tier_of(const ModelInfo& model);
std::string_view to_string(Tier t);

// Paid models are never picked automatically; they run only when the lead
// names one. Paid = provider in QCODE_PAID_PROVIDERS (default
// "anthropic,openai") or a nonzero catalog cost. antigravity, opencode and
// openrouter models are free.
bool is_paid(const ProviderInfo& provider, const ModelInfo& model);

// Learned evidence for one arm; the router adds the prior itself.
struct ArmStats {
  double alpha = 0;  // decayed sum of q
  double beta = 0;   // decayed sum of 1 - q
  int runs = 0;      // runs that said something about the model
  int failures = 0;  // of those, runs the model failed
  int rated = 0;     // runs the lead rated
  double rating_sum = 0;   // sum of those ratings (1-5)
  double latency_ms = 0;   // moving average over successful runs
  int64_t cooldown_until = 0;  // unix seconds; the arm rests until then
  int consecutive_transient = 0;  // transient errors in a row
};

// Arms by stats_key(). Mode "*" holds the model's evidence over all modes.
using StatsTable = std::map<std::string, ArmStats>;
std::string stats_key(std::string_view provider, std::string_view model,
                      std::string_view mode);

constexpr double kDecay = 0.97;           // per run on the same arm
constexpr double kImplicitQuality = 0.7;  // finished, not rated by the lead

struct RouteRequest {
  std::string mode = "explore";  // explore | implement | verify
  Difficulty difficulty = Difficulty::kMedium;
  std::string lead_provider;  // the lead's own model is never picked
  std::string lead_model;
  bool allow_cursor = false;  // cursor runs only when the lead asks for it
  int64_t now = 0;            // unix seconds
  // Subagents running right now, per provider (spreads parallel jobs).
  std::map<std::string, int> inflight_by_provider;
};

struct RankedTarget {
  const ProviderInfo* provider = nullptr;
  const ModelInfo* model = nullptr;
  double score = 0;     // utility the ranking used
  double expected = 0;  // posterior mean success for the requested mode
  bool resting = false; // in cooldown; ranked after every rested arm
  std::string reason;   // one line for logs and task metadata
};

// Free, authenticated, working models other than the lead's, best first.
std::vector<RankedTarget> rank_targets(const std::vector<ProviderInfo>& providers,
                                       const StatsTable& stats,
                                       const RouteRequest& req, std::mt19937& rng);

// What a finished run says about the model.
enum class Outcome {
  kSuccess,         // finished with a report
  kModelFailure,    // the model's fault: empty output, stuck loop, bad calls
  kTransientError,  // provider trouble: 429, 5xx, timeout, auth; not quality
  kAborted,         // stopped by the user; says nothing
};
Outcome classify_outcome(bool ok, std::string_view error);

// Lead rating 1-5 -> quality 0..1.
double rating_quality(int score);

// Rest after `consecutive` transient errors in a row: 1, 2, 4, ... minutes up
// to an hour; a daily free-tier limit rests the arm until the same time
// tomorrow.
int64_t cooldown_seconds(int consecutive, std::string_view error);

// Compact table of the free models for the lead's prompt: tier, success per
// mode, rating, latency, resting. Empty when there is nothing to show.
std::string format_routing_table(const std::vector<ProviderInfo>& providers,
                                 const StatsTable& stats, std::string_view lead_provider,
                                 std::string_view lead_model, int64_t now,
                                 std::size_t max_rows = 10);

// Running subagents per provider, process-wide. Hold a ScopedInflight while a
// subagent runs on `provider`.
std::map<std::string, int> inflight_snapshot();
class ScopedInflight {
 public:
  explicit ScopedInflight(std::string provider);
  ~ScopedInflight();
  ScopedInflight(const ScopedInflight&) = delete;
  ScopedInflight& operator=(const ScopedInflight&) = delete;

 private:
  std::string provider_;
};

}  // namespace qcode::routing
