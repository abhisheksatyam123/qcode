#include <qcode/session/session_store.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>

namespace qcode {
namespace session {
namespace {

TEST(ModelPerformanceTelemetryTest, TelemetryPersistenceAndAggregation) {
    init_database();

    std::string test_model = "x-preview-f-free";

    // 1. Record generation turns
    record_generation_turn(test_model, "opencode", true, false, 250.0);
    record_generation_turn(test_model, "opencode", true, false, 350.0);
    record_generation_turn(test_model, "opencode", false, true, 500.0);

    // 2. Fetch performance summary
    auto summary = get_model_performance_summary(test_model, "opencode");
    EXPECT_EQ(summary.model_id, test_model);
    EXPECT_GE(summary.total_turns, 3);
    EXPECT_GE(summary.successful_turns, 2);
    EXPECT_GE(summary.tool_loop_failures, 1);

    // 3. Record user feedback
    record_user_feedback(test_model, true);
    record_user_feedback(test_model, true);
    record_user_feedback(test_model, false);

    auto summary2 = get_model_performance_summary(test_model, "opencode");
    EXPECT_GE(summary2.positive_feedback_count, 2);
    EXPECT_GE(summary2.negative_feedback_count, 1);

    // 4. Test list all summaries
    auto all_summaries = list_all_model_performance_summaries();
    EXPECT_FALSE(all_summaries.empty());
}

} // namespace
} // namespace session
} // namespace qcode
