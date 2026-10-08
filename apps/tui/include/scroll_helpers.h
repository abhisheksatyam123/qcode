#pragma once

#include <algorithm>

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

} // namespace tui
} // namespace qcode
