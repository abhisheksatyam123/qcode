#include "cursor_proto.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace qcode {
namespace cursor {
namespace proto {

std::string encode_varint(uint64_t value) {
  std::string out;
  do {
    uint8_t byte = static_cast<uint8_t>(value & 0x7F);
    value = value / 128;
    if (value) byte |= 0x80;
    out.push_back(static_cast<char>(byte));
  } while (value);
  return out;
}

std::string bytes_field(uint32_t field_number, const std::string& value) {
  const uint32_t tag = (field_number * 8) | 2;
  std::string out = encode_varint(tag);
  out += encode_varint(value.size());
  out += value;
  return out;
}

std::string varint_field(uint32_t field_number, uint64_t value) {
  const uint32_t tag = (field_number * 8) | 0;
  std::string out = encode_varint(tag);
  out += encode_varint(value);
  return out;
}

std::vector<Field> parse_fields(const std::string& data) {
  std::vector<Field> out;
  size_t off = 0;
  const size_t n = data.size();
  while (off < n) {
    uint64_t tag = 0;
    uint64_t mult = 1;
    while (off < n) {
      const auto b = static_cast<uint8_t>(data[off++]);
      tag += static_cast<uint64_t>(b & 0x7F) * mult;
      mult *= 128;
      if (!(b & 0x80)) break;
    }
    if (off > n) break;
    Field f;
    f.num = static_cast<uint32_t>(tag / 8);
    f.wire = static_cast<uint32_t>(tag & 0x7);
    if (f.wire == 0) {
      uint64_t v = 0;
      mult = 1;
      while (off < n) {
        const auto b = static_cast<uint8_t>(data[off++]);
        v += static_cast<uint64_t>(b & 0x7F) * mult;
        mult *= 128;
        if (!(b & 0x80)) break;
      }
      f.varint = v;
    } else if (f.wire == 1) {
      if (off + 8 > n) break;
      f.bytes = data.substr(off, 8);
      off += 8;
    } else if (f.wire == 5) {
      if (off + 4 > n) break;
      f.bytes = data.substr(off, 4);
      off += 4;
    } else if (f.wire == 2) {
      uint64_t len = 0;
      mult = 1;
      while (off < n) {
        const auto b = static_cast<uint8_t>(data[off++]);
        len += static_cast<uint64_t>(b & 0x7F) * mult;
        mult *= 128;
        if (!(b & 0x80)) break;
      }
      if (off + len > n) break;
      f.bytes = data.substr(off, static_cast<size_t>(len));
      off += static_cast<size_t>(len);
    } else {
      break;
    }
    out.push_back(std::move(f));
  }
  return out;
}

std::string field_string(const std::vector<Field>& fields, uint32_t field_number) {
  for (const auto& f : fields) {
    if (f.num == field_number && f.wire == 2) return f.bytes;
  }
  return {};
}

uint64_t field_varint(const std::vector<Field>& fields, uint32_t field_number,
                      uint64_t fallback) {
  for (const auto& f : fields) {
    if (f.num == field_number && f.wire == 0) return f.varint;
  }
  return fallback;
}

bool has_field(const std::vector<Field>& fields, uint32_t field_number) {
  for (const auto& f : fields) {
    if (f.num == field_number) return true;
  }
  return false;
}

std::string envelope(const std::string& payload) {
  std::string out;
  out.push_back('\x00');  // flag: uncompressed
  const uint32_t len = static_cast<uint32_t>(payload.size());
  out.push_back(static_cast<char>((len / 16777216) & 0xFF));
  out.push_back(static_cast<char>(((len / 65536) & 0xFF)));
  out.push_back(static_cast<char>(((len / 256) & 0xFF)));
  out.push_back(static_cast<char>(len & 0xFF));
  out += payload;
  return out;
}

namespace {
int b64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

std::string base64_decode(const std::string& in) {
  std::string out;
  int accum = 0, bits = 0;
  for (char c : in) {
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    const int v = b64_value(c);
    if (v < 0) continue;
    accum = (accum * 64) + v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((accum >> bits) & 0xFF));
      accum &= (static_cast<uint64_t>(1) << bits) - 1;  // keep only remaining bits
    }
  }
  return out;
}

bool looks_like_text(const std::string& s) {
  if (s.size() < 2) return false;
  bool has_alpha = false, has_space = false;
  for (unsigned char ch : s) {
    if (std::isalpha(ch)) has_alpha = true;
    if (ch == ' ') has_space = true;
    if (!std::isprint(ch) && !std::isspace(ch)) return false;
  }
  if (!has_alpha) return false;
  if (!has_space && s.size() < 24) return false;
  return true;
}

void walk(const std::string& data, std::vector<std::string>& out) {
  size_t off = 0;
  const size_t n = data.size();
  while (off < n) {
    uint64_t tag = 0, mult = 1;
    while (off < n) {
      uint8_t b = static_cast<uint8_t>(data[off++]);
      tag += static_cast<uint64_t>(b & 0x7F) * mult;
      mult *= 128;
      if (!(b & 0x80)) break;
    }
    if (off > n) break;
    const uint32_t field = static_cast<uint32_t>(tag / 8);
    const uint32_t wire = static_cast<uint32_t>(tag & 0x7);
    (void)field;
    if (wire == 0) {
      while (off < n && (data[off] & 0x80)) ++off;
      if (off < n) ++off;
    } else if (wire == 1) {
      off += 8;
    } else if (wire == 5) {
      off += 4;
    } else if (wire == 2) {
      uint64_t len = 0;
      mult = 1;
      while (off < n) {
        uint8_t b = static_cast<uint8_t>(data[off++]);
        len += static_cast<uint64_t>(b & 0x7F) * mult;
        mult *= 128;
        if (!(b & 0x80)) break;
      }
      if (off + len > n) break;
      std::string sub = data.substr(off, static_cast<size_t>(len));
      off += static_cast<size_t>(len);
      if (looks_like_text(sub)) out.push_back(sub);
      walk(sub, out);
    } else {
      return;
    }
  }
}
}  // namespace

std::string decode_connect_payload(const std::string& sse_line) {
  std::string line = sse_line;
  if (line.rfind("data:", 0) == 0) line = line.substr(5);
  const size_t a = line.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  const size_t b = line.find_last_not_of(" \t\r\n");
  line = line.substr(a, b - a + 1);

  std::string frame = base64_decode(line);
  if (frame.size() < 5) return "";
  const uint32_t len =
      (static_cast<uint32_t>(static_cast<unsigned char>(frame[1])) * 16777216) |
      (static_cast<uint32_t>(static_cast<unsigned char>(frame[2])) * 65536) |
      (static_cast<uint32_t>(static_cast<unsigned char>(frame[3])) * 256) |
      static_cast<uint32_t>(static_cast<unsigned char>(frame[4]));
  if (frame.size() < 5u + len) return "";
  return frame.substr(5, len);
}

std::string extract_text(const std::string& payload) {
  std::vector<std::string> parts;
  walk(payload, parts);
  std::string out;
  for (const auto& p : parts) {
    if (!out.empty()) out += "\n";
    out += p;
  }
  return out;
}

}  // namespace proto
}  // namespace cursor
}  // namespace qcode
