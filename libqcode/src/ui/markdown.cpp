// GitHub-flavored Markdown rendering for qcode-tui, backed by md4c.
#include <qcode/ui/markdown.h>
#include <qcode/ui/themes.h>

#include <qcode/core/logger.h>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/terminal.hpp>

#include "md4c.h"

#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
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
  bool url = false;  // markup.link.url suffix: markdownLink (vs label cyan)
  bool img = false;  // markup.image: markdownImage
  bool img_text = false; // markup.image.text: markdownImageText
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
  std::string code_lang;
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
  std::string theme = "opencode";
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

static int avail_width_cached(int override_w = -1) {
  if (override_w > 0) return std::max(20, override_w - 6);
  int w = 100;
  try {
    w = ftxui::Terminal::Size().dimx;
  } catch (...) {
  }
  if (w <= 0) w = 100;
  // Account for: 2 chars message indent ("  ") + 2 chars scrollbar + 2 chars right safety margin
  return std::max(20, w - 6);
}
static int avail_width() { return avail_width_cached(-1); }

// LRU cache for parsed markdown — eliminates O(n²) re-parse of growing
// streaming text and repeated parse of completed messages. Keyed by
// (text hash + theme + width). Streaming deltas coalesce via bus, but each
// drain still grows the visible text; the cache turns the steady-state
// completed-message renders into O(1) hits.
namespace {
struct MdCacheKey {
  std::string text;
  std::string theme;
  int width = 0;
  bool operator==(const MdCacheKey& o) const {
    return width == o.width && theme == o.theme && text == o.text;
  }
};
struct MdCacheKeyHash {
  size_t operator()(const MdCacheKey& k) const noexcept {
    size_t h = std::hash<std::string>{}(k.text);
    h ^= std::hash<std::string>{}(k.theme) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.width) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};
constexpr size_t kMdCacheMax = 32;
std::mutex g_md_cache_mutex;
std::unordered_map<MdCacheKey, ftxui::Elements, MdCacheKeyHash> g_md_cache;
std::deque<MdCacheKey> g_md_cache_order;
}  // namespace

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
    // opencode markup.list: enumeration cyan (md_list_enum), item peach
    // (md_list_item); body text keeps its own inline colors.
    if (!pfx.empty()) {
      if (!first_prefix.empty() && li == 0) {
        bool is_enum = (!pfx.empty() && pfx[0] >= '0' && pfx[0] <= '9');
        auto col = is_enum ? theme_md_list_enum(theme) : theme_md_list_item(theme);
        row.push_back(text(pfx) | color(col));
      } else {
        row.push_back(text(pfx));
      }
    }
    for (size_t k = 0; k < lines[li].size(); k++) {
      const auto& tk = lines[li][k];
      Element e = text(tk.w);
      // opencode TUI scopes: markup.bold/strong → markdownStrong+bold,
      // markup.italic → markdownEmph, markup.raw.inline → markdownCode on
      // background, markup.link.label → markdownLinkText ul,
      // markup.link.url → markdownLink ul.
      if (tk.s.bold) e = e | bold | color(theme_md_strong(theme));
      if (tk.s.code)
        e = e | bgcolor(theme_bg(theme)) | color(theme_md_code(theme));
      if (tk.s.del) e = e | strikethrough | color(theme_muted(theme));
      if (tk.s.em) e = e | color(theme_md_emph(theme));
      if (tk.s.dim) e = e | dim;
      if (tk.s.underline) e = e | underlined;
      if (tk.s.mark) e = e | inverted;
      if (tk.s.link)
        e = e | color(theme_md_link_text(theme)) | underlined;
      if (tk.s.url) e = e | color(theme_md_link(theme)) | underlined;
      if (tk.s.img) e = e | color(theme_md_image(theme));
      if (tk.s.img_text) e = e | color(theme_md_image_text(theme));
      row.push_back(e);
      if (k + 1 < lines[li].size()) row.push_back(text(" "));
    }
    out.push_back(hbox(std::move(row)));
  }
  if (out.empty()) out.push_back(text(""));
  return vbox(std::move(out));
}


// ── Lightweight per-line syntax highlighter ─────────────────────────────────
// Mirrors opencode's TUI element roles (theme index.ts): code body text uses
// markdownCodeBlock, comments → syntaxComment, keywords → syntaxKeyword,
// function calls → syntaxFunction, strings → syntaxString, numbers →
// syntaxNumber, types → syntaxType, operators → syntaxOperator, punctuation →
// syntaxPunctuation, diff +/-/@@ lines → diffAdded/diffRemoved/diffHunkHeader.
// A hand tokenizer (not tree-sitter/shiki): terminal has no web worker, and
// ftxui renders plain Elements — this gives per-theme coloring at O(n) cost.
namespace {

enum class HlKind {
  Plain, Comment, Keyword, Str, Num, Func, Type, Op, Punct,
  DiffAdd, DiffDel, DiffHunk
};

struct HlTok { std::string w; HlKind k = HlKind::Plain; };

// C-like keyword set shared by c/cpp/js/ts/java/go/rust/sh-ish families.
inline bool hl_is_keyword(const std::string& w) {
  static const char* kws[] = {
    "if","else","elif","for","while","do","switch","case","default","break",
    "continue","return","yield","try","except","catch","finally","throw","raise",
    "import","from","export","as","with","using","namespace","package",
    "class","struct","enum","union","interface","trait","impl","extends",
    "implements","public","private","protected","static","final","const",
    "constexpr","virtual","override","abstract","sealed","readonly","volatile",
    "async","await","function","def","fn","lambda","let","var","new","delete",
    "typeof","sizeof","typedef","template","typename","self","this","super",
    "true","false","null","nullptr","nil","None","True","False","and","or",
    "not","in","is","of","pass","goto","do","end","begin","then","until",
    "local","echo","exit","fi","done"
  };
  for (auto* kw : kws) if (w == kw) return true;
  return false;
}

inline bool hl_is_type(const std::string& w) {
  if (w.empty() || !(w[0] >= 'A' && w[0] <= 'Z')) return false;
  for (char ch : w)
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') || ch == '_')) return false;
  return true;
}

inline bool hl_is_number(const std::string& w) {
  if (w.empty()) return false;
  size_t i = 0;
  if (w.size() > 2 && w[0] == '0' && (w[1] == 'x' || w[1] == 'X')) {
    if (w.size() == 2) return false;
    for (i = 2; i < w.size(); i++)
      if (!((w[i] >= '0' && w[i] <= '9') || (w[i] >= 'a' && w[i] <= 'f') ||
            (w[i] >= 'A' && w[i] <= 'F') || w[i] == '_')) return false;
    return true;
  }
  bool dot = false, digit = false;
  for (; i < w.size(); i++) {
    char ch = w[i];
    if (ch >= '0' && ch <= '9') { digit = true; continue; }
    if (ch == '.' && !dot) { dot = true; continue; }
    if (ch == '_' && digit) continue;
    return false;
  }
  return digit;
}

// Tokenize one code line into colored spans. `lang` selects comment/string
// conventions; unknown/empty lang still gets strings/numbers/operators.
inline std::vector<HlTok> hl_tokenize_line(const std::string& ln,
                                           const std::string& lang,
                                           bool& in_block, bool& in_pystr) {
  std::vector<HlTok> out;
  const size_t n = ln.size();
  size_t i = 0;
  auto is_word = [](char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
  };
  const bool hash_comment = (lang == "py" || lang == "python" || lang == "rb" ||
                             lang == "ruby" || lang == "sh" || lang == "bash" ||
                             lang == "zsh" || lang == "yaml" || lang == "yml" ||
                             lang == "toml" || lang == "dockerfile" || lang.empty());
  const bool dash_comment = (lang == "sql" || lang == "lua" || lang == "hs" ||
                             lang == "haskell");
  const bool slash_comment = !dash_comment;  // c-like default

  // diff热量: whole-line roles (opencode theme diff.* roles)
  if ((lang == "diff" || lang == "patch") && n > 0 &&
      (ln[0] == '+' || ln[0] == '-' || ln.compare(0, 2, "@@") == 0) && !in_block) {
    HlKind k = HlKind::Plain;
    if (ln[0] == '+') k = HlKind::DiffAdd;
    else if (ln[0] == '-') k = HlKind::DiffDel;
    else k = HlKind::DiffHunk;
    out.push_back({ln, k});
    return out;
  }
  if (in_pystr) {
    size_t e = ln.find("\"\"\"");
    if (e == std::string::npos) { out.push_back({ln, HlKind::Str}); return out; }
    out.push_back({ln.substr(0, e + 3), HlKind::Str});
    i = e + 3;
    in_pystr = false;
  }
  if (in_block) {
    size_t e = ln.find("*/");
    if (e == std::string::npos) { out.push_back({ln, HlKind::Comment}); return out; }
    out.push_back({ln.substr(0, e + 2), HlKind::Comment});
    i = e + 2;
    in_block = false;
  }

  std::string plain;
  auto flush_plain = [&]() {
    if (!plain.empty()) { out.push_back({plain, HlKind::Plain}); plain.clear(); }
  };
  while (i < n) {
    char ch = ln[i];
    // line comments
    if (slash_comment && ch == '/' && i + 1 < n && ln[i + 1] == '/') {
      flush_plain(); out.push_back({ln.substr(i), HlKind::Comment}); break;
    }
    if (hash_comment && ch == '#') {
      flush_plain(); out.push_back({ln.substr(i), HlKind::Comment}); break;
    }
    if (dash_comment && ch == '-' && i + 1 < n && ln[i + 1] == '-') {
      flush_plain(); out.push_back({ln.substr(i), HlKind::Comment}); break;
    }
    // block comments
    if (slash_comment && ch == '/' && i + 1 < n && ln[i + 1] == '*') {
      size_t e = ln.find("*/", i + 2);
      flush_plain();
      if (e == std::string::npos) { out.push_back({ln.substr(i), HlKind::Comment}); in_block = true; break; }
      out.push_back({ln.substr(i, e + 2 - i), HlKind::Comment}); i = e + 2; continue;
    }
    // python triple-quoted strings
    if ((lang == "py" || lang == "python") && ln.compare(i, 3, "\"\"\"") == 0) {
      size_t e = ln.find("\"\"\"", i + 3);
      flush_plain();
      if (e == std::string::npos) { out.push_back({ln.substr(i), HlKind::Str}); in_pystr = true; break; }
      out.push_back({ln.substr(i, e + 3 - i), HlKind::Str}); i = e + 3; continue;
    }
    // strings
    if (ch == '"' || ch == '\'' || ch == '`') {
      size_t j = i + 1;
      while (j < n) {
        if (ln[j] == '\\' && j + 1 < n) { j += 2; continue; }
        if (ln[j] == ch) break;
        j++;
      }
      flush_plain();
      out.push_back({ln.substr(i, (j < n ? j + 1 : n) - i), HlKind::Str});
      i = (j < n ? j + 1 : n);
      continue;
    }
    // numbers
    if ((ch >= '0' && ch <= '9') &&
        (i == 0 || (!is_word(ln[i - 1]) && ln[i - 1] != '.'))) {
      size_t j = i;
      bool dot = false;
      while (j < n && ((ln[j] >= '0' && ln[j] <= '9') || ln[j] == '_' ||
                       (!dot && ln[j] == '.'))) {
        if (ln[j] == '.') dot = true;
        j++;
      }
      if ((ch == '0' && j == i + 1 && j < n && (ln[j] == 'x' || ln[j] == 'X'))) {
        j++;
        while (j < n && ((ln[j] >= '0' && ln[j] <= '9') ||
                         (ln[j] >= 'a' && ln[j] <= 'f') ||
                         (ln[j] >= 'A' && ln[j] <= 'F') || ln[j] == '_')) j++;
      }
      flush_plain();
      out.push_back({ln.substr(i, j - i), HlKind::Num});
      i = j;
      continue;
    }
    // words: keyword / function-call / type / variable
    if (is_word(ch) && (i == 0 || !is_word(ln[i - 1]))) {
      size_t j = i;
      while (j < n && is_word(ln[j])) j++;
      std::string w = ln.substr(i, j - i);
      size_t k = j;
      while (k < n && (ln[k] == ' ' || ln[k] == '\t')) k++;
      flush_plain();
      if (hl_is_keyword(w)) out.push_back({w, HlKind::Keyword});
      else if (k < n && ln[k] == '(') out.push_back({w, HlKind::Func});
      else if (hl_is_type(w)) out.push_back({w, HlKind::Type});
      else out.push_back({w, HlKind::Plain});
      i = j;
      continue;
    }
    // operators
    if (std::string("=+-*/%<>!&|^~?:").find(ch) != std::string::npos) {
      flush_plain(); out.push_back({std::string(1, ch), HlKind::Op}); i++;
      continue;
    }
    // punctuation
    if (std::string("()[]{}.,;@").find(ch) != std::string::npos) {
      flush_plain(); out.push_back({std::string(1, ch), HlKind::Punct}); i++;
      continue;
    }
    plain += ch;
    i++;
  }
  flush_plain();
  // hl_is_number validation pass: reclassify plain-looking numeric runs that
  // the scanner above may have merged with units (e.g. "100ms" stays plain).
  return out;
}

// Normalize fence info to a highlight family.
inline std::string hl_lang_family(std::string lang) {
  for (auto& ch : lang) ch = static_cast<char>(std::tolower(ch));
  if (lang == "py" || lang == "python3") return "py";
  if (lang == "js" || lang == "jsx" || lang == "ts" || lang == "tsx" ||
      lang == "mjs" || lang == "cjs") return "js";
  if (lang == "sh" || lang == "shell" || lang == "bash" || lang == "zsh" ||
      lang == "console" || lang == "terminal") return "sh";
  if (lang == "yml" || lang == "toml" || lang == "ini" || lang == "cfg") return "yaml";
  if (lang == "cc" || lang == "hh" || lang == "cxx" || lang == "hpp" ||
      lang == "c++" || lang == "cpp" || lang == "h" || lang == "java" ||
      lang == "cs" || lang == "go" || lang == "rs" || lang == "swift" ||
      lang == "kt" || lang == "scala" || lang == "c") return "c";
  return lang;
}

// Render one highlighted code line as an hbox of colored spans on panel_bg.
inline Element hl_render_line(const std::string& pfx, const std::string& ln,
                              const std::string& lang, const std::string& theme,
                              bool& in_block, bool& in_pystr) {
  Elements row;
  if (!pfx.empty()) row.push_back(text(pfx));
  if (ln.empty()) { row.push_back(text(" ")); return hbox(std::move(row)) | bgcolor(theme_panel_bg(theme)); }
  auto toks = hl_tokenize_line(ln, lang, in_block, in_pystr);
  for (const auto& tk : toks) {
    Element e = text(tk.w);
    switch (tk.k) {
      case HlKind::Comment: e = e | color(theme_syn_comment(theme)); break;
      case HlKind::Keyword: e = e | color(theme_syn_keyword(theme)) | bold; break;
      case HlKind::Str: e = e | color(theme_syn_string(theme)); break;
      case HlKind::Num: e = e | color(theme_syn_number(theme)); break;
      case HlKind::Func: e = e | color(theme_syn_function(theme)); break;
      case HlKind::Type: e = e | color(theme_syn_type(theme)); break;
      case HlKind::Op: e = e | color(theme_syn_operator(theme)); break;
      case HlKind::Punct: e = e | color(theme_syn_punct(theme)) | dim; break;
      case HlKind::DiffAdd: e = e | color(theme_success(theme)); break;
      case HlKind::DiffDel: e = e | color(theme_error(theme)); break;
      case HlKind::DiffHunk: e = e | color(theme_md_quote(theme)) | bold; break;
      case HlKind::Plain:
      default: e = e | color(theme_md_codeblock(theme)); break;
    }
    row.push_back(e);
  }
  return hbox(std::move(row)) | bgcolor(theme_panel_bg(theme));
}

}  // namespace

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
  } else if (type == MD_BLOCK_CODE) {
    auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
    // md4c splits the info string: `lang` is the first word when the fence
    // carries one (```cpp), otherwise empty for bare/indented blocks.
    b.code_lang = attr_str(d->lang);
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
      // opencode TUI: markup.heading → markdownHeading+bold for all levels,
      // markup.heading.1 additionally underlined (h1 rule kept for h1 only).
      auto* d = static_cast<MD_BLOCK_H_DETAIL*>(detail);
      el = flush_inline(b.inline_items, avail, "", 0, c->theme) | bold |
           color(theme_md_heading(c->theme));
      if (d->level == 1) el = el | underlined;
      if (d->level <= 2)
        el = vbox({el, separatorLight() | color(theme_md_hr(c->theme))});
      break;
    }
    case MD_BLOCK_CODE: {
      // opencode web: code blocks get the shiki background; TUI equivalent
      // is panel_bg with per-theme syntax roles. Long lines fall back to
      // chunked plain rendering so nothing clips at the right edge.
      Elements lines;
      std::istringstream iss(b.code_text);
      std::string ln;
      int max_code_w = std::max(20, avail - 4);
      const std::string family = hl_lang_family(b.code_lang);
      bool in_block = false, in_pystr = false;
      constexpr size_t kHlMaxLine = 2000;
      while (std::getline(iss, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        if (ln.size() > kHlMaxLine || static_cast<int>(ln.size()) > max_code_w) {
          // Overlong/minified lines: keep old chunked plain behavior.
          if (static_cast<int>(ln.size()) <= max_code_w) {
            lines.push_back(text("  " + ln) | bgcolor(theme_panel_bg(c->theme)) |
                            color(theme_md_codeblock(c->theme)));
          } else {
            int start = 0;
            bool first_chunk = true;
            while (start < static_cast<int>(ln.size())) {
              int take = std::min(static_cast<int>(ln.size()) - start, max_code_w);
              std::string chunk = ln.substr(start, take);
              std::string pfx = first_chunk ? "  " : "    ↳ ";
              lines.push_back(text(pfx + chunk) | bgcolor(theme_panel_bg(c->theme)) |
                              color(theme_md_codeblock(c->theme)));
              first_chunk = false;
              start += take;
            }
          }
          continue;
        }
        lines.push_back(hl_render_line("  ", ln, family, c->theme, in_block, in_pystr));
      }
      if (!b.code_lang.empty()) {
        lines.insert(lines.begin(),
                     text("‹" + b.code_lang + "›") | dim |
                         color(theme_muted(c->theme)));
      }
      el = vbox(std::move(lines));
      break;
    }
    case MD_BLOCK_QUOTE: {
      // opencode web: muted text + base rail; TUI theme: markdownBlockQuote.
      Element inner = b.children.empty() ? vbox({}) : vbox(std::move(b.children));
      el = hbox({text(" │ ") | color(theme_md_quote(c->theme)), inner});
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
      el = separatorLight() | color(theme_md_hr(c->theme));
      break;
    case MD_BLOCK_ADMONITION: {
      Element inner = b.children.empty() ? vbox({}) : vbox(std::move(b.children));
      std::string label = b.adm_type.empty() ? "NOTE" : b.adm_type;
      el = vbox({text("▌ " + label) | bold | color(theme_md_heading(c->theme)),
                 hbox({text(" │ ") | color(theme_md_quote(c->theme)), inner})});
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
        t.SelectRow(0).DecorateCells(color(theme_md_heading(c->theme)));
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
      // opencode TUI: markup renders image alt in markdownImage and the src
      // in markdownImageText. Terminal has no image widget; emit a styled
      // two-token placeholder instead of inheriting ambient style.
      auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      std::string src = attr_str(d->src);
      if (!c->stack.empty()) {
        IItem alt;
        alt.kind = IKind::Text;
        alt.text = " [image: ";
        alt.style = MStyle{};
        alt.style.img = true;
        c->stack.back().inline_items.push_back(alt);
        IItem srct;
        srct.kind = IKind::Text;
        srct.text = src;
        srct.style = MStyle{};
        srct.style.img_text = true;
        c->stack.back().inline_items.push_back(srct);
        IItem close;
        close.kind = IKind::Text;
        close.text = "] ";
        close.style = MStyle{};
        close.style.img = true;
        c->stack.back().inline_items.push_back(close);
      }
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
    // opencode TUI scope markup.link.url → markdownLink (underlined); the
    // label itself already rendered via markdownLinkText in flush_inline.
    std::string href = c->style_stack.back().href;
    if (!href.empty() && !c->stack.empty()) {
      IItem it;
      it.kind = IKind::Text;
      it.text = " (" + href + ")";
      it.style = MStyle{};
      it.style.url = true;
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
  return render_markdown(input_text, theme, avail_width_cached(-1));
}

ftxui::Elements render_markdown(const std::string& input_text, const std::string& theme, int avail_w_override) {
  Elements out;
  if (input_text.empty()) return out;
  const int avail_for_key = avail_width_cached(avail_w_override);

  // Fast LRU lookup — completed messages and stable streaming prefixes hit here.
  {
    std::lock_guard<std::mutex> lock(g_md_cache_mutex);
    MdCacheKey key{input_text, theme, avail_for_key};
    auto it = g_md_cache.find(key);
    if (it != g_md_cache.end()) return it->second;
  }

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
  out = ctx.result;

  {
    std::lock_guard<std::mutex> lock(g_md_cache_mutex);
    MdCacheKey key{input_text, theme, avail_for_key};
    if (g_md_cache.size() >= kMdCacheMax) {
      auto oldest = g_md_cache_order.front();
      g_md_cache_order.pop_front();
      g_md_cache.erase(oldest);
    }
    g_md_cache.emplace(key, out);
    g_md_cache_order.push_back(std::move(key));
  }
  return out;
}

}  // namespace qcode
