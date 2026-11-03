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

namespace qcode {
namespace tui {
namespace {

int sum(const std::vector<int>& v, size_t b, size_t e) {
    int s = 0;
    for (size_t i = b; i < e; ++i) s += v[i];
    return s;
}

TEST(TuiVisibleRowsTest, KeepsOnlyRowsNearTheViewport) {
    std::vector<int> heights(100, 10);  // 1000 lines
    const auto s = visible_rows(heights, /*top=*/500, /*viewport=*/40, /*overscan=*/0);
    EXPECT_EQ(s.begin, 50u);
    EXPECT_EQ(s.end, 54u);
    EXPECT_EQ(s.top_pad, 500);
    EXPECT_EQ(s.bottom_pad, 460);
}

TEST(TuiVisibleRowsTest, PaddingPlusVisibleEqualsTotalHeight) {
    std::vector<int> heights = {3, 0, 17, 1, 40, 2, 9, 9, 5, 120, 4};
    int total = 0;
    for (int h : heights) total += h;
    for (int top = 0; top <= total; ++top) {
        for (int overscan : {0, 5, 50}) {
            const auto s = visible_rows(heights, top, 12, overscan);
            ASSERT_LE(s.begin, s.end);
            EXPECT_EQ(s.top_pad + sum(heights, s.begin, s.end) + s.bottom_pad, total);
            // Every line of the viewport is covered by a kept row.
            EXPECT_LE(s.top_pad, std::max(0, top - overscan));
            if (s.end < heights.size()) {
                EXPECT_GE(s.top_pad + sum(heights, s.begin, s.end), top + 12 + overscan);
            }
        }
    }
}

TEST(TuiVisibleRowsTest, OverscanWidensTheWindow) {
    std::vector<int> heights(100, 10);
    const auto s = visible_rows(heights, 500, 40, 40);
    EXPECT_EQ(s.begin, 46u);
    EXPECT_EQ(s.end, 58u);
}

TEST(TuiVisibleRowsTest, ShortListIsKeptWhole) {
    std::vector<int> heights = {2, 3, 4};
    const auto s = visible_rows(heights, 0, 40, 40);
    EXPECT_EQ(s.begin, 0u);
    EXPECT_EQ(s.end, 3u);
    EXPECT_EQ(s.top_pad, 0);
    EXPECT_EQ(s.bottom_pad, 0);
}

TEST(TuiVisibleRowsTest, EmptyList) {
    const auto s = visible_rows({}, 10, 40, 40);
    EXPECT_EQ(s.begin, 0u);
    EXPECT_EQ(s.end, 0u);
}

}  // namespace
}  // namespace tui
}  // namespace qcode
