#include <qcode/session/token_budget.h>
#include <qcode/core/message.h>

#include <gtest/gtest.h>
#include <string>

namespace qcode {
namespace {

TEST(TokenBudgetTest, EstimateEmptyIsZero) {
    EXPECT_EQ(estimate_tokens({}), 0u);
}

TEST(TokenBudgetTest, EstimateGrowsWithText) {
    Messages short_msgs{Message::user("hi")};
    Messages long_msgs{Message::user(std::string(400, 'a'))};
    EXPECT_GT(estimate_tokens(long_msgs), estimate_tokens(short_msgs));
    EXPECT_GT(estimate_system_tokens(std::string(40, 'x')), 0u);
}

TEST(TokenBudgetTest, CalibrateClampsAndPassthrough) {
    EXPECT_EQ(calibrate_estimate(100, 0, 0), 100u);
    EXPECT_EQ(calibrate_estimate(100, 0, 50), 100u);
    EXPECT_EQ(calibrate_estimate(100, 100, 100), 100u);
    // ratio 2.0 is the clamp ceiling
    EXPECT_EQ(calibrate_estimate(100, 400, 100), 200u);
    // ratio 0.5 is the clamp floor
    EXPECT_EQ(calibrate_estimate(100, 10, 100), 50u);
}

TEST(TokenBudgetTest, PruneKeepsRecentWhenOverBudget) {
    Messages msgs;
    for (int i = 0; i < 20; ++i) {
        msgs.push_back(Message::user(std::string(200, 'x')));
        msgs.push_back(Message::assistant(std::string(200, 'y')));
    }
    auto pruned = prune_context(msgs, /*context_window=*/80, /*keep_recent=*/4);
    EXPECT_LE(pruned.size(), msgs.size());
    EXPECT_GE(pruned.size(), 4u);
}

}  // namespace
}  // namespace qcode
