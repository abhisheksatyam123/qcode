// GitHub-flavored Markdown rendering for qcode-tui, backed by md4c.
#include <qcode/render/markdown.h>
#include <qcode/render/themes.h>

#include <qcode/logger/logger.h>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/terminal.hpp>

#include "md4c.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace ftxui;

namespace qcode {

// ── Inline styling state ────────────────────────────────────────────────────
struct MStyle {
  bool bold = false;
  bool code = false;
  bool del = false;
  bool em = false;
  bool underline = false;
  bool mark = false;
  bool link = false;
  bool dim = false;
  std::string href;
};

enum class IKind { Text, Soft, Hard };
struct IItem {
  IKind kind = IKind::Text;
  std::string text;
  MStyle style;
};

struct BlockCtx {
  MD_BLOCKTYPE type = MD_BLOCK_DOC;
  Elements children;
  std::vector<IItem> inline_items;
  std::string code_text;
  // List item prefix / indentation
  std::string bullet;
  int hang = 0;
  // Ordered list counter
  int ol_index = 0;
  // Admonition type label
  std::string adm_type;
};

struct Ctx {
  std::vector<BlockCtx> stack;
  std::vector<MStyle> style_stack;
  Elements result;
  // Table capture (kept out of the block stack to avoid dangling refs)
  bool in_table = false;
  bool in_cell = false;
  int table_row = -1;
  std::string cell_buf;
  std::vector<std::vector<std::string>> table_grid;
  std::vector<std::vector<MD_ALIGN>> table_align;
  std::string theme = "orange";
};

// ── Helpers ──────────────────────────────────────────────────────────────────
static std::string attr_str(const MD_ATTRIBUTE& a) {
  return std::string(a.text, a.size);
}

static std::string decode_entity(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '&') {
      size_t j = s.find(';', i);
      if (j != std::string::npos) {
        std::string e = s.substr(i, j - i + 1);
        if (e == "&amp;") out += '&';
        else if (e == "&lt;") out += '<';
        else if (e == "&gt;") out += '>';
        else if (e == "&quot;") out += '"';
        else if (e == "&#39;" || e == "&#x27;") out += '\'';
        else if (e == "&nbsp;") out += ' ';
        else out += e;
        i = j + 1;
        continue;
      }
    }
    out += s[i++];
  }
  return out;
}

static int avail_width() {
  int w = 100;
  try {
    w = ftxui::Terminal::Size().dimx;
  } catch (...) {
  }
  if (w <= 0) w = 100;
  // Account for: 2 chars message indent ("  ") + 2 chars scrollbar + 2 chars right safety margin
  return std::max(20, w - 6);
}

// Flush collected inline items into a word-wrapped block. `first_prefix` is
// rendered on the first line; continuation lines are indented by `hang`.
static Element flush_inline(const std::vector<IItem>& items, int avail,
                            const std::string& first_prefix, int hang,
                            const std::string& theme) {
  struct Tok {
    std::string w;
    MStyle s;
    bool hard = false;
  };
  std::vector<Tok> toks;
  std::string cw;
  MStyle cs;

  auto push_cur = [&]() {
    if (!cw.empty()) {
      toks.push_back({cw, cs});
      cw.clear();
    }
  };

  for (const auto& it : items) {
    if (it.kind == IKind::Hard) {
      push_cur();
      toks.push_back({"", it.style, true});
    } else if (it.kind == IKind::Soft) {
      push_cur();
    } else {
      const std::string& t = it.text;
      size_t i = 0;
      while (i < t.size()) {
        while (i < t.size() && t[i] == ' ') i++;
        size_t j = t.find(' ', i);
        if (j == std::string::npos) j = t.size();
        if (j > i) toks.push_back({t.substr(i, j - i), it.style});
        i = j;
      }
    }
  }
  push_cur();

  std::vector<std::vector<Tok>> lines;
  std::vector<Tok> cl;
  int cwdt = 0;
  auto newline = [&]() {
    lines.push_back(cl);
    cl.clear();
    cwdt = 0;
  };
  int prefix_cols = (int)first_prefix.size();
  for (const auto& tk : toks) {
    if (tk.hard) {
      if (!tk.w.empty()) cl.push_back(tk);
      newline();
      continue;
    }
    int w = (int)tk.w.size();
    int allowance =
        lines.empty() ? std::max(1, avail - prefix_cols) : std::max(1, avail - hang);
    if (!cl.empty() && cwdt + 1 + w > allowance) newline();

    // If word exceeds allowance, split chunk across lines to prevent right-edge clipping
    if (w > allowance && allowance > 0) {
      int start = 0;
      while (start < w) {
        int cur_allowance = lines.empty() ? std::max(1, avail - prefix_cols) : std::max(1, avail - hang);
        int take = std::min(w - start, cur_allowance);
        Tok sub_tk = tk;
        sub_tk.w = tk.w.substr(start, take);
        cl.push_back(sub_tk);
        cwdt += take;
        start += take;
        if (start < w) {
          newline();
        }
      }
    } else {
      if (!cl.empty()) cwdt += 1;
      cl.push_back(tk);
      cwdt += w;
    }
  }
  if (!cl.empty()) lines.push_back(cl);

  Elements out;
  for (size_t li = 0; li < lines.size(); li++) {
    Elements row;
    std::string pfx = (li == 0) ? first_prefix : std::string(hang, ' ');
    if (!pfx.empty()) row.push_back(text(pfx));
    for (size_t k = 0; k < lines[li].size(); k++) {
      const auto& tk = lines[li][k];
      Element e = text(tk.w);
      if (tk.s.bold) e = e | bold;
      if (tk.s.code)
        e = e | bgcolor(theme_focus_bg(theme)) |
            color(accent2(theme));
      if (tk.s.del) e = e | dim;
      if (tk.s.em) e = e | dim;  // italic not available in ftxui
      if (tk.s.dim) e = e | dim;
      if (tk.s.underline) e = e | underlined;
      if (tk.s.mark) e = e | inverted;
      if (tk.s.link) e = e | color(accent2(theme)) | underlined;
      row.push_back(e);
      if (k + 1 < lines[li].size()) row.push_back(text(" "));
    }
    out.push_back(hbox(std::move(row)));
  }
  if (out.empty()) out.push_back(text(""));
  return vbox(std::move(out));
}

// ── md4c callbacks (C linkage) ───────────────────────────────────────────────
extern "C" {

static int md_enter_block(MD_BLOCKTYPE type, void* detail, void* ud) {
  Ctx* c = static_cast<Ctx*>(ud);
  BlockCtx b;
  b.type = type;
  if (type == MD_BLOCK_OL) {
    auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
    b.ol_index = static_cast<int>(d->start);
  } else if (type == MD_BLOCK_LI) {
    auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
    MD_BLOCKTYPE parent = c->stack.empty() ? MD_BLOCK_DOC : c->stack.back().type;
    if (parent == MD_BLOCK_OL) {
      int idx = c->stack.back().ol_index;
      b.bullet = std::to_string(idx) + ". ";
      c->stack.back().ol_index = idx + 1;
    } else if (parent == MD_BLOCK_UL) {
      b.bullet = "- ";
    }
    if (d->is_task) {
      char m = d->task_mark;
      b.bullet = (m == ' ' || m == 0) ? "[ ] " : "[x] ";
    }
    b.hang = static_cast<int>(b.bullet.size());
  } else if (type == MD_BLOCK_ADMONITION) {
    auto* d = static_cast<MD_BLOCK_ADMONITION_DETAIL*>(detail);
    b.adm_type = attr_str(d->type);
  } else if (type == MD_BLOCK_TABLE) {
    c->in_table = true;
    c->table_grid.clear();
    c->table_align.clear();
    c->table_row = -1;
  } else if (type == MD_BLOCK_TR) {
    if (c->in_table) {
      c->table_grid.push_back({});
      c->table_align.push_back({});
      c->table_row = static_cast<int>(c->table_grid.size()) - 1;
    }
  } else if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
    if (c->in_table) {
      c->in_cell = true;
      c->cell_buf.clear();
    }
  }
  c->stack.push_back(b);
  return 0;
}

static std::vector<std::string> wrap_text(const std::string& str, int width) {
  std::vector<std::string> lines;
  if (width <= 0) {
    lines.push_back(str);
    return lines;
  }
  
  std::vector<std::string> paragraphs;
  std::string current;
  for (char c : str) {
    if (c == '\n') {
      paragraphs.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  paragraphs.push_back(current);
  
  for (const auto& p : paragraphs) {
    std::vector<std::string> words;
    std::string word;
    for (char c : p) {
      if (c == ' ') {
        if (!word.empty()) {
          words.push_back(word);
          word.clear();
        }
      } else {
        word.push_back(c);
      }
    }
    if (!word.empty()) {
      words.push_back(word);
    }
    
    if (words.empty()) {
      lines.push_back("");
      continue;
    }
    
    std::string line;
    for (const auto& w : words) {
      if (line.empty()) {
        if (static_cast<int>(w.size()) > width) {
          int start = 0;
          while (start + width < static_cast<int>(w.size())) {
            lines.push_back(w.substr(start, width));
            start += width;
          }
          line = w.substr(start);
        } else {
          line = w;
        }
      } else {
        if (static_cast<int>(line.size() + 1 + w.size()) <= width) {
          line += " " + w;
        } else {
          lines.push_back(line);
          if (static_cast<int>(w.size()) > width) {
            int start = 0;
            while (start + width < static_cast<int>(w.size())) {
              lines.push_back(w.substr(start, width));
              start += width;
            }
            line = w.substr(start);
          } else {
            line = w;
          }
        }
      }
    }
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}


static int md_leave_block(MD_BLOCKTYPE type, void* detail, void* ud) {
  Ctx* c = static_cast<Ctx*>(ud);
  BlockCtx b = c->stack.back();
  c->stack.pop_back();

  // Table-internal blocks: TH/TD contribute their captured text to the grid;
  // THEAD/TBODY/TR are structural only.
  if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
    if (c->in_table && c->table_row >= 0 &&
        c->table_row < static_cast<int>(c->table_grid.size())) {
      MD_ALIGN align = MD_ALIGN_DEFAULT;
      if (detail) align = static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align;
      c->table_grid[c->table_row].push_back(c->cell_buf);
      c->table_align[c->table_row].push_back(align);
    }
    c->in_cell = false;
    return 0;
  }
  if (type == MD_BLOCK_THEAD || type == MD_BLOCK_TBODY ||
      type == MD_BLOCK_TR) {
    return 0;
  }

  Element el;
  int avail = avail_width();

  switch (type) {
    case MD_BLOCK_DOC: {
      Elements v;
      for (size_t i = 0; i < b.children.size(); i++) {
        v.push_back(b.children[i]);
        if (i + 1 < b.children.size()) v.push_back(text(""));
      }
      el = vbox(std::move(v));
      break;
    }
    case MD_BLOCK_P:
      el = flush_inline(b.inline_items, avail, "", 0, c->theme);
      break;
    case MD_BLOCK_H: {
      auto* d = static_cast<MD_BLOCK_H_DETAIL*>(detail);
      el = flush_inline(b.inline_items, avail, "", 0, c->theme) | bold;
      el = el | (d->level <= 2 ? color(accent2(c->theme)) : color(accent(c->theme)));
      if (d->level <= 2)
        el = vbox({el, separatorLight() | color(accent(c->theme))});
      break;
    }
    case MD_BLOCK_CODE: {
      Elements lines;
      std::istringstream iss(b.code_text);
      std::string ln;
      int max_code_w = std::max(20, avail - 4);
      while (std::getline(iss, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        if (static_cast<int>(ln.size()) <= max_code_w) {
          lines.push_back(text("  " + ln) | bgcolor(Color::RGB(0x1E, 0x1E, 0x2E)) |
                          color(Color::RGB(0xBB, 0xBB, 0xBB)));
        } else {
          int start = 0;
          bool first_chunk = true;
          while (start < static_cast<int>(ln.size())) {
            int take = std::min(static_cast<int>(ln.size()) - start, max_code_w);
            std::string chunk = ln.substr(start, take);
            std::string pfx = first_chunk ? "  " : "    ↳ ";
            lines.push_back(text(pfx + chunk) | bgcolor(Color::RGB(0x1E, 0x1E, 0x2E)) |
                            color(Color::RGB(0xBB, 0xBB, 0xBB)));
            first_chunk = false;
            start += take;
          }
        }
      }
      el = vbox(std::move(lines));
      break;
    }
    case MD_BLOCK_QUOTE: {
      Element inner = b.children.empty() ? vbox({}) : vbox(std::move(b.children));
      el = hbox({text(" │ ") | color(accent2(c->theme)) | dim, inner | dim});
      break;
    }
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
      el = vbox(std::move(b.children));
      break;
    case MD_BLOCK_LI: {
      Element para = flush_inline(b.inline_items, avail, b.bullet, b.hang, c->theme);
      Elements li_children;
      li_children.push_back(para);
      for (auto& ch : b.children)
        li_children.push_back(hbox({text(std::string(b.hang, ' ')), ch}));
      el = vbox(std::move(li_children));
      break;
    }
    case MD_BLOCK_HR:
      el = separator();
      break;
    case MD_BLOCK_ADMONITION: {
      Element inner = b.children.empty() ? vbox({}) : vbox(std::move(b.children));
      std::string label = b.adm_type.empty() ? "NOTE" : b.adm_type;
      el = vbox({text("▌ " + label) | bold | color(accent2(c->theme)),
                 hbox({text(" │ ") | color(accent(c->theme)) | dim, inner | dim})});
      break;
    }
    case MD_BLOCK_TABLE: {
      if (c->table_grid.empty()) {
        el = emptyElement();
        c->in_table = false;
        c->table_row = -1;
        break;
      }
      size_t C = 0;
      for (const auto& row : c->table_grid) {
        C = std::max(C, row.size());
      }
      if (C == 0) {
        el = emptyElement();
        c->in_table = false;
        c->table_row = -1;
        break;
      }
      std::vector<int> col_widths(C, 0);
      int total_max = 0;
      std::vector<int> max_col_width(C, 0);
      for (size_t col = 0; col < C; ++col) {
        for (const auto& row : c->table_grid) {
          if (col < row.size()) {
            max_col_width[col] = std::max(max_col_width[col], static_cast<int>(row[col].size()));
          }
        }
        total_max += max_col_width[col];
      }
      int avail_content = avail - static_cast<int>(C * 2 + C + 1);
      if (avail_content < static_cast<int>(C * 4)) {
        avail_content = static_cast<int>(C * 4);
      }
      if (total_max <= avail_content) {
        for (size_t col = 0; col < C; ++col) {
          col_widths[col] = max_col_width[col];
        }
      } else {
        int remaining_width = avail_content;
        int remaining_cols = static_cast<int>(C);
        std::vector<bool> allocated(C, false);
        int min_w = 5;
        if (avail_content < static_cast<int>(C) * min_w) {
          min_w = std::max(1, avail_content / static_cast<int>(C));
        }
        for (size_t col = 0; col < C; ++col) {
          if (max_col_width[col] <= min_w) {
            col_widths[col] = std::max(1, max_col_width[col]);
            remaining_width -= col_widths[col];
            remaining_cols--;
            allocated[col] = true;
          }
        }
        int sum_remaining_max = 0;
        for (size_t col = 0; col < C; ++col) {
          if (!allocated[col]) {
            sum_remaining_max += max_col_width[col];
          }
        }
        for (size_t col = 0; col < C; ++col) {
          if (!allocated[col]) {
            if (remaining_cols == 1) {
              col_widths[col] = std::max(min_w, remaining_width);
            } else {
              int w = (max_col_width[col] * remaining_width) / sum_remaining_max;
              w = std::max(min_w, w);
              col_widths[col] = w;
              remaining_width -= w;
              remaining_cols--;
              if (remaining_cols <= 0) remaining_cols = 1;
            }
          }
        }
      }
      std::vector<std::vector<Element>> cells;
      for (size_t row_idx = 0; row_idx < c->table_grid.size(); ++row_idx) {
        const auto& row = c->table_grid[row_idx];
        std::vector<Element> r;
        for (size_t col = 0; col < C; ++col) {
          std::string cell_text = "";
          if (col < row.size()) {
            cell_text = row[col];
          }
          int w = std::max(1, col_widths[col]);
          std::vector<std::string> lines = wrap_text(cell_text, w);
          MD_ALIGN align = MD_ALIGN_DEFAULT;
          if (row_idx < c->table_align.size() && col < c->table_align[row_idx].size()) {
            align = c->table_align[row_idx][col];
          }
          Elements line_elements;
          for (const auto& line : lines) {
            std::string padded = line;
            int pad_size = w - static_cast<int>(line.size());
            if (pad_size > 0) {
              if (align == MD_ALIGN_RIGHT) {
                padded = std::string(pad_size, ' ') + line;
              } else if (align == MD_ALIGN_CENTER) {
                int left_pad = pad_size / 2;
                int right_pad = pad_size - left_pad;
                padded = std::string(left_pad, ' ') + line + std::string(right_pad, ' ');
              } else {
                padded = line + std::string(pad_size, ' ');
              }
            }
            line_elements.push_back(text(padded));
          }
          r.push_back(vbox(std::move(line_elements)));
        }
        cells.push_back(std::move(r));
      }
      Table t(cells);
      t.SelectAll().Border(LIGHT);
      t.SelectAll().Separator(LIGHT);
      if (!c->table_grid.empty() && !c->table_grid[0].empty()) {
        t.SelectRow(0).Decorate(bold);
      }
      el = std::move(t).Render();
      c->in_table = false;
      c->table_row = -1;
      break;
    }

    default:
      el = b.children.empty() ? vbox({}) : vbox(std::move(b.children));
      break;
  }

  if (c->stack.empty())
    c->result.push_back(el);
  else
    c->stack.back().children.push_back(el);
  return 0;
}

static int md_enter_span(MD_SPANTYPE type, void* detail, void* ud) {
  Ctx* c = static_cast<Ctx*>(ud);
  MStyle s = c->style_stack.empty() ? MStyle{} : c->style_stack.back();
  switch (type) {
    case MD_SPAN_STRONG: s.bold = true; break;
    case MD_SPAN_EM: s.em = true; break;
    case MD_SPAN_CODE: s.code = true; break;
    case MD_SPAN_DEL: s.del = true; break;
    case MD_SPAN_U: s.underline = true; break;
    case MD_SPAN_MARK: s.mark = true; break;
    case MD_SPAN_A: {
      auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
      s.link = true;
      s.href = attr_str(d->href);
      break;
    }
    case MD_SPAN_IMG: {
      auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      std::string src = attr_str(d->src);
      IItem it;
      it.kind = IKind::Text;
      it.text = " [image: " + src + "] ";
      it.style = s;
      if (!c->stack.empty()) c->stack.back().inline_items.push_back(it);
      return 0;  // image placeholder only; don't persist style
    }
    default: break;
  }
  c->style_stack.push_back(s);
  return 0;
}

static int md_leave_span(MD_SPANTYPE type, void*, void* ud) {
  Ctx* c = static_cast<Ctx*>(ud);
  if (type == MD_SPAN_IMG) return 0;
  if (type == MD_SPAN_A && !c->style_stack.empty()) {
    std::string href = c->style_stack.back().href;
    if (!href.empty() && !c->stack.empty()) {
      IItem it;
      it.kind = IKind::Text;
      it.text = " (" + href + ")";
      it.style = MStyle{};
      it.style.dim = true;
      c->stack.back().inline_items.push_back(it);
    }
  }
  if (!c->style_stack.empty()) c->style_stack.pop_back();
  return 0;
}

static int md_text(MD_TEXTTYPE type, const MD_CHAR* txt, MD_SIZE size, void* ud) {
  Ctx* c = static_cast<Ctx*>(ud);
  if (c->stack.empty()) return 0;
  BlockCtx& cur = c->stack.back();
  std::string s(txt, size);

  if (cur.type == MD_BLOCK_CODE) {
    cur.code_text += s;
    return 0;
  }
  if (c->in_cell) {
    if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
      c->cell_buf += "\n";
    } else if (type == MD_TEXT_HTML) {
      std::string clean = s;
      clean.erase(std::remove_if(clean.begin(), clean.end(), [](unsigned char x) { return std::isspace(x); }), clean.end());
      for (auto& ch : clean) ch = std::tolower(ch);
      if (clean == "<br>" || clean == "<br/>") {
        c->cell_buf += "\n";
      }
    } else {
      c->cell_buf += decode_entity(s);
    }
    return 0;
  }
  if (type == MD_TEXT_HTML) return 0;  // strip raw HTML

  IItem it;
  if (type == MD_TEXT_BR) {
    it.kind = IKind::Hard;
  } else if (type == MD_TEXT_SOFTBR) {
    it.kind = IKind::Soft;
  } else {
    it.kind = IKind::Text;
    it.text = (type == MD_TEXT_ENTITY) ? decode_entity(s) : s;
    it.style = c->style_stack.empty() ? MStyle{} : c->style_stack.back();
  }
  cur.inline_items.push_back(it);
  return 0;
}

}  // extern "C"

// ── Public entry point ─────────────────────────────────────────────────────
ftxui::Elements render_markdown(const std::string& input_text, const std::string& theme) {
  Elements out;
  if (input_text.empty()) return out;

  Ctx ctx;
  ctx.theme = theme;
  MD_PARSER parser;
  std::memset(&parser, 0, sizeof(parser));
  parser.abi_version = 0;
  parser.flags = MD_DIALECT_GITHUB;
  parser.enter_block = md_enter_block;
  parser.leave_block = md_leave_block;
  parser.enter_span = md_enter_span;
  parser.leave_span = md_leave_span;
  parser.text = md_text;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  int rc = md_parse(input_text.c_str(), static_cast<MD_SIZE>(input_text.size()),
                    &parser, &ctx);
  if (rc != 0) {
    LOG_WARN("markdown: md_parse failed rc={}; falling back to raw text", rc);
    out.push_back(paragraph(input_text));
    return out;
  }
  return ctx.result;
}

}  // namespace qcode
