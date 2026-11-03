#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace qcode {
namespace tui {

// Calculate the maximum vertical scroll offset (top line index of the visible frame).
inline int compute_max_scroll(int content_height, int viewport_height) {
    return std::max(0, content_height - viewport_height);
}

// Calculate the focus_y coordinate to pass to FTXUI focusPosition(0, focus_y)
// so that line `scroll_offset` is placed exactly at the top of the viewport
// (offsetting FTXUI Frame's centering bias: dy = focus_y - external_dimy / 2).
inline int compute_focus_y(int scroll_offset, int viewport_height) {
    const int vp = std::max(1, viewport_height);
    return std::max(0, scroll_offset) + (vp - 1) / 2;
}


// Rows [begin, end) of a vertically stacked list that overlap the window
// [top - overscan, top + viewport + overscan), plus the summed heights of the
// rows above (`top_pad`) and below (`bottom_pad`) that window. Spacers of
// those heights keep the list's total height, and so the scroll position and
// scrollbar, unchanged while only the visible rows are laid out.
struct VisibleSlice {
    size_t begin = 0;
    size_t end = 0;
    int top_pad = 0;
    int bottom_pad = 0;
};

inline VisibleSlice visible_rows(const std::vector<int>& heights, int top,
                                 int viewport, int overscan) {
    VisibleSlice slice;
    const int lo = std::max(0, top - std::max(0, overscan));
    const int hi = std::max(0, top) + std::max(1, viewport) + std::max(0, overscan);
    int y = 0;
    size_t i = 0;
    // Rows that end at or above `lo` are skipped.
    while (i < heights.size() && y + heights[i] <= lo) {
        y += heights[i];
        ++i;
    }
    slice.begin = i;
    slice.top_pad = y;
    while (i < heights.size() && y < hi) {
        y += heights[i];
        ++i;
    }
    slice.end = i;
    for (; i < heights.size(); ++i) slice.bottom_pad += heights[i];
    return slice;
}

} // namespace tui
} // namespace qcode
