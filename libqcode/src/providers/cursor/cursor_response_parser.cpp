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

uint32_t read_be32(const std::string& s, size_t off) {
  return (static_cast<uint32_t>(static_cast<unsigned char>(s[off])) << 24) |
         (static_cast<uint32_t>(static_cast<unsigned char>(s[off + 1])) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(s[off + 2])) << 8) |
         static_cast<uint32_t>(static_cast<unsigned char>(s[off + 3]));
}

// AgentServerMessage { 1:interaction_update { 1:text_delta { 1:text } } }
std::string extract_text_delta(const std::string& agent_server_msg) {
  std::string out;
  for (const auto& top : parse_fields(agent_server_msg)) {
    if (!(top.num == 1 && top.wire == 2)) continue;  // interaction_update
    for (const auto& upd : parse_fields(top.bytes)) {
      // 1:text_delta only — thinking_delta (4) is not assistant-visible text
      if (!(upd.num == 1 && upd.wire == 2)) continue;
      for (const auto& delta : parse_fields(upd.bytes)) {
        if (delta.num == 1 && delta.wire == 2 && !delta.bytes.empty()) {
          out += delta.bytes;
        }
      }
    }
  }
  return out;
}

bool is_connect_heartbeat(const std::string& payload) {
  // Connect keepalive: field 1 = "j\0" (0a 02 6a 00).
  return payload.size() == 4 &&
         static_cast<unsigned char>(payload[0]) == 0x0a &&
         static_cast<unsigned char>(payload[1]) == 0x02 &&
         static_cast<unsigned char>(payload[2]) == 0x6a &&
         static_cast<unsigned char>(payload[3]) == 0x00;
}

bool looks_like_json_error(const std::string& payload) {
  return payload.find("\"error\"") != std::string::npos ||
         payload.find("\"message\"") != std::string::npos;
}

// application/connect+proto body: repeated <flag:1><len:4 BE><payload>
std::string parse_binary_connect_stream(const std::string& body) {
  std::string out;
  size_t off = 0;
  const size_t n = body.size();
  while (off + 5 <= n) {
    const auto flag = static_cast<unsigned char>(body[off]);
    const uint32_t len = read_be32(body, off + 1);
    off += 5;
    if (off + len > n) break;
    const std::string payload = body.substr(off, len);
    off += len;
    if (len == 0) continue;
    (void)flag;
    std::string chunk = extract_text_delta(payload);
    if (chunk.empty()) chunk = proto::extract_text(payload);
    if (!chunk.empty()) out += chunk;
  }
  return out;
}
}  // namespace

std::vector<std::string> ConnectFrameBuffer::feed(std::string_view chunk) {
  pending_.append(chunk.data(), chunk.size());
  std::vector<std::string> frames;
  size_t off = 0;
  while (off + 5 <= pending_.size()) {
    const uint32_t len = read_be32(pending_, off + 1);
    if (off + 5 + len > pending_.size()) break;
    frames.push_back(pending_.substr(off + 5, len));
    off += 5 + len;
  }
  if (off > 0) {
    pending_.erase(0, off);
  }
  return frames;
}

AgentStreamEvent CursorResponseParser::classify_agent_payload(
    const std::string& payload) {
  AgentStreamEvent ev;
  if (payload.empty() || is_connect_heartbeat(payload)) {
    ev.kind = AgentStreamEvent::Kind::kHeartbeat;
    return ev;
  }

  // Connect end-stream error frames often carry JSON.
  if (looks_like_json_error(payload) &&
      payload.find('{') != std::string::npos) {
    ev.kind = AgentStreamEvent::Kind::kError;
    ev.error = payload;
    return ev;
  }

  const auto top = parse_fields(payload);
  for (const auto& f : top) {
    if (f.num == 1 && f.wire == 2) {  // interaction_update
      bool saw_turn_ended = false;
      bool saw_token_delta = false;
      std::string text;
      for (const auto& upd : parse_fields(f.bytes)) {
        if (upd.num == 14) {
          saw_turn_ended = true;
        }
        if (upd.num == 8) {
          saw_token_delta = true;
        }
        if (upd.num == 1 && upd.wire == 2) {  // text_delta only (not thinking)
          for (const auto& delta : parse_fields(upd.bytes)) {
            if (delta.num == 1 && delta.wire == 2 && !delta.bytes.empty()) {
              text += delta.bytes;
            }
          }
        }
      }
      if (saw_turn_ended) {
        ev.kind = AgentStreamEvent::Kind::kTurnEnded;
        ev.text = std::move(text);
        return ev;
      }
      if (!text.empty()) {
        ev.kind = AgentStreamEvent::Kind::kTextDelta;
        ev.text = std::move(text);
        return ev;
      }
      if (saw_token_delta) {
        ev.kind = AgentStreamEvent::Kind::kPostTextTokenDelta;
        return ev;
      }
      ev.kind = AgentStreamEvent::Kind::kOther;
      return ev;
    }
    if (f.num == 2 && f.wire == 2) {  // exec_server_message
      bool is_request_context = false;
      for (const auto& exec : parse_fields(f.bytes)) {
        if (exec.num == 1 && exec.wire == 0) {
          ev.exec_id = static_cast<uint32_t>(exec.varint);
        }
        if (exec.num == 15 && exec.wire == 2) {
          ev.exec_id_str = exec.bytes;
        }
        if (exec.num == 10 && exec.wire == 2) {
          is_request_context = true;
        }
      }
      if (is_request_context) {
        ev.kind = AgentStreamEvent::Kind::kRequestContext;
        return ev;
      }
      ev.kind = AgentStreamEvent::Kind::kOther;
      return ev;
    }
  }
  ev.kind = AgentStreamEvent::Kind::kOther;
  return ev;
}

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
    const std::string& body) {
  if (!body.empty() && body.find("data:") == std::string::npos) {
    const auto binary = parse_binary_connect_stream(body);
    if (!binary.empty()) return binary;
  }

  std::string out;
  size_t start = 0;
  const size_t n = body.size();
  while (start < n) {
    size_t nl = body.find('\n', start);
    const std::string line = (nl == std::string::npos)
                                 ? body.substr(start)
                                 : body.substr(start, nl - start);
    start = (nl == std::string::npos) ? n : nl + 1;
    const std::string payload = proto::decode_connect_payload(line);
    if (payload.empty()) continue;
    std::string chunk = extract_text_delta(payload);
    if (chunk.empty()) chunk = proto::extract_text(payload);
    if (!chunk.empty()) out += chunk;
  }
  return out;
}

}  // namespace cursor
}  // namespace ai
