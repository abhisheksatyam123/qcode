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

}  // namespace test
}  // namespace qcode
