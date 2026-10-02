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

// Calculate the number of lines to scroll for PageUp/PageDown based on terminal height.
inline int compute_page_step(int terminal_height) {
    return std::max(5, terminal_height - 6);
}

// Clamp and update scroll offset for wheel events
inline int apply_wheel_scroll(int current_scroll, bool is_wheel_up, int lines_per_wheel = 3) {
    if (is_wheel_up) {
        return std::max(0, current_scroll - lines_per_wheel);
    } else {
        return current_scroll + lines_per_wheel;
    }
}

// Clamp and update scroll offset for page up/down
inline int apply_page_scroll(int current_scroll, bool is_page_up, int terminal_height) {
    const int page = compute_page_step(terminal_height);
    if (is_page_up) {
        return std::max(0, current_scroll - page);
    } else {
        return current_scroll + page;
    }
}

} // namespace tui
} // namespace qcode
