#include <qcode/tools/subagent_router.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace qcode::routing {
namespace {

ModelInfo model(std::string id, std::string name = "") {
  ModelInfo m;
  m.id = std::move(id);
  m.name = std::move(name);
  m.tool_call = true;
  m.context_window = 200000;
  return m;
}

// api_key set so is_provider_authenticated() needs no token files or env.
ProviderInfo provider(std::string id, std::vector<ModelInfo> models) {
  ProviderInfo p;
  p.id = id;
  p.name = std::move(id);
  p.api_url = "https://example.invalid";
  p.api_key = "test-key";
  p.models = std::move(models);
  return p;
}

std::vector<ProviderInfo> catalog() {
  return {
      provider("anthropic", {model("claude-opus-5-5")}),
      provider("antigravity", {model("gemini-3.1-pro"), model("claude-opus-5-5-thinking")}),
      provider("opencode", {model("space-bunny-free"), model("mimo-v2.6-flash-free")}),
      provider("openrouter", {model("nvidia/nemotron-3.5-lightning:free")}),
      provider("cursor", {model("cursor-grok-4.6")}),
  };
}

std::vector<std::string> ids(const std::vector<RankedTarget>& ranked) {
  std::vector<std::string> out;
  for (const auto& t : ranked) out.push_back(t.provider->id + ":" + t.model->id);
  return out;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

// Arm with `wins` successes and `losses` failures (q = 1 / 0).
ArmStats record(double wins, double losses) {
  ArmStats a;
  a.alpha = wins;
  a.beta = losses;
  a.runs = static_cast<int>(std::lround(wins + losses));
  a.failures = static_cast<int>(std::lround(losses));
  return a;
}

// Sets an environment variable for one test and restores it after.
class ScopedEnv {
 public:
  ScopedEnv(const char* name, const char* value) : name_(name) {
    if (const char* old = std::getenv(name)) old_ = old;
    if (value) {
      setenv(name, value, 1);
    } else {
      unsetenv(name);
    }
  }
  ~ScopedEnv() {
    if (old_) {
      setenv(name_, old_->c_str(), 1);
    } else {
      unsetenv(name_);
    }
  }

 private:
  const char* name_;
  std::optional<std::string> old_;
};

// Every test starts from the default paid list, whatever the shell sets.
class SubagentRouterTest : public ::testing::Test {
 private:
  ScopedEnv paid_{"QCODE_PAID_PROVIDERS", nullptr};
};

// Display width of a UTF-8 line ("·", "–" and "★" are one column each).
std::size_t columns(std::string_view line) {
  return static_cast<std::size_t>(std::count_if(line.begin(), line.end(), [](char c) {
    return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
  }));
}

TEST_F(SubagentRouterTest, ParseDifficulty) {
  EXPECT_EQ(parse_difficulty("easy"), Difficulty::kEasy);
  EXPECT_EQ(parse_difficulty("Trivial"), Difficulty::kEasy);
  EXPECT_EQ(parse_difficulty("simple"), Difficulty::kEasy);
  EXPECT_EQ(parse_difficulty("HARD"), Difficulty::kHard);
  EXPECT_EQ(parse_difficulty("complex"), Difficulty::kHard);
  EXPECT_EQ(parse_difficulty("difficult"), Difficulty::kHard);
  EXPECT_EQ(parse_difficulty("medium"), Difficulty::kMedium);
  EXPECT_EQ(parse_difficulty(""), Difficulty::kMedium);
  EXPECT_EQ(parse_difficulty("whatever"), Difficulty::kMedium);
  EXPECT_EQ(to_string(Difficulty::kHard), "hard");
  EXPECT_EQ(to_string(Tier::kFast), "fast");
}

TEST_F(SubagentRouterTest, TierFromModelName) {
  EXPECT_EQ(tier_of(model("gemini-3.8-flash")), Tier::kFast);
  EXPECT_EQ(tier_of(model("gemini-3.1-pro")), Tier::kStrong);
  EXPECT_EQ(tier_of(model("claude-sonnet-4-6")), Tier::kStrong);
  EXPECT_EQ(tier_of(model("claude-opus-5-5-thinking")), Tier::kStrong);
  EXPECT_EQ(tier_of(model("claude-haiku-5-5")), Tier::kFast);
  EXPECT_EQ(tier_of(model("nvidia/nemotron-3.5-lightning:free")), Tier::kFast);
  EXPECT_EQ(tier_of(model("nvidia/nemotron-3-ultra-550b-a55b:free")), Tier::kStrong);
  EXPECT_EQ(tier_of(model("cohere/north-mini-code:free")), Tier::kFast);
  EXPECT_EQ(tier_of(model("space-bunny-free")), Tier::kStandard);
  EXPECT_EQ(tier_of(model("longcat-2.5-preview-free")), Tier::kStandard);
  EXPECT_EQ(tier_of(model("mimo-v2.6-flash-free")), Tier::kFast);
  EXPECT_EQ(tier_of(model("exo-free")), Tier::kStandard);
  // Parameter count alone; small counts and "a55b" active params do not count.
  EXPECT_EQ(tier_of(model("qwen3-coder-480b-a35b")), Tier::kStrong);
  EXPECT_EQ(tier_of(model("llama-3.1-8b")), Tier::kStandard);
  // The display name counts too; "gemini" is not "mini".
  EXPECT_EQ(tier_of(model("g-31", "Gemini 3.1 Pro (High)")), Tier::kStrong);
  EXPECT_EQ(tier_of(model("gemini-3")), Tier::kStandard);
}

TEST_F(SubagentRouterTest, PaidByProviderOrCost) {
  EXPECT_TRUE(is_paid(provider("anthropic", {}), model("claude-opus-5-5")));
  EXPECT_TRUE(is_paid(provider("OpenAI", {}), model("gpt-6")));
  EXPECT_FALSE(is_paid(provider("antigravity", {}), model("claude-opus-5-5-thinking")));
  ModelInfo priced = model("some-model");
  priced.output_cost = 2.5;
  EXPECT_TRUE(is_paid(provider("openrouter", {}), priced));
  EXPECT_EQ(stats_key("antigravity", "gemini-3.1-pro", "*"), "antigravity:gemini-3.1-pro|*");
}

TEST_F(SubagentRouterTest, OnlyFreeModelsAreCandidates) {
  const auto providers = catalog();
  std::mt19937 rng(1);
  RouteRequest req;
  req.lead_provider = "anthropic";
  req.lead_model = "claude-opus-5-5";
  const auto ranked = ids(rank_targets(providers, {}, req, rng));
  EXPECT_FALSE(contains(ranked, "anthropic:claude-opus-5-5"));
  EXPECT_TRUE(contains(ranked, "antigravity:claude-opus-5-5-thinking"));
  EXPECT_TRUE(contains(ranked, "antigravity:gemini-3.1-pro"));
  EXPECT_TRUE(contains(ranked, "opencode:space-bunny-free"));
  EXPECT_TRUE(contains(ranked, "openrouter:nvidia/nemotron-3.5-lightning:free"));
  EXPECT_FALSE(contains(ranked, "cursor:cursor-grok-4.6"));
  EXPECT_EQ(ranked.size(), 5u);
}

TEST_F(SubagentRouterTest, PaidProvidersComeFromEnvironment) {
  ScopedEnv env("QCODE_PAID_PROVIDERS", " Antigravity , opencode");
  std::mt19937 rng(1);
  const auto ranked = ids(rank_targets(catalog(), {}, RouteRequest{}, rng));
  EXPECT_TRUE(contains(ranked, "anthropic:claude-opus-5-5"));
  EXPECT_FALSE(contains(ranked, "antigravity:gemini-3.1-pro"));
  EXPECT_FALSE(contains(ranked, "opencode:space-bunny-free"));
  EXPECT_TRUE(contains(ranked, "openrouter:nvidia/nemotron-3.5-lightning:free"));
}

TEST_F(SubagentRouterTest, LeadModelIsNeverPicked) {
  std::mt19937 rng(1);
  RouteRequest req;
  req.lead_provider = "antigravity";
  req.lead_model = "gemini-3.1-pro";
  const auto ranked = ids(rank_targets(catalog(), {}, req, rng));
  EXPECT_FALSE(contains(ranked, "antigravity:gemini-3.1-pro"));
  EXPECT_TRUE(contains(ranked, "antigravity:claude-opus-5-5-thinking"));
}

TEST_F(SubagentRouterTest, CursorOnlyWhenAllowed) {
  std::mt19937 rng(1);
  RouteRequest req;
  req.allow_cursor = true;
  EXPECT_TRUE(contains(ids(rank_targets(catalog(), {}, req, rng)), "cursor:cursor-grok-4.6"));
}

TEST_F(SubagentRouterTest, StrongRecordWinsMostDraws) {
  const std::vector<ProviderInfo> providers = {
      provider("opencode", {model("alpha-free"), model("beta-free")})};
  StatsTable stats;
  stats[stats_key("opencode", "alpha-free", "explore")] = record(6, 14);
  stats[stats_key("opencode", "alpha-free", "*")] = record(6, 14);
  stats[stats_key("opencode", "beta-free", "explore")] = record(18, 2);
  stats[stats_key("opencode", "beta-free", "*")] = record(18, 2);

  std::mt19937 rng(42);
  int strong_first = 0;
  for (int i = 0; i < 200; ++i) {
    const auto ranked = rank_targets(providers, stats, RouteRequest{}, rng);
    ASSERT_EQ(ranked.size(), 2u);
    if (ranked.front().model->id == "beta-free") ++strong_first;
  }
  EXPECT_GE(strong_first, 160);

  const auto ranked = rank_targets(providers, stats, RouteRequest{}, rng);
  for (const auto& t : ranked) {
    if (t.model->id == "beta-free") {
      EXPECT_NEAR(t.expected, (2.4 + 18) / 24.0, 1e-9);
      EXPECT_EQ(t.reason, "opencode:beta-free standard p=0.85 n=20 load=0");
    }
  }
}

TEST_F(SubagentRouterTest, HardJobsGoToProvenModelsDeterministically) {
  const std::vector<ProviderInfo> providers = {
      provider("opencode", {model("mimo-v2.6-flash-free"), model("space-bunny-free")}),
      provider("antigravity", {model("gemini-3.1-pro")}),
  };
  StatsTable stats;
  // Same strong record for the fast and the standard model; the pro is unproven.
  for (const char* id : {"mimo-v2.6-flash-free", "space-bunny-free"}) {
    stats[stats_key("opencode", id, "implement")] = record(19, 1);
    stats[stats_key("opencode", id, "*")] = record(19, 1);
  }
  RouteRequest req;
  req.mode = "implement";
  req.difficulty = Difficulty::kHard;

  std::mt19937 rng(7);
  const std::mt19937 before = rng;
  const auto first = ids(rank_targets(providers, stats, req, rng));
  EXPECT_EQ(rng, before);  // no draws for hard jobs
  std::mt19937 other(12345);
  EXPECT_EQ(ids(rank_targets(providers, stats, req, other)), first);
  ASSERT_EQ(first.size(), 3u);
  EXPECT_EQ(first[0], "opencode:space-bunny-free");
  EXPECT_EQ(first[1], "antigravity:gemini-3.1-pro");
  EXPECT_EQ(first[2], "opencode:mimo-v2.6-flash-free");
}

TEST_F(SubagentRouterTest, RestingArmsRankLast) {
  const std::vector<ProviderInfo> providers = {
      provider("antigravity", {model("gemini-3.1-pro")}),
      provider("opencode", {model("space-bunny-free")}),
  };
  StatsTable stats;
  ArmStats all = record(30, 0);
  all.cooldown_until = 1600;
  stats[stats_key("antigravity", "gemini-3.1-pro", "*")] = all;
  stats[stats_key("antigravity", "gemini-3.1-pro", "explore")] = record(30, 0);
  RouteRequest req;
  req.now = 1000;

  std::mt19937 rng(3);
  for (int i = 0; i < 20; ++i) {
    const auto ranked = rank_targets(providers, stats, req, rng);
    ASSERT_EQ(ranked.size(), 2u);
    EXPECT_EQ(ranked[0].model->id, "space-bunny-free");
    EXPECT_FALSE(ranked[0].resting);
    EXPECT_TRUE(ranked[1].resting);
    EXPECT_NE(ranked[1].reason.find(" resting"), std::string::npos);
  }
  req.now = 1600;  // cooldown over
  EXPECT_FALSE(rank_targets(providers, stats, req, rng)[0].resting);
}

TEST_F(SubagentRouterTest, InflightLoadSpreadsJobs) {
  const std::vector<ProviderInfo> providers = {
      provider("openrouter", {model("exo-free")}),
      provider("opencode", {model("space-bunny-free")}),
  };
  RouteRequest req;
  req.inflight_by_provider["openrouter"] = 3;
  std::mt19937 rng(11);
  int idle_first = 0;
  for (int i = 0; i < 200; ++i) {
    if (rank_targets(providers, {}, req, rng).front().provider->id == "opencode") ++idle_first;
  }
  EXPECT_GE(idle_first, 120);
  EXPECT_LT(idle_first, 200);  // still a chance for the busy provider
}

TEST_F(SubagentRouterTest, ClassifyOutcome) {
  EXPECT_EQ(classify_outcome(true, ""), Outcome::kSuccess);
  EXPECT_EQ(classify_outcome(true, "timeout"), Outcome::kSuccess);
  EXPECT_EQ(classify_outcome(false, "Aborted by user"), Outcome::kAborted);
  EXPECT_EQ(classify_outcome(false, "request cancelled"), Outcome::kAborted);
  EXPECT_EQ(classify_outcome(false, "HTTP 429 Too Many Requests"), Outcome::kTransientError);
  EXPECT_EQ(classify_outcome(false, "upstream status=503"), Outcome::kTransientError);
  EXPECT_EQ(classify_outcome(false, "(502) bad gateway"), Outcome::kTransientError);
  EXPECT_EQ(classify_outcome(false, "Rate limit exceeded: free-models-per-day"),
            Outcome::kTransientError);
  EXPECT_EQ(classify_outcome(false, "request timed out after 600s"), Outcome::kTransientError);
  EXPECT_EQ(classify_outcome(false, "Unauthorized"), Outcome::kTransientError);
  EXPECT_EQ(classify_outcome(false, "Failed to resolve subagent client"),
            Outcome::kTransientError);
  EXPECT_EQ(classify_outcome(false, "Insufficient credits"), Outcome::kTransientError);
  EXPECT_EQ(classify_outcome(false, "empty output"), Outcome::kModelFailure);
  EXPECT_EQ(classify_outcome(false, ""), Outcome::kModelFailure);
  // Numbers that merely contain a status code are not status codes.
  EXPECT_EQ(classify_outcome(false, "stuck in a loop after 4290 steps"), Outcome::kModelFailure);
  EXPECT_EQ(classify_outcome(false, "wrote 5000 lines of invalid tool calls"),
            Outcome::kModelFailure);
}

TEST_F(SubagentRouterTest, RatingQuality) {
  EXPECT_DOUBLE_EQ(rating_quality(1), 0.0);
  EXPECT_DOUBLE_EQ(rating_quality(3), 0.5);
  EXPECT_DOUBLE_EQ(rating_quality(5), 1.0);
  EXPECT_DOUBLE_EQ(rating_quality(0), 0.0);
  EXPECT_DOUBLE_EQ(rating_quality(9), 1.0);
}

TEST_F(SubagentRouterTest, CooldownBacksOffToAnHour) {
  EXPECT_EQ(cooldown_seconds(0, "HTTP 429"), 60);
  EXPECT_EQ(cooldown_seconds(1, "HTTP 429"), 60);
  EXPECT_EQ(cooldown_seconds(2, "overloaded"), 120);
  EXPECT_EQ(cooldown_seconds(3, ""), 240);
  EXPECT_EQ(cooldown_seconds(6, ""), 1920);
  EXPECT_EQ(cooldown_seconds(7, ""), 3600);
  EXPECT_EQ(cooldown_seconds(1000, ""), 3600);
  EXPECT_EQ(cooldown_seconds(1, "Rate limit exceeded: free-models-per-day"), 86400);
  EXPECT_EQ(cooldown_seconds(1, "Daily quota exhausted"), 86400);
}

TEST_F(SubagentRouterTest, RoutingTableForTheLead) {
  StatsTable stats;
  ArmStats all = record(20, 5);
  all.rated = 6;
  all.rating_sum = 24.6;
  all.latency_ms = 40000;
  all.cooldown_until = 1240;
  stats[stats_key("antigravity", "gemini-3.1-pro", "*")] = all;
  stats[stats_key("antigravity", "gemini-3.1-pro", "explore")] = record(23, 2);
  stats[stats_key("antigravity", "gemini-3.1-pro", "implement")] = record(4.9, 2.1);
  stats[stats_key("opencode", "mimo-v2.6-flash-free", "*")] = record(1, 9);
  stats[stats_key("opencode", "mimo-v2.6-flash-free", "explore")] = record(1, 9);

  const std::string table =
      format_routing_table(catalog(), stats, "antigravity", "claude-opus-5-5-thinking", 1000);
  EXPECT_EQ(table.rfind("### Subagent models (free, ranked by learned success)\n", 0), 0u);
  EXPECT_NE(table.find("- `antigravity:gemini-3.1-pro` strong · explore 92% (25) · implement "
                       "70% (7) · verify – · ★4.1 (6) · ~40s · resting 4m\n"),
            std::string::npos)
      << table;
  EXPECT_NE(table.find("- `opencode:space-bunny-free` standard · new\n"), std::string::npos)
      << table;
  EXPECT_EQ(table.find("anthropic:"), std::string::npos);
  EXPECT_EQ(table.find("claude-opus-5-5-thinking"), std::string::npos);
  EXPECT_EQ(table.find("cursor"), std::string::npos);
  EXPECT_NE(table.find("`rate_task` (1-5)"), std::string::npos);
  // Best record first, the failing model last.
  EXPECT_LT(table.find("gemini-3.1-pro"), table.find("space-bunny-free"));
  EXPECT_LT(table.find("space-bunny-free"), table.find("mimo-v2.6-flash-free"));
  for (std::size_t start = 0, end; (end = table.find('\n', start)) != std::string::npos;
       start = end + 1) {
    EXPECT_LE(columns(std::string_view(table).substr(start, end - start)), 130u);
  }

  const std::string two = format_routing_table(catalog(), stats, "", "", 1000, 2);
  EXPECT_NE(two.find("gemini-3.1-pro"), std::string::npos);
  EXPECT_EQ(two.find("mimo-v2.6-flash-free"), std::string::npos);

  const std::vector<ProviderInfo> paid_only = {provider("anthropic", {model("claude-opus-5-5")})};
  EXPECT_EQ(format_routing_table(paid_only, stats, "", "", 1000), "");
}

TEST_F(SubagentRouterTest, ScopedInflightCountsPerProvider) {
  const std::string id = "router-test-provider";
  EXPECT_EQ(inflight_snapshot().count(id), 0u);
  {
    ScopedInflight a(id);
    {
      ScopedInflight b(id);
      EXPECT_EQ(inflight_snapshot().at(id), 2);
    }
    EXPECT_EQ(inflight_snapshot().at(id), 1);
  }
  EXPECT_EQ(inflight_snapshot().count(id), 0u);
}

}  // namespace
}  // namespace qcode::routing
