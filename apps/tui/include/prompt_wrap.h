#pragma once

// Word-wrapping for the chat input box.
//
// FTXUI's `text()` element draws a string on ONE row and clips it (see
// ftxui/dom/text.cpp: y = box_.y_min, no line breaking), and FTXUI's Input
// component builds one element per logical '\n' line. Long typed lines
// therefore never stack vertically: they scroll horizontally inside the
// input's `frame`. These helpers pre-wrap the content into display rows so
// the input can render (and be navigated) vertically instead.

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <string>
#include <vector>

namespace qcode {
namespace tui {

// Byte range [start, end) of one display row of the wrapped content.
// Rows are ordered; at most one byte (the whitespace consumed at a word
// break) may sit between row[i].end and row[i+1].start.
struct PromptRow {
  int start = 0;
  int end = 0;
};

// Wraps `content` (logical lines split by '\n') into display rows of at
// most `width` terminal columns. UTF-8 and double-width (CJK) aware.
// Word-aware: breaks at the last space that fits; over-long words are
// hard-broken at glyph boundaries (never mid-character). Every byte of
// `content` belongs to a row except the whitespace consumed at breaks and
// the '\n' separators themselves. Empty logical lines produce an empty row.
std::vector<PromptRow> wrap_prompt_rows(const std::string& content, int width);

// Number of wrapped rows, clamped to [1, max_lines].
int wrapped_line_count(const std::string& content, int width, int max_lines);

// First row whose byte range contains `cursor` (rows with cursor <= end
// match first, so a cursor on a consumed break space stays on the earlier
// row). Falls back to the last row.
int cursor_row_index(const std::vector<PromptRow>& rows, int cursor);

// Display column of byte `cursor` within `row` (clamped to the row).
int visual_col_at(const std::string& content, const PromptRow& row, int cursor);

// Byte offset within `content` for display column `col` of `row`, clamped
// so a click past the row's end lands on the row's end.
int byte_at_visual_col(const std::string& content, const PromptRow& row, int col);

// Moves `cursor` by `delta` display rows (-1/+1), preserving the display
// column (clamped to the target row). Returns -1 when the target row does
// not exist (caller should fall back to FTXUI's logical-line movement).
int move_visual_row(const std::string& content, int width, int cursor, int delta);

// Renders `content` wrapped to `width` columns with the cursor cell at
// byte `cursor` styled like FTXUI's Input (blinking bar when focused,
// selection otherwise) and a `frame` that scrolls toward the cursor row.
// When boxes are provided they receive the absolute screen bounds of the
// content (`content_box`, tracks scroll offset) and of the viewport
// (`viewport_box`), enabling click-to-position. Returns `vbox | frame`.
ftxui::Element render_wrapped_input(const std::string& content, int cursor,
                                    int width, bool focused, bool hovered,
                                    ftxui::Box* content_box = nullptr,
                                    ftxui::Box* viewport_box = nullptr);

}  // namespace tui
}  // namespace qcode
