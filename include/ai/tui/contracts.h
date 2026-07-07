/* 
 * Contracts for data abstraction in UI components.
 */
#pragma once
#include <memory>
#include <algorithm>

namespace ai::tui {
    // Abstract interface for scrollable components
    struct Scrollable {
        std::shared_ptr<int> scroll_offset = std::make_shared<int>(0);
        virtual void scroll_up(int amount = 10) { *scroll_offset = std::max(0, *scroll_offset - amount); }
        virtual void scroll_down(int amount = 10) { *scroll_offset += amount; }
        virtual ~Scrollable() = default;
    };
}
