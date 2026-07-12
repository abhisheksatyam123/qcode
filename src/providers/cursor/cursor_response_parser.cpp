#include "cursor_response_parser.h"

#include "ai/types/embedding_options.h"
#include "ai/types/generate_options.h"
#include "cursor_proto.h"

#include <string>
#include <vector>

namespace ai {
namespace cursor {

namespace {
struct Field {
  uint32_t num = 0;
  uint32_t wire = 0;
  std::string bytes;
  uint64_t varint = 0;
};

std::vector<Field> parse_fields(const std::string& d) {
  std::vector<Field> out;
  size_t off = 0;
  const size_t n = d.size();
  while (off < n) {
    uint64_t tag = 0, mult = 1;
    while (off < n) {
      uint8_t b = static_cast<uint8_t>(d[off++]);
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
        uint8_t b = static_cast<uint8_t>(d[off++]);
        v += static_cast<uint64_t>(b & 0x7F) * mult;
        mult *= 128;
        if (!(b & 0x80)) break;
      }
      f.varint = v;
    } else if (f.wire == 1) {
      if (off + 8 > n) break;
      f.bytes = d.substr(off, 8);
      off += 8;
    } else if (f.wire == 5) {
      if (off + 4 > n) break;
      f.bytes = d.substr(off, 4);
      off += 4;
    } else if (f.wire == 2) {
      uint64_t len = 0;
      mult = 1;
      while (off < n) {
        uint8_t b = static_cast<uint8_t>(d[off++]);
        len += static_cast<uint64_t>(b & 0x7F) * mult;
        mult *= 128;
        if (!(b & 0x80)) break;
      }
      if (off + len > n) break;
      f.bytes = d.substr(off, static_cast<size_t>(len));
      off += static_cast<size_t>(len);
    } else {
      break;
    }
    out.push_back(f);
  }
  return out;
}
}  // namespace

GenerateResult CursorResponseParser::parse_success_completion_response(
    const nlohmann::json&) {
  return GenerateResult();
}

GenerateResult CursorResponseParser::parse_error_completion_response(
    int, const std::string& body) {
  return GenerateResult(body.empty() ? "cursor error" : body);
}

EmbeddingResult CursorResponseParser::parse_success_embedding_response(
    const nlohmann::json&) {
  return EmbeddingResult();
}

EmbeddingResult CursorResponseParser::parse_error_embedding_response(
    int, const std::string& body) {
  return EmbeddingResult(body.empty() ? "cursor embedding error" : body);
}

std::vector<std::string> CursorResponseParser::parse_get_usable_models(
    const std::string& body) {
  std::vector<std::string> models;
  const auto top = parse_fields(body);
  for (const auto& f : top) {
    if (f.num == 1 && f.wire == 2) {  // models: ModelDetails[]
      const auto model = parse_fields(f.bytes);
      for (const auto& g : model) {
        if (g.num == 1 && g.wire == 2) {  // model_id
          models.push_back(g.bytes);
        }
      }
    }
  }
  return models;
}

std::string CursorResponseParser::parse_agent_stream_body(
    const std::string& sse_body) {
  std::string out;
  size_t start = 0;
  const size_t n = sse_body.size();
  while (start < n) {
    size_t nl = sse_body.find('\n', start);
    const std::string line =
        (nl == std::string::npos) ? sse_body.substr(start)
                                   : sse_body.substr(start, nl - start);
    start = (nl == std::string::npos) ? n : nl + 1;
    const std::string payload = proto::decode_connect_payload(line);
    if (payload.empty()) continue;
    const std::string text = proto::extract_text(payload);
    if (!text.empty()) {
      if (!out.empty()) out += "\n";
      out += text;
    }
  }
  return out;
}

}  // namespace cursor
}  // namespace ai
