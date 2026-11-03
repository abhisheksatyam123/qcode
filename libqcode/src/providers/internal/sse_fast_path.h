#pragma once

// Helpers for the per-token fast paths in the SSE parsers. A fast path decodes
// one specific, very common event shape without a full JSON DOM parse. These
// helpers only do the mechanical part: decoding a JSON string body and
// checking the bytes that follow it. Each caller decides which shapes qualify
// and must fall back to its full parser for anything else.

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

namespace qcode::sse_fast {

// Decodes a JSON string body that starts at data[i] (the first byte after the
// opening quote). On success, out holds the decoded text and i points at the
// closing quote. \u escapes and raw control characters return false.
inline bool decode_json_string(std::string_view data, size_t& i, std::string& out) {
  while (true) {
    if (i >= data.size()) return false;
    const char c = data[i];
    if (c == '"') return true;
    if (static_cast<unsigned char>(c) < 0x20) return false;
    if (c != '\\') {
      out += c;
      ++i;
      continue;
    }
    if (i + 1 >= data.size()) return false;
    switch (data[i + 1]) {
      case '"': out += '"'; break;
      case '\\': out += '\\'; break;
      case '/': out += '/'; break;
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      case 'r': out += '\r'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      default: return false;  // includes \u
    }
    i += 2;
  }
}

// True when data[from..] equals `expected` once spaces are ignored.
inline bool tail_equals(std::string_view data, size_t from, std::string_view expected) {
  size_t k = 0;
  for (size_t j = from; j < data.size(); ++j) {
    if (data[j] == ' ') continue;
    if (k >= expected.size() || data[j] != expected[k]) return false;
    ++k;
  }
  return k == expected.size();
}

// True when data contains any of the markers.
inline bool contains_any(std::string_view data, std::initializer_list<std::string_view> markers) {
  for (const auto marker : markers) {
    if (data.find(marker) != std::string_view::npos) return true;
  }
  return false;
}

}  // namespace qcode::sse_fast
