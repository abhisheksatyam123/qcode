#include <qcode/tools/subagent_router.h>
#include <qcode/tools/task_target.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <sstream>

namespace qcode::routing {
namespace {

std::string to_lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string trim(std::string_view s) {
  const auto first = s.find_first_not_of(" \t");
  if (first == std::string_view::npos) return "";
  return std::string(s.substr(first, s.find_last_not_of(" \t") - first + 1));
}

bool contains_any(std::string_view haystack,
                  std::initializer_list<std::string_view> needles) {
  for (const auto needle : needles) {
    if (haystack.find(needle) != std::string_view::npos) return true;
  }
  return false;
}

// True when `code` stands alone in `s` ("HTTP 429", "429:"), so "4290 steps"
// or "5000 lines" do not read as status codes.
bool has_status_code(std::string_view s, std::string_view code) {
  for (auto pos = s.find(code); pos != std::string_view::npos;
       pos = s.find(code, pos + 1)) {
    const auto end = pos + code.size();
    const bool left = pos == 0 || !std::isalnum(static_cast<unsigned char>(s[pos - 1]));
    const bool right = end == s.size() || !std::isalnum(static_cast<unsigned char>(s[end]));
    if (left && right) return true;
  }
  return false;
}

// Lowercase tokens split on punctuation and at letter/digit boundaries:
// "nemotron-3-ultra-550b" -> nemotron 3 ultra 550 b. Whole tokens only, so
// "preview" is not "pro" and "gemini" is not "mini".
std::vector<std::string> name_tokens(std::string_view text) {
  std::vector<std::string> tokens;
  std::string cur;
  auto flush = [&] {
    if (!cur.empty()) tokens.push_back(std::move(cur));
    cur.clear();
  };
  for (const unsigned char c : text) {
    if (!std::isalnum(c)) {
      flush();
      continue;
    }
    if (!cur.empty() &&
        (std::isdigit(c) != 0) != (std::isdigit(static_cast<unsigned char>(cur.back())) != 0)) {
      flush();
    }
    cur.push_back(static_cast<char>(std::tolower(c)));
  }
  flush();
  return tokens;
}

// "550" followed by "b": a parameter count of 200B or more.
bool is_big_param_count(const std::string& number, const std::string& next) {
  if (next != "b" || !std::isdigit(static_cast<unsigned char>(number.front()))) return false;
  long long value = 0;
  const auto [ptr, ec] = std::from_chars(number.data(), number.data() + number.size(), value);
  return ec == std::errc() && value >= 200;
}

struct Candidate {
  const ProviderInfo* provider = nullptr;
  const ModelInfo* model = nullptr;
};

// The models the router may pick: free, authenticated, working, not the lead.
std::vector<Candidate> free_candidates(const std::vector<ProviderInfo>& providers,
                                       std::string_view lead_provider,
                                       std::string_view lead_model, bool allow_cursor) {
  std::vector<Candidate> out;
  for (const auto& p : providers) {
    if (!is_provider_authenticated(p)) continue;
    if (!allow_cursor && p.id.find("cursor") != std::string::npos) continue;
    for (const auto& m : p.models) {
      if (!is_model_working(p, m) || is_paid(p, m)) continue;
      if (matches_orchestrator(p.id, m.id, lead_provider, lead_model)) continue;
      out.push_back({&p, &m});
    }
  }
  return out;
}

// The arm's evidence, or an empty arm when the model has none yet.
ArmStats arm(const StatsTable& stats, std::string_view provider, std::string_view model,
             std::string_view mode) {
  const auto it = stats.find(stats_key(provider, model, mode));
  return it == stats.end() ? ArmStats{} : it->second;
}

// The tier prior counts as this many runs: a few real results outweigh it.
constexpr double kPriorWeight = 4;
// Share of the model's evidence from other modes that counts for this mode.
constexpr double kPoolWeight = 0.3;

// Prior success rate before the model has a record. Implementing punishes a
// weak model more than exploring or verifying does.
double prior_mean(Tier tier, std::string_view mode) {
  const bool implement = mode == "implement";
  switch (tier) {
    case Tier::kStrong: return implement ? 0.70 : 0.75;
    case Tier::kStandard: return implement ? 0.50 : 0.60;
    case Tier::kFast: return implement ? 0.45 : 0.60;
  }
  return 0.6;
}

struct Beta {
  double a = 1;
  double b = 1;
  double mean() const { return a / (a + b); }
};

// Prior + the mode arm's evidence + a little of what the model showed in the
// other modes (the all-modes arm minus this mode's share).
Beta posterior(Tier tier, std::string_view mode, const ArmStats& m, const ArmStats& all) {
  const double mean = prior_mean(tier, mode);
  return {kPriorWeight * mean + m.alpha + kPoolWeight * std::max(0.0, all.alpha - m.alpha),
          kPriorWeight * (1 - mean) + m.beta + kPoolWeight * std::max(0.0, all.beta - m.beta)};
}

// Thompson draw from Beta(a, b) as X / (X + Y) with X ~ Gamma(a), Y ~ Gamma(b).
double draw(const Beta& p, std::mt19937& rng) {
  std::gamma_distribution<double> gx(p.a, 1.0);
  std::gamma_distribution<double> gy(p.b, 1.0);
  const double x = gx(rng);
  const double y = gy(rng);
  return x + y > 0 ? x / (x + y) : p.mean();
}

// Mean minus half a standard deviation: a long record beats a lucky guess.
double conservative(const Beta& p) {
  const double n = p.a + p.b;
  const double sd = std::sqrt(p.a * p.b / (n * n * (n + 1)));
  return std::max(0.0, p.mean() - 0.5 * sd);
}

// Hard jobs need strong models; easy ones go to fast models and leave the
// strong ones free for harder work.
double difficulty_fit(Difficulty d, Tier t, std::string_view mode) {
  switch (d) {
    case Difficulty::kHard:
      return t == Tier::kFast ? 0.55 : t == Tier::kStandard ? 0.85 : 1.0;
    case Difficulty::kMedium:
      return t == Tier::kFast && mode == "implement" ? 0.9 : 1.0;
    case Difficulty::kEasy:
      return t == Tier::kStrong ? 0.9 : t == Tier::kFast ? 1.05 : 1.0;
  }
  return 1.0;
}

// "40s", "12m", "24h".
std::string short_duration(int64_t seconds) {
  if (seconds < 120) return std::to_string(seconds) + "s";
  if (seconds < 120 * 60) return std::to_string((seconds + 30) / 60) + "m";
  return std::to_string((seconds + 1800) / 3600) + "h";
}

std::string fixed(double value, int decimals) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
  return buf;
}

// Subagents running per provider, process-wide.
struct InflightRegistry {
  std::mutex mu;
  std::map<std::string, int> count;
};

InflightRegistry& inflight_registry() {
  static InflightRegistry registry;
  return registry;
}

}  // namespace

Difficulty parse_difficulty(std::string_view s) {
  const std::string d = to_lower(trim(s));
  if (d == "easy" || d == "simple" || d == "trivial") return Difficulty::kEasy;
  if (d == "hard" || d == "complex" || d == "difficult") return Difficulty::kHard;
  return Difficulty::kMedium;
}

std::string_view to_string(Difficulty d) {
  switch (d) {
    case Difficulty::kEasy: return "easy";
    case Difficulty::kMedium: return "medium";
    case Difficulty::kHard: return "hard";
  }
  return "medium";
}

Tier tier_of(const ModelInfo& model) {
  static constexpr std::string_view kFast[] = {"flash", "mini", "nano", "lite", "lightning",
                                               "haiku", "small", "tiny", "fast", "air"};
  // "thinking" is left out: the thinking variants worth it are opus/sonnet/pro.
  static constexpr std::string_view kStrong[] = {"opus", "pro", "ultra", "sonnet", "max",
                                                 "large"};
  const auto tokens = name_tokens(model.id + " " + model.name);
  auto has = [&](const auto& words) {
    return std::any_of(tokens.begin(), tokens.end(), [&](const std::string& t) {
      return std::find(std::begin(words), std::end(words), t) != std::end(words);
    });
  };
  if (has(kFast)) return Tier::kFast;  // "flash" beats "pro" in one name
  if (has(kStrong)) return Tier::kStrong;
  for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
    if (is_big_param_count(tokens[i], tokens[i + 1])) return Tier::kStrong;
  }
  return Tier::kStandard;
}

std::string_view to_string(Tier t) {
  switch (t) {
    case Tier::kStrong: return "strong";
    case Tier::kStandard: return "standard";
    case Tier::kFast: return "fast";
  }
  return "standard";
}

bool is_paid(const ProviderInfo& provider, const ModelInfo& model) {
  if (model.input_cost > 0 || model.output_cost > 0) return true;
  // Read on every call so tests and a changed environment apply at once.
  // Set but empty means no provider is paid by name.
  const char* env = std::getenv("QCODE_PAID_PROVIDERS");
  const std::string id = to_lower(provider.id);
  const std::string name = to_lower(provider.name);
  std::stringstream list(to_lower(env ? env : "anthropic,openai"));
  for (std::string item; std::getline(list, item, ',');) {
    item = trim(item);
    if (!item.empty() && (item == id || item == name)) return true;
  }
  return false;
}

std::string stats_key(std::string_view provider, std::string_view model,
                      std::string_view mode) {
  std::string key;
  key.reserve(provider.size() + model.size() + mode.size() + 2);
  key.append(provider).append(":").append(model).append("|").append(mode);
  return key;
}

std::vector<RankedTarget> rank_targets(const std::vector<ProviderInfo>& providers,
                                       const StatsTable& stats,
                                       const RouteRequest& req, std::mt19937& rng) {
  std::vector<RankedTarget> out;
  for (const auto& c : free_candidates(providers, req.lead_provider, req.lead_model,
                                       req.allow_cursor)) {
    const ProviderInfo& p = *c.provider;
    const ModelInfo& m = *c.model;
    const Tier tier = tier_of(m);
    const ArmStats mode_arm = arm(stats, p.id, m.id, req.mode);
    const ArmStats all = arm(stats, p.id, m.id, "*");
    const Beta post = posterior(tier, req.mode, mode_arm, all);

    const double success =
        req.difficulty == Difficulty::kHard ? conservative(post) : draw(post, rng);
    // A model that takes two minutes scores ~10% below an instant one.
    const double latency = std::pow(1 + all.latency_ms / 120000.0, -0.15);
    const auto busy = req.inflight_by_provider.find(p.id);
    const int inflight = busy == req.inflight_by_provider.end() ? 0 : busy->second;
    const double load = std::pow(0.8, inflight);

    RankedTarget t;
    t.provider = &p;
    t.model = &m;
    t.expected = post.mean();
    t.score = success * difficulty_fit(req.difficulty, tier, req.mode) * latency * load;
    t.resting = req.now < std::max(mode_arm.cooldown_until, all.cooldown_until);
    t.reason = p.id + ":" + m.id + " " + std::string(to_string(tier)) +
               " p=" + fixed(t.expected, 2) + " n=" + std::to_string(mode_arm.runs);
    if (all.latency_ms > 0) {
      t.reason += " lat=" + std::to_string(std::llround(all.latency_ms / 1000)) + "s";
    }
    t.reason += " load=" + std::to_string(inflight);
    if (t.resting) t.reason += " resting";
    out.push_back(std::move(t));
  }
  std::stable_sort(out.begin(), out.end(), [](const RankedTarget& a, const RankedTarget& b) {
    if (a.resting != b.resting) return !a.resting;
    return a.score > b.score;
  });
  return out;
}

Outcome classify_outcome(bool ok, std::string_view error) {
  if (ok) return Outcome::kSuccess;
  const std::string e = to_lower(error);
  if (contains_any(e, {"abort", "cancel"})) return Outcome::kAborted;
  // Provider trouble, not the model's answer: retry elsewhere, rest the arm.
  if (contains_any(e, {"rate limit", "rate_limit", "ratelimit", "quota", "too many requests",
                       "overloaded", "unavailable", "timeout", "timed out", "connection",
                       "network", "unauthorized", "forbidden", "missing api key", "auth",
                       "failed to resolve subagent client", "free tier", "credits"})) {
    return Outcome::kTransientError;
  }
  for (const auto code : {"429", "500", "502", "503", "504", "401", "402", "403"}) {
    if (has_status_code(e, code)) return Outcome::kTransientError;
  }
  return Outcome::kModelFailure;
}

double rating_quality(int score) {
  return (std::clamp(score, 1, 5) - 1) / 4.0;
}

int64_t cooldown_seconds(int consecutive, std::string_view error) {
  // A daily free-tier cap (OpenRouter's "free-models-per-day") will not lift
  // in minutes; retrying sooner only burns attempts.
  const std::string e = to_lower(error);
  if (contains_any(e, {"per day", "per-day", "daily", "day limit"})) return 86400;
  // 60 << 6 already passes the hour cap; clamping first avoids overflow.
  const int doublings = std::min(std::max(consecutive, 1) - 1, 6);
  return std::min<int64_t>(int64_t{60} << doublings, 3600);
}

std::string format_routing_table(const std::vector<ProviderInfo>& providers,
                                 const StatsTable& stats, std::string_view lead_provider,
                                 std::string_view lead_model, int64_t now,
                                 std::size_t max_rows) {
  struct Row {
    Candidate c;
    Tier tier = Tier::kStandard;
    double expected = 0;  // all-modes posterior mean
  };
  std::vector<Row> rows;
  for (const auto& c : free_candidates(providers, lead_provider, lead_model, false)) {
    const Tier tier = tier_of(*c.model);
    const ArmStats all = arm(stats, c.provider->id, c.model->id, "*");
    rows.push_back({c, tier, posterior(tier, "explore", all, all).mean()});
  }
  if (rows.empty()) return "";
  std::stable_sort(rows.begin(), rows.end(),
                   [](const Row& a, const Row& b) { return a.expected > b.expected; });
  if (rows.size() > max_rows) rows.resize(max_rows);

  std::ostringstream out;
  out << "### Subagent models (free, ranked by learned success)\n\n";
  for (const auto& row : rows) {
    const std::string& pid = row.c.provider->id;
    const std::string& mid = row.c.model->id;
    const ArmStats all = arm(stats, pid, mid, "*");
    out << "- `" << pid << ":" << mid << "` " << to_string(row.tier);

    bool any_runs = all.runs > 0;
    std::string modes;
    for (const std::string_view mode : {"explore", "implement", "verify"}) {
      const ArmStats m = arm(stats, pid, mid, mode);
      any_runs = any_runs || m.runs > 0;
      modes += " · ";
      modes += mode;
      if (m.runs > 0 && m.alpha + m.beta > 0) {
        modes += " " + std::to_string(std::lround(100 * m.alpha / (m.alpha + m.beta))) +
                 "% (" + std::to_string(m.runs) + ")";
      } else {
        modes += " –";
      }
    }
    if (any_runs) {
      out << modes;
      if (all.rated > 0) {
        out << " · ★" << fixed(all.rating_sum / all.rated, 1) << " (" << all.rated << ")";
      }
      if (all.latency_ms > 0) {
        out << " · ~" << short_duration(std::llround(all.latency_ms / 1000));
      }
    } else {
      out << " · new";
    }
    if (now < all.cooldown_until) out << " · resting " << short_duration(all.cooldown_until - now);
    out << "\n";
  }
  out << "\nOmit `model` to let the router pick (it learns from results); set `difficulty`; "
         "rate reports with `rate_task` (1-5).\n";
  return out.str();
}

std::map<std::string, int> inflight_snapshot() {
  auto& r = inflight_registry();
  std::lock_guard<std::mutex> lock(r.mu);
  return r.count;
}

ScopedInflight::ScopedInflight(std::string provider) : provider_(std::move(provider)) {
  auto& r = inflight_registry();
  std::lock_guard<std::mutex> lock(r.mu);
  ++r.count[provider_];
}

ScopedInflight::~ScopedInflight() {
  auto& r = inflight_registry();
  std::lock_guard<std::mutex> lock(r.mu);
  const auto it = r.count.find(provider_);
  if (it != r.count.end() && --it->second <= 0) r.count.erase(it);
}

}  // namespace qcode::routing
