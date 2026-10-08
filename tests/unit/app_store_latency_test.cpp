#include <gtest/gtest.h>
#include <qcode/config/provider_info.h>
#include <qcode/core/event.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/session/usage_stats.h>
#include <qcode/ui/app_store.h>
#include <qcode/ui/chat_state.h>
#include <memory>
#include <string>

namespace qcode {
namespace test {

using namespace qcode::contract;

TEST(AppStoreLatencyTest, AccumulatesLatencyMetrics) {
    bus::BusRuntime bus;
    qcode::contract::register_all_events(bus);
    qcode::AppStore store(bus);
    store.wire();
    const std::string sid = "latency-test-session";
    store.set_session_id(sid);

    // First step
    bus.publish<StepLatency>({
        .session_id = sid,
        .step = 1,
        .streamed = false,
        .model_ms = 1200.0,
        .ttft_ms = 300.0,
        .output_tokens = 50,
        .reasoning_tokens = 10,
        .effort = "high",
        .ok = true,
        .input_tokens = 1000,
        .cache_read_tokens = 800,
        .cache_write_tokens = 100,
        .variant = "high",
    });

    bus.drain();
    auto& state = store.state();
    const auto& usage = *state.usage;
    EXPECT_EQ(usage.model_calls, 1);
    EXPECT_DOUBLE_EQ(usage.model_ms_total, 1200.0);
    EXPECT_DOUBLE_EQ(usage.model_ms_max, 1200.0);
    EXPECT_DOUBLE_EQ(usage.model_ms_last, 1200.0);
    EXPECT_DOUBLE_EQ(usage.ttft_ms_last, 300.0);
    EXPECT_EQ(usage.reasoning_tokens, 10);
    EXPECT_EQ(usage.last_effort, "high");
    EXPECT_EQ(usage.input_tokens, 1000);
    EXPECT_EQ(usage.cache_read_tokens, 800);
    EXPECT_EQ(usage.cache_write_tokens, 100);

    // Second step
    bus.publish<StepLatency>({
        .session_id = sid,
        .step = 2,
        .streamed = false,
        .model_ms = 1500.0,
        .ttft_ms = 400.0,
        .output_tokens = 60,
        .reasoning_tokens = 20,
        .effort = "max",
        .ok = true,
        .input_tokens = 1200,
        .cache_read_tokens = 1000,
        .variant = "ultra",
    });

    bus.drain();
    EXPECT_EQ(usage.model_calls, 2);
    EXPECT_DOUBLE_EQ(usage.model_ms_total, 2700.0);
    EXPECT_DOUBLE_EQ(usage.model_ms_max, 1500.0);
    EXPECT_DOUBLE_EQ(usage.model_ms_last, 1500.0);
    EXPECT_DOUBLE_EQ(usage.ttft_ms_last, 400.0);
    EXPECT_DOUBLE_EQ(usage.ttft_ms_total, 700.0);
    EXPECT_EQ(usage.ttft_count, 2);
    EXPECT_EQ(usage.reasoning_tokens, 30);
    EXPECT_EQ(usage.output_tokens, 110);
    EXPECT_EQ(usage.last_effort, "max");
    EXPECT_EQ(usage.last_variant, "ultra");

    // Faster third step without first-token timing; doesn't increase max.
    bus.publish<StepLatency>({
        .session_id = sid,
        .step = 3,
        .streamed = true,
        .model_ms = 800.0,
        .ttft_ms = -1.0,
        .output_tokens = 10,
        .reasoning_tokens = 0,
        .effort = "medium",
        .ok = true
    });

    bus.drain();
    EXPECT_EQ(usage.model_calls, 3);
    EXPECT_DOUBLE_EQ(usage.model_ms_total, 3500.0);
    EXPECT_DOUBLE_EQ(usage.model_ms_max, 1500.0);  // unchanged
    EXPECT_DOUBLE_EQ(usage.model_ms_last, 800.0);
    EXPECT_DOUBLE_EQ(usage.ttft_ms_last, -1.0);
    EXPECT_EQ(usage.ttft_count, 2);  // calls without TTFT don't skew the average
    EXPECT_EQ(usage.reasoning_tokens, 30);  // unchanged
    EXPECT_EQ(usage.last_effort, "medium");
    EXPECT_EQ(usage.last_variant, "ultra");  // empty variant keeps the last one

    // Another session's call never lands in this session's mirror.
    bus.publish<StepLatency>({.session_id = "other-session", .model_ms = 9999.0});
    bus.drain();
    EXPECT_EQ(usage.model_calls, 3);

    // Cost travels with the call (priced when it ran), per serving model.
    EXPECT_EQ(usage.unpriced_calls, 3);
    bus.publish<StepLatency>({
        .session_id = sid,
        .model_ms = 100.0,
        .output_tokens = 10,
        .ok = true,
        .input_tokens = 100,
        .provider = "anthropic",
        .model = "claude-opus-5-5",
        .priced = true,
        .cost_input = 0.01,
        .cost_output = 0.02,
    });
    bus.drain();
    EXPECT_EQ(usage.priced_calls, 1);
    EXPECT_NEAR(usage.cost.total(), 0.03, 1e-12);
    ASSERT_EQ(usage.by_model.count("anthropic/claude-opus-5-5"), 1u);
    EXPECT_EQ(usage.by_model.at("anthropic/claude-opus-5-5").calls, 1);
    EXPECT_EQ(usage.last_input_tokens, 100);
}

TEST(UsageStatsTest, JsonRoundTripAndCost) {
    session::SessionUsageStats stats;
    stats.add({.model_ms = 2000.0,
               .ttft_ms = 500.0,
               .input_tokens = 1'000'000,
               .cache_read_tokens = 800'000,
               .cache_write_tokens = 100'000,
               .output_tokens = 50'000,
               .reasoning_tokens = 20'000,
               .effort = "max",
               .variant = "ultra"});
    const auto back = session::SessionUsageStats::from_json(stats.to_json());
    EXPECT_EQ(back.model_calls, 1);
    EXPECT_EQ(back.input_tokens, 1'000'000);
    EXPECT_EQ(back.cache_read_tokens, 800'000);
    EXPECT_EQ(back.cache_write_tokens, 100'000);
    EXPECT_EQ(back.output_tokens, 50'000);
    EXPECT_EQ(back.reasoning_tokens, 20'000);
    EXPECT_DOUBLE_EQ(back.ttft_ms_last, 500.0);
    EXPECT_EQ(back.last_variant, "ultra");
    EXPECT_TRUE(session::SessionUsageStats::from_json(nlohmann::json()).empty());

    // Opus 5.5 list prices (USD / 1M): 4 in, 20 out, 0.2 cache read, 5 cache write.
    ModelInfo model;
    model.input_cost = 4.0;
    model.output_cost = 20.0;
    model.cache_read_cost = 0.2;
    model.cache_write_cost = 5.0;
    const auto cost = session::estimate_usage_cost(back, model);
    ASSERT_TRUE(cost.priced);
    EXPECT_NEAR(cost.input, 0.4, 1e-9);        // 100k uncached x $4
    EXPECT_NEAR(cost.cache_read, 0.16, 1e-9);  // 800k x $0.2
    EXPECT_NEAR(cost.cache_write, 0.5, 1e-9);  // 100k x $5
    EXPECT_NEAR(cost.output, 1.0, 1e-9);       // 50k x $20
    EXPECT_NEAR(cost.total(), 2.06, 1e-9);

    // No configured price -> unpriced (no invented fallback rates).
    EXPECT_FALSE(session::estimate_usage_cost(back, ModelInfo{}).priced);
}

namespace {
ModelInfo priced_model(std::string id, double in, double out, double read = 0.0,
                       double write = 0.0) {
    ModelInfo m;
    m.id = std::move(id);
    m.input_cost = in;
    m.output_cost = out;
    m.cache_read_cost = read;
    m.cache_write_cost = write;
    return m;
}

session::ModelCallUsage call_on(const ModelInfo& model, int input, int output,
                                int cache_read = 0) {
    session::ModelCallUsage call{.model_ms = 1000.0,
                                 .ttft_ms = 100.0,
                                 .input_tokens = input,
                                 .cache_read_tokens = cache_read,
                                 .output_tokens = output,
                                 .effort = "high",
                                 .provider = "anthropic",
                                 .model = model.id,
                                 .cost = {}};
    call.cost = session::price_call(call, model);
    return call;
}
}  // namespace

TEST(UsageStatsTest, EachCallKeepsItsOwnModelPrice) {
    const ModelInfo opus = priced_model("claude-opus-5-5", 4.0, 20.0, 0.2, 5.0);
    const ModelInfo haiku = priced_model("claude-haiku-5-5", 0.1, 0.5, 0.01, 0.125);
    session::SessionUsageStats stats;
    stats.add(call_on(opus, 1'000'000, 100'000, 500'000));  // $2 + $0.1 + $2 = $4.1
    stats.add(call_on(haiku, 1'000'000, 100'000));          // $0.1 + $0.05 = $0.15
    EXPECT_EQ(stats.priced_calls, 2);
    EXPECT_NEAR(stats.cost.total(), 4.25, 1e-9);
    ASSERT_EQ(stats.by_model.size(), 2u);
    EXPECT_NEAR(stats.by_model.at("anthropic/claude-opus-5-5").cost.total(), 4.1, 1e-9);
    EXPECT_NEAR(stats.by_model.at("anthropic/claude-haiku-5-5").cost.total(), 0.15, 1e-9);

    // The session total does not depend on which model is selected now.
    const auto now_haiku = session::session_cost(stats, &haiku);
    EXPECT_TRUE(now_haiku.available);
    EXPECT_FALSE(now_haiku.estimated);
    EXPECT_NEAR(now_haiku.total, 4.25, 1e-9);
    EXPECT_NEAR(session::session_cost(stats, nullptr).total, 4.25, 1e-9);

    // Round trip keeps per-call cost and the per-model split.
    const auto back = session::SessionUsageStats::from_json(stats.to_json());
    EXPECT_EQ(back.legacy_calls, 0);
    EXPECT_NEAR(back.cost.total(), 4.25, 1e-9);
    EXPECT_EQ(back.by_model.at("anthropic/claude-opus-5-5").cache_read_tokens, 500'000);
    EXPECT_EQ(back.last_input_tokens, 1'000'000);

    // A call on a model without a price is counted but not costed.
    session::SessionUsageStats mixed = back;
    ModelInfo free_model;
    free_model.id = "free";
    mixed.add(call_on(free_model, 10, 10));
    EXPECT_EQ(mixed.unpriced_calls, 1);
    const auto mixed_cost = session::session_cost(mixed, &opus);
    EXPECT_NEAR(mixed_cost.total, 4.25, 1e-9);
    EXPECT_EQ(mixed_cost.unpriced_calls, 1);
}

TEST(UsageStatsTest, LegacyStatsAreEstimatedUntilPricedCallsExist) {
    // usage_stats written before per-call pricing: tokens only, no "cost".
    const nlohmann::json legacy = {{"model_calls", 2},
                                   {"input_tokens", 1'000'000},
                                   {"output_tokens", 100'000}};
    auto stats = session::SessionUsageStats::from_json(legacy);
    EXPECT_EQ(stats.legacy_calls, 2);
    const ModelInfo opus = priced_model("claude-opus-5-5", 4.0, 20.0);
    const auto estimated = session::session_cost(stats, &opus);
    EXPECT_TRUE(estimated.available);
    EXPECT_TRUE(estimated.estimated);
    EXPECT_NEAR(estimated.total, 6.0, 1e-9);  // $4 in + $2 out at today's price

    // Once priced calls exist, only they count; the old ones are reported.
    stats.add(call_on(opus, 1000, 100));
    const auto exact = session::session_cost(stats, &opus);
    EXPECT_FALSE(exact.estimated);
    EXPECT_EQ(exact.legacy_calls, 2);
    EXPECT_NEAR(exact.total, 0.006, 1e-9);
    EXPECT_EQ(session::SessionUsageStats::from_json(stats.to_json()).legacy_calls, 2);

    // Sessions older than usage stats: turn totals at the current price.
    const auto turns = session::session_cost(session::SessionUsageStats{}, &opus, 500'000, 0);
    EXPECT_TRUE(turns.estimated);
    EXPECT_NEAR(turns.total, 2.0, 1e-9);
    EXPECT_FALSE(session::session_cost(session::SessionUsageStats{}, nullptr, 10, 10).available);
}

TEST(UsageStatsTest, SummaryJsonAddsDerivedFigures) {
    const ModelInfo opus = priced_model("claude-opus-5-5", 4.0, 20.0, 0.2, 5.0);
    session::SessionUsageStats stats;
    stats.add(call_on(opus, 1000, 500, 800));
    stats.add(call_on(opus, 1000, 500, 600));
    const auto j = session::usage_summary_json(stats, &opus);
    EXPECT_EQ(j["model_calls"], 2);
    EXPECT_DOUBLE_EQ(j["avg_call_ms"].get<double>(), 1000.0);
    EXPECT_DOUBLE_EQ(j["avg_ttft_ms"].get<double>(), 100.0);
    EXPECT_DOUBLE_EQ(j["cache_hit_pct"].get<double>(), 70.0);
    EXPECT_DOUBLE_EQ(j["output_tok_per_s"].get<double>(), 500.0);
    EXPECT_EQ(j["uncached_input_tokens"], 600);
    EXPECT_TRUE(j["session_cost"]["available"].get<bool>());
    EXPECT_FALSE(j["session_cost"]["estimated"].get<bool>());
    EXPECT_NEAR(j["session_cost"]["total"].get<double>(), stats.cost.total(), 1e-12);
    EXPECT_TRUE(j["by_model"].contains("anthropic/claude-opus-5-5"));
    EXPECT_TRUE(session::usage_summary_json({}, nullptr)["avg_ttft_ms"].is_null());
}

}  // namespace test
}  // namespace qcode
