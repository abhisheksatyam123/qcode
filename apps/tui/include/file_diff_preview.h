#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>

namespace qcode {
namespace tui {

// Quote a path as a single-quoted POSIX shell literal.
std::string shell_quote(const std::string& s);

// Cached git/file preview for the Files tab (bounded in memory).
const std::string& get_file_diff(const std::string& path, size_t files_revision);

ftxui::Element render_diff_content(const std::string& content);

}  // namespace tui
}  // namespace qcode
