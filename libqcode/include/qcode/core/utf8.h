#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace qcode {
namespace utils {

// Byte length of the well-formed UTF-8 sequence starting at `in[i]`, or 0 if
// there is none there: invalid lead or continuation byte, truncated sequence,
// overlong encoding, UTF-16 surrogate, or a code point above U+10FFFF.
inline size_t utf8_sequence_length(std::string_view in, size_t i) {
  const auto b = static_cast<unsigned char>(in[i]);
  if (b < 0x80) return 1;  // ASCII

  // Determine the expected sequence length from the lead byte.
  size_t len = 0;
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
    return 0;  // invalid lead byte
  }
  if (i + len > in.size()) return 0;  // missing continuation bytes

  for (size_t k = 1; k < len; ++k) {
    const auto cb = static_cast<unsigned char>(in[i + k]);
    if ((cb & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (cb & 0x3F);
  }

  // Reject overlong encodings, out-of-range, and UTF-16 surrogates.
  if (len == 2 && cp < 0x80) return 0;
  if (len == 3 && cp < 0x800) return 0;
  if (len == 4 && cp < 0x10000) return 0;
  if (cp > 0x10FFFF) return 0;
  if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
  return len;
}

inline bool is_valid_utf8(std::string_view in) {
  for (size_t i = 0; i < in.size();) {
    const size_t len = utf8_sequence_length(in, i);
    if (len == 0) return false;
    i += len;
  }
  return true;
}

// Convert `in` to valid UTF-8 by replacing any invalid byte sequence with the
// Unicode replacement character U+FFFD (EF BF BD). This guarantees the result
// can be safely embedded in and serialized from JSON -- nlohmann::json throws
// `type_error.316` ("invalid UTF-8 byte") when dumping a string that contains
// invalid UTF-8, which previously crashed request building for tool output
// that contained binary / non-UTF-8 bytes.
inline std::string sanitize_utf8(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  size_t kept = 0;  // start of the pending run of valid bytes
  for (size_t i = 0; i < in.size();) {
    if (const size_t len = utf8_sequence_length(in, i)) {
      i += len;
      continue;
    }
    // Replace the bad byte and resync on the next one.
    out.append(in.substr(kept, i - kept));
    out.append("\xEF\xBF\xBD");
    kept = ++i;
  }
  out.append(in.substr(kept));
  return out;
}

// In-place form for strings the caller owns: valid input (the common case)
// is only scanned, never copied.
inline void sanitize_utf8_in_place(std::string& s) {
  if (!is_valid_utf8(s)) s = sanitize_utf8(s);
}

// Recursively sanitize every string value in a JSON tree in place. Object keys
// are left untouched (they are always ASCII identifiers in this codebase).
inline void sanitize_json_strings(nlohmann::json& value) {
  if (value.is_string()) {
    sanitize_utf8_in_place(value.get_ref<std::string&>());
  } else if (value.is_structured()) {
    // Iterating an object yields its values; rewriting them in place does
    // not invalidate the iterators.
    for (auto& el : value) sanitize_json_strings(el);
  }
  // numbers / bool / null need no sanitization
}

}  // namespace utils
}  // namespace qcode
