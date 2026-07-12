// Internal protobuf / connect-es wire helpers for the Cursor provider.
//
// Cursor's aiserver/agent endpoints speak protobuf. ServerStreaming (RunSSE) uses
// the connect protocol: each SSE `data:` line is base64( <flag:1><len:4 BE><payload> ).
// Unary aiserver responses are raw protobuf (no connect envelope).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ai {
namespace cursor {
namespace proto {

// Base-128 varint (little-endian groups of 7 bits).
std::string encode_varint(uint64_t value);

// Length-delimited field (wire type 2): string / bytes / nested message.
std::string bytes_field(uint32_t field_number, const std::string& value);

// Varint field (wire type 0): int32/int64/uint32/bool/enum.
std::string varint_field(uint32_t field_number, uint64_t value);

// connect-es envelope: 1-byte flag (0 = uncompressed) + 4-byte big-endian length +
// payload. An empty `payload` yields the canonical `00 00 00 00 00` body.
std::string envelope(const std::string& payload);

// Decode a single SSE `data:` line into its inner protobuf payload (strips the
// connect envelope). Returns "" for blank lines / non-data lines.
std::string decode_connect_payload(const std::string& sse_line);

// Recursively walk a protobuf message and collect printable string (wire type 2)
// leaf fields that look like natural-language text. Best-effort; full
// AgentRunResponse schema parsing is deferred.
std::string extract_text(const std::string& payload);

}  // namespace proto
}  // namespace cursor
}  // namespace ai
