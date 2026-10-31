#include <prompt_wrap.h>

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <climits>
#include <limits>

namespace qcode {
namespace tui {

namespace {

// Wraps one logical line (no '\n') starting at byte `base`.
void wrap_line(const std::string& line, int base, int width,
               std::vector<PromptRow>& out) {
  const std::vector<std::string> glyphs = ftxui::Utf8ToGlyphs(line);
  if (glyphs.empty()) {
    out.push_back({base, base});
    return;
  }

  // Prefix byte offsets: byte_of[i] = byte offset of glyph i within `line`,
  // byte_of[glyphs.size()] = line.size().
  std::vector<int> byte_of(glyphs.size() + 1, 0);
  for (size_t i = 0; i < glyphs.size(); ++i) {
    byte_of[i + 1] = byte_of[i] + static_cast<int>(glyphs[i].size());
  }
  auto emit = [&](size_t from, size_t to) {
    out.push_back({base + byte_of[from], base + byte_of[to]});
  };

  size_t row_start = 0;
  while (row_start < glyphs.size()) {
    // Greedily extend the current row while glyphs fit.
    size_t g = row_start;
    int w = 0;
    size_t last_space = std::numeric_limits<size_t>::max();
    while (g < glyphs.size()) {
      const int gw = ftxui::string_width(glyphs[g]);
      if (w + gw > width) break;
      w += gw;
      if (glyphs[g] == " ") last_space = g;
      ++g;
    }

    if (g == glyphs.size()) {
      emit(row_start, g);  // final row
      return;
    }
    // Glyph `g` does not fit: choose the break point.
    if (g == row_start) {
      // Single glyph wider than `width` (only possible for width < 2 with
      // double-width glyphs): force-place it so we always make progress.
      emit(row_start, g + 1);
      row_start = g + 1;
      continue;
    }
    if (glyphs[g] == " ") {
      // The overflowing glyph is whitespace: end the row before it and
      // consume it (invisible break space).
      emit(row_start, g);
      row_start = g + 1;
      continue;
    }
    if (last_space != std::numeric_limits<size_t>::max() &&
        last_space > row_start) {
      // Word break: end at the last space, consume it, rescan the rest.
      emit(row_start, last_space);
      row_start = last_space + 1;
      continue;
    }
    // No space inside the row: hard-break at the glyph boundary.
    emit(row_start, g);
    row_start = g;
  }
}

}  // namespace

std::vector<PromptRow> wrap_prompt_rows(const std::string& content, int width) {
  if (width < 1) width = 1;
  std::vector<PromptRow> rows;
  int line_start = 0;
  for (int i = 0; i <= static_cast<int>(content.size()); ++i) {
    if (i == static_cast<int>(content.size()) || content[i] == '\n') {
      wrap_line(content.substr(line_start, i - line_start), line_start, width,
                rows);
      line_start = i + 1;
    }
  }
  return rows;
}

int wrapped_line_count(const std::string& content, int width, int max_lines) {
  if (max_lines < 1) return 1;
  const auto rows = wrap_prompt_rows(content, width);
  return std::min(static_cast<int>(rows.size()), max_lines);
}

int cursor_row_index(const std::vector<PromptRow>& rows, int cursor) {
  if (rows.empty()) return 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (cursor <= rows[i].end) return static_cast<int>(i);
  }
  return static_cast<int>(rows.size()) - 1;
}

int visual_col_at(const std::string& content, const PromptRow& row, int cursor) {
  const int c = std::clamp(cursor, row.start, row.end);
  return ftxui::string_width(content.substr(row.start, c - row.start));
}

int byte_at_visual_col(const std::string& content, const PromptRow& row,
                       int col) {
  if (col <= 0) return row.start;
  const std::string slice = content.substr(row.start, row.end - row.start);
  const auto glyphs = ftxui::Utf8ToGlyphs(slice);
  int w = 0;
  int byte = row.start;
  for (const auto& g : glyphs) {
    const int gw = ftxui::string_width(g);
    if (w + gw > col) break;
    w += gw;
    byte += static_cast<int>(g.size());
  }
  return byte;
}

int move_visual_row(const std::string& content, int width, int cursor,
                    int delta) {
  const auto rows = wrap_prompt_rows(content, width);
  if (rows.empty()) return -1;
  const int i = cursor_row_index(rows, cursor);
  const int j = i + delta;
  if (j < 0 || j >= static_cast<int>(rows.size())) return -1;
  const int col = visual_col_at(content, rows[i], cursor);
  return byte_at_visual_col(content, rows[j], col);
}

ftxui::Element render_wrapped_input(const std::string& content, int cursor,
                                    int width, bool focused, bool hovered,
                                    ftxui::Box* content_box,
                                    ftxui::Box* viewport_box) {
  if (width < 1) width = 1;
  std::vector<PromptRow> rows = wrap_prompt_rows(content, width);
  if (rows.empty()) rows.push_back({0, 0});
  const int ri = cursor_row_index(rows, cursor);
  const ftxui::Decorator cursor_deco =
      (focused || hovered) ? ftxui::focusCursorBarBlinking : ftxui::select;

  ftxui::Elements els;
  els.reserve(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    const std::string row =
        content.substr(rows[i].start, rows[i].end - rows[i].start);
    if (static_cast<int>(i) != ri) {
      els.push_back(ftxui::text(row));
      continue;
    }

    // Byte offset of the cursor within this row, snapped to a glyph
    // boundary (the cursor may sit on a consumed break space == row end).
    const int off = std::clamp(cursor - rows[i].start, 0,
                               static_cast<int>(row.size()));
    int snapped = 0;
    for (const auto& gl : ftxui::Utf8ToGlyphs(row)) {
      const int prev = snapped;
      snapped += static_cast<int>(gl.size());
      if (snapped > off) {
        snapped = prev;
        break;
      }
    }

    if (snapped >= static_cast<int>(row.size())) {
      // Cursor at (or past) the end of the row: bar after the text.
      els.push_back(ftxui::hbox({ftxui::text(row),
                                 ftxui::text(" ") | cursor_deco}) |
                    ftxui::xflex);
      continue;
    }

    // Cursor on a glyph: split row into before / at / after.
    int glyph_end = static_cast<int>(row.size());
    int acc = 0;
    for (const auto& gl : ftxui::Utf8ToGlyphs(row)) {
      acc += static_cast<int>(gl.size());
      if (acc > snapped) {
        glyph_end = acc;
        break;
      }
    }
    els.push_back(ftxui::hbox({
                       ftxui::text(row.substr(0, snapped)),
                       ftxui::text(row.substr(snapped, glyph_end - snapped)) |
                           cursor_deco,
                       ftxui::text(row.substr(glyph_end)),
                   }) |
                  ftxui::xflex);
  }

  ftxui::Element el = ftxui::vbox(std::move(els));
  if (content_box) el = std::move(el) | ftxui::reflect(*content_box);
  el = std::move(el) | ftxui::frame;
  if (viewport_box) el = std::move(el) | ftxui::reflect(*viewport_box);
  return el;
}

}  // namespace tui
}  // namespace qcode
