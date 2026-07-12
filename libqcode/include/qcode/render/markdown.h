#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace ai {
namespace tui {

// Render a GitHub-flavored Markdown string into ftxui Elements.
// Uses the vendored md4c parser (third_party/md4c) for fidelity close to
// opencode's marked + marked-shiki pipeline (CommonMark + GFM tables,
// strikethrough, task lists, autolinks).
ftxui::Elements render_markdown(const std::string& input_text, const std::string& theme = "orange");

}  // namespace tui
}  // namespace ai
