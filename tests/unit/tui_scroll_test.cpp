#include <scroll_helpers.h>

#include <gtest/gtest.h>

namespace qcode {
namespace tui {
namespace {

TEST(TuiScrollTest, ComputeMaxScrollWhenContentExceedsViewport) {
    EXPECT_EQ(compute_max_scroll(100, 25), 75);
    EXPECT_EQ(compute_max_scroll(200, 40), 160);
    EXPECT_EQ(compute_max_scroll(50, 10), 40);
}

TEST(TuiScrollTest, ComputeMaxScrollWhenContentFitsInsideViewport) {
    EXPECT_EQ(compute_max_scroll(20, 25), 0);
    EXPECT_EQ(compute_max_scroll(25, 25), 0);
    EXPECT_EQ(compute_max_scroll(0, 25), 0);
}

TEST(TuiScrollTest, ComputeFocusYOffsetsCenteringBias) {
    // When viewport is 21 rows tall, centering bias is (21 - 1) / 2 = 10 rows.
    // FTXUI calculates: dy = focus_y - external_dimy / 2
    // For scroll_offset = 0: focus_y = 10 -> dy = 10 - 10 = 0.
    EXPECT_EQ(compute_focus_y(0, 21), 10);
    EXPECT_EQ(compute_focus_y(5, 21), 15);
    EXPECT_EQ(compute_focus_y(75, 21), 85);

    // Negative offset should clamp to 0
    EXPECT_EQ(compute_focus_y(-5, 21), 10);
}

} // namespace
} // namespace tui
} // namespace qcode
