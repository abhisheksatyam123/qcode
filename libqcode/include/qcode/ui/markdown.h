#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace qcode {

// Render a GitHub-flavored Markdown string into ftxui Elements.
// Uses the vendored md4c parser (third_party/md4c) for fidelity close to
// opencode's marked + marked-shiki pipeline (CommonMark + GFM tables,
// strikethrough, task lists, autolinks).
// terminal_width: columns to wrap for (<= 0: query the terminal). Results
// are LRU-cached; pass cache_result=false for text that is still growing
// (the streaming message) so it doesn't evict completed messages.
ftxui::Elements render_markdown(const std::string& input_text, const std::string& theme = "opencode");
ftxui::Elements render_markdown(const std::string& input_text, const std::string& theme,
                                int terminal_width, bool cache_result = true);

}  // namespace qcode
