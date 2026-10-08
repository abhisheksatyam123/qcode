#include <gtest/gtest.h>
#include <qcode/ui/app_store.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>
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
        .ok = true
    });

    bus.drain();
    auto& state = store.state();
    EXPECT_EQ(*state.model_calls, 1);
    EXPECT_DOUBLE_EQ(*state.total_model_ms, 1200.0);
    EXPECT_DOUBLE_EQ(*state.max_model_ms, 1200.0);
    EXPECT_DOUBLE_EQ(*state.last_model_ms, 1200.0);
    EXPECT_DOUBLE_EQ(*state.last_ttft_ms, 300.0);
    EXPECT_EQ(*state.total_reasoning_tokens, 10);
    EXPECT_EQ(*state.last_effort, "high");

    // Second step
    bus.publish<StepLatency>({
        .session_id = sid,
        .step = 2,
        .streamed = false,
        .model_ms = 1500.0,
        .ttft_ms = 400.0,
        .output_tokens = 60,
        .reasoning_tokens = 20,
        .effort = "high",
        .ok = true
    });

    bus.drain();
    EXPECT_EQ(*state.model_calls, 2);
    EXPECT_DOUBLE_EQ(*state.total_model_ms, 2700.0);
    EXPECT_DOUBLE_EQ(*state.max_model_ms, 1500.0);
    EXPECT_DOUBLE_EQ(*state.last_model_ms, 1500.0);
    EXPECT_DOUBLE_EQ(*state.last_ttft_ms, 400.0);
    EXPECT_EQ(*state.total_reasoning_tokens, 30);
    EXPECT_EQ(*state.last_effort, "high");

    // Faster third step, doesn't increase max
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
    EXPECT_EQ(*state.model_calls, 3);
    EXPECT_DOUBLE_EQ(*state.total_model_ms, 3500.0);
    EXPECT_DOUBLE_EQ(*state.max_model_ms, 1500.0); // unchanged
    EXPECT_DOUBLE_EQ(*state.last_model_ms, 800.0);
    EXPECT_DOUBLE_EQ(*state.last_ttft_ms, -1.0);
    EXPECT_EQ(*state.total_reasoning_tokens, 30); // unchanged
    EXPECT_EQ(*state.last_effort, "medium");
}

} // namespace test
} // namespace qcode
