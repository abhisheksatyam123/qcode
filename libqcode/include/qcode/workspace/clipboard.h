#pragma once

#include <string>

namespace qcode {
namespace tui {

/// Copy text to the system clipboard (xclip/xsel).
void copy_to_clipboard(const std::string& text);

}  // namespace tui
}  // namespace qcode
