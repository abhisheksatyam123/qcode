#include "cursor_kv.h"

#include "cursor_proto.h"

#include <qcode/core/logger.h>

namespace qcode {
namespace cursor {
namespace {

std::string wrap_kv_client(uint32_t id, uint32_t result_field,
                           const std::string& result) {
  const auto kv =
      proto::varint_field(1, id) + proto::bytes_field(result_field, result);
  return proto::bytes_field(3, kv);
}

}  // namespace

std::string CursorKv::handle(const std::string& kv_server_message) {
  const auto fields = proto::parse_fields(kv_server_message);
  const auto id = static_cast<uint32_t>(proto::field_varint(fields, 1));

  if (proto::has_field(fields, 2)) {
    const auto args = proto::parse_fields(proto::field_string(fields, 2));
    const auto blob_id = proto::field_string(args, 1);
    std::string data;
    {
      std::lock_guard<std::mutex> lock(mu_);
      const auto it = blobs_.find(blob_id);
      if (it != blobs_.end()) data = it->second;
    }
    LOG_INFO("Cursor kv get id={} key_len={} hit={} data_len={}", id,
             blob_id.size(), !data.empty(), data.size());
    std::string result;
    if (!data.empty()) result = proto::bytes_field(1, data);
    return wrap_kv_client(id, 2, result);
  }

  if (proto::has_field(fields, 3)) {
    const auto args = proto::parse_fields(proto::field_string(fields, 3));
    const auto blob_id = proto::field_string(args, 1);
    const auto blob_data = proto::field_string(args, 2);
    {
      std::lock_guard<std::mutex> lock(mu_);
      blobs_[blob_id] = blob_data;
    }
    LOG_INFO("Cursor kv set id={} key_len={} data_len={}", id, blob_id.size(),
             blob_data.size());
    return wrap_kv_client(id, 3, std::string{});
  }

  LOG_WARN("Cursor kv unknown message id={} fields={}", id, fields.size());
  return wrap_kv_client(id, 3, std::string{});
}

}  // namespace cursor
}  // namespace qcode
