// Chat input wrapping: FTXUI's text() draws one row per string and clips
// (it never wraps), so the input pre-wraps content into display rows.
// These tests pin the wrap geometry, cursor mapping, row navigation, and
// the rendered output.

#include <prompt_wrap.h>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace qcode {
namespace tui {
namespace {

std::vector<std::string> row_strings(const std::string& content,
                                     const std::vector<PromptRow>& rows) {
  std::vector<std::string> out;
  out.reserve(rows.size());
  for (const auto& r : rows) {
    out.push_back(content.substr(r.start, r.end - r.start));
  }
  return out;
}

// Renders the element into a plain (style-free) grid of screen rows.
std::vector<std::string> render_rows(const ftxui::Element& el, int w, int h) {
  auto screen = ftxui::Screen::Create(ftxui::Dimensions{w, h});
  ftxui::Render(screen, el);
  std::vector<std::string> out;
  for (int y = 0; y < h; ++y) {
    std::string line;
    for (int x = 0; x < w; ++x) {
      line += screen.PixelAt(x, y).character;
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) {
      line.pop_back();
    }
    out.push_back(line);
  }
  return out;
}

TEST(PromptWrapTest, WrapsWordsBeforeExceedingWidth) {
  // "hello world" is exactly 11 columns; "foo" moves to the next row and
  // the break space is consumed.
  const auto rows = wrap_prompt_rows("hello world foo", 11);
  ASSERT_EQ(rows.size(), 2u);
  const auto strs = row_strings("hello world foo", rows);
  EXPECT_EQ(strs[0], "hello world");
  EXPECT_EQ(strs[1], "foo");
  // Rows are contiguous minus the single consumed space byte.
  EXPECT_EQ(rows[1].start, rows[0].end + 1);
}

TEST(PromptWrapTest, HardBreaksWordsLongerThanWidth) {
  const std::string content = "abcdefghij";
  const auto rows = wrap_prompt_rows(content, 4);
  const auto strs = row_strings(content, rows);
  EXPECT_EQ(strs, (std::vector<std::string>{"abcd", "efgh", "ij"}));
}

TEST(PromptWrapTest, CountsDoubleWidthGlyphsAsTwoColumns) {
  const std::string content = "\u4e2d\u6587\u4e2d\u6587\u6587";  // 5 CJK chars
  // Width 5 => two CJK glyphs (4 cols) per row, never splitting a glyph.
  const auto rows = wrap_prompt_rows(content, 5);
  const auto strs = row_strings(content, rows);
  ASSERT_EQ(strs.size(), 3u);
  EXPECT_EQ(strs[0], "\u4e2d\u6587");
  EXPECT_EQ(strs[1], "\u4e2d\u6587");
  EXPECT_EQ(strs[2], "\u6587");
}

TEST(PromptWrapTest, NewlinesStartNewLogicalLines) {
  const std::string content = "ab\n\ncd";
  const auto rows = wrap_prompt_rows(content, 20);
  const auto strs = row_strings(content, rows);
  ASSERT_EQ(strs.size(), 3u);
  EXPECT_EQ(strs[0], "ab");
  EXPECT_EQ(strs[1], "");  // empty logical line stays a visible row
  EXPECT_EQ(strs[2], "cd");
  // The '\n' bytes belong to no row.
  EXPECT_EQ(rows[0].end, 2);
  EXPECT_EQ(rows[1].start, 3);
  EXPECT_EQ(rows[1].end, 3);
  EXPECT_EQ(rows[2].start, 4);
}

TEST(PromptWrapTest, WrappedLineCountClampsToMaxLines) {
  EXPECT_EQ(wrapped_line_count("", 20, 6), 1);
  EXPECT_EQ(wrapped_line_count("hello", 20, 6), 1);
  // Word wrap can produce more rows than a ceil(w/avail) estimate.
  EXPECT_EQ(wrapped_line_count("aaa bbb ccc ddd eee fff ggg hhh", 4, 6), 6);  // clamped
  EXPECT_EQ(wrapped_line_count("aaa bbb ccc ddd eee fff ggg hhh", 4, 100), 8);
  EXPECT_EQ(wrapped_line_count("12345678", 4, 100), 2);
}

TEST(PromptWrapTest, CursorRowMappingPrefersEarlierRowAtBreak) {
  const std::string content = "hello world foo";  // rows: "hello world" | "foo"
  const auto rows = wrap_prompt_rows(content, 11);
  ASSERT_EQ(rows.size(), 2u);
  // Inside first row.
  EXPECT_EQ(cursor_row_index(rows, 0), 0);
  EXPECT_EQ(cursor_row_index(rows, 5), 0);
  // Cursor on the consumed break space (byte 11 == rows[0].end): first row.
  EXPECT_EQ(cursor_row_index(rows, rows[0].end), 0);
  // First byte of second row.
  EXPECT_EQ(cursor_row_index(rows, rows[1].start), 1);
  // Content end.
  EXPECT_EQ(cursor_row_index(rows, (int)content.size()), 1);
}

TEST(PromptWrapTest, VisualRowMovePreservesColumn) {
  const std::string content = "abcdefgh";  // rows: "abcd" | "efgh"
  const auto rows = wrap_prompt_rows(content, 4);
  ASSERT_EQ(rows.size(), 2u);
  // Column 1 ('b' at byte 1) => same column on row 2 ('f' at byte 5).
  EXPECT_EQ(move_visual_row(content, 4, 1, +1), 5);
  EXPECT_EQ(move_visual_row(content, 4, 5, -1), 1);
  // Out of range rows report -1 so callers fall back to FTXUI behavior.
  EXPECT_EQ(move_visual_row(content, 4, 1, -1), -1);
  EXPECT_EQ(move_visual_row(content, 4, 5, +1), -1);
  // Column past a short target row clamps to the row end ("cd" row is
  // full; short last row clamps).
  const std::string short_last = "abcdefg";  // rows "abcd" | "efg"
  EXPECT_EQ(move_visual_row(short_last, 4, 3, +1), (int)short_last.size());
}

TEST(PromptWrapTest, ClickColumnMapsIntoRowBytes) {
  const std::string content = "hello world foo";
  const auto rows = wrap_prompt_rows(content, 11);
  ASSERT_EQ(rows.size(), 2u);
  // Column 2 on row 1 => byte 12+2 = 14 ('o' of "foo" starts at 12+2? row1
  // starts at 12: f=12,o=13,o=14) => col 2 -> byte 14.
  EXPECT_EQ(byte_at_visual_col(content, rows[1], 0), rows[1].start);
  EXPECT_EQ(byte_at_visual_col(content, rows[1], 2), rows[1].start + 2);
  // Columns beyond the row clamp to its end.
  EXPECT_EQ(byte_at_visual_col(content, rows[1], 99), rows[1].end);
}

TEST(PromptWrapTest, RenderStacksLongLineVertically) {
  const std::string content = "hello world foo";
  auto el = render_wrapped_input(content, 0, 11, /*focused=*/true,
                                 /*hovered=*/false);
  const auto lines = render_rows(el, 16, 4);
  EXPECT_EQ(lines[0], "hello world");
  EXPECT_EQ(lines[1], "foo");
  EXPECT_EQ(lines[2], "");
}

TEST(PromptWrapTest, RenderKeepsCursorGlyphInItsRow) {
  const std::string content = "hello world foo";
  // Cursor on 'w' (byte 6, start of "world" in row 0).
  auto el = render_wrapped_input(content, 6, 11, true, false);
  const auto lines = render_rows(el, 16, 4);
  EXPECT_EQ(lines[0], "hello world");
  EXPECT_EQ(lines[1], "foo");
  // Cursor in row 1: content of both rows unchanged.
  auto el2 = render_wrapped_input(content, (int)content.size(), 11, true, false);
  const auto lines2 = render_rows(el2, 16, 4);
  EXPECT_EQ(lines2[0], "hello world");
  EXPECT_EQ(lines2[1], "foo");
}

TEST(PromptWrapTest, RenderMultilineContentKeepsEmptyRows) {
  auto el = render_wrapped_input("ab\n\ncd", 0, 20, false, false);
  const auto lines = render_rows(el, 22, 4);
  EXPECT_EQ(lines[0], "ab");
  EXPECT_EQ(lines[1], "");
  EXPECT_EQ(lines[2], "cd");
}

}  // namespace
}  // namespace tui
}  // namespace qcode
