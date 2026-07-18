#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace qcode {
namespace utils {

// Convert `in` to valid UTF-8 by replacing any invalid byte sequence with the
// Unicode replacement character U+FFFD (EF BF BD). This guarantees the result
// can be safely embedded in and serialized from JSON -- nlohmann::json throws
// `type_error.316` ("invalid UTF-8 byte") when dumping a string that contains
// invalid UTF-8, which previously crashed request building for tool output
// that contained binary / non-UTF-8 bytes.
inline std::string sanitize_utf8(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  const unsigned char* s = reinterpret_cast<const unsigned char*>(in.data());
  const size_t n = in.size();
  size_t i = 0;

  const auto emit_replacement = [&]() {
    out.push_back(static_cast<char>(0xEF));
    out.push_back(static_cast<char>(0xBF));
    out.push_back(static_cast<char>(0xBD));
  };

  while (i < n) {
    unsigned char b = s[i];
    if (b < 0x80) {  // ASCII
      out.push_back(static_cast<char>(b));
      ++i;
      continue;
    }

    // Determine the expected sequence length from the lead byte.
    int len = 0;
    unsigned int cp = 0;
    if ((b & 0xE0) == 0xC0) {
      len = 2;
      cp = b & 0x1F;
    } else if ((b & 0xF0) == 0xE0) {
      len = 3;
      cp = b & 0x0F;
    } else if ((b & 0xF8) == 0xF0) {
      len = 4;
      cp = b & 0x07;
    } else {
      emit_replacement();  // invalid lead byte
      ++i;
      continue;
    }

    // Missing continuation bytes => replace and resync by one byte.
    if (i + static_cast<size_t>(len) > n) {
      emit_replacement();
      ++i;
      continue;
    }

    bool valid = true;
    for (int k = 1; k < len; ++k) {
      unsigned char cb = s[i + k];
      if ((cb & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      cp = (cp << 6) | (cb & 0x3F);
    }
    if (!valid) {
      emit_replacement();
      ++i;
      continue;
    }

    // Reject overlong encodings, out-of-range, and UTF-16 surrogates.
    if (len == 2 && cp < 0x80) { emit_replacement(); ++i; continue; }
    if (len == 3 && cp < 0x800) { emit_replacement(); ++i; continue; }
    if (len == 4 && cp < 0x10000) { emit_replacement(); ++i; continue; }
    if (cp > 0x10FFFF) { emit_replacement(); ++i; continue; }
    if (cp >= 0xD800 && cp <= 0xDFFF) { emit_replacement(); ++i; continue; }

    // Valid sequence: keep the original bytes.
    for (int k = 0; k < len; ++k) out.push_back(static_cast<char>(s[i + k]));
    i += len;
  }
  return out;
}

// Recursively sanitize every string value in a JSON tree in place. Object keys
// are left untouched (they are always ASCII identifiers in this codebase).
inline void sanitize_json_strings(nlohmann::json& value) {
  switch (value.type()) {
    case nlohmann::json::value_t::string:
      value = sanitize_utf8(value.get<std::string>());
      break;
    case nlohmann::json::value_t::array:
      for (auto& el : value) sanitize_json_strings(el);
      break;
    case nlohmann::json::value_t::object: {
      // Collect keys first so we don't invalidate iterators while mutating.
      std::vector<std::string> keys;
      keys.reserve(value.size());
      for (auto it = value.begin(); it != value.end(); ++it) {
        keys.push_back(it.key());
      }
      for (const auto& k : keys) sanitize_json_strings(value[k]);
      break;
    }
    default:
      break;  // numbers / bool / null need no sanitization
  }
}

}  // namespace utils
}  // namespace qcode
