#pragma once

#include <qcode/core/message.h>
#include <qcode/core/tool.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace qcode {

// `image` tool: loads a local png/jpeg/webp/gif so a vision model can see it.
//
// Success result (stored and persisted as-is): {path, mime_type, size_bytes,
// data (base64)}. It never reaches a provider in that form:
// ProviderTransform::lift_tool_result_images turns it into a short text tool
// result plus a typed user ImageContentPart, which every request builder
// already serializes. Failures throw, so they surface as is_error results.
class ImageTool {
 public:
  // Base64 of this stays under Anthropic's 5 MB per-image limit.
  static constexpr size_t kMaxBytes = 3'750'000;

  static Tool definition();
  static JsonValue execute(const JsonValue& args,
                           const ToolExecutionContext& context);

  // True for a successful image tool result (the shape `execute` returns).
  static bool is_image_result(const ToolResultContentPart& part);
  // "Loaded image a.png (image/png, 1024 bytes)" for an image result.
  static std::string summary(const JsonValue& result);

  // MIME type from magic bytes; empty unless png/jpeg/webp/gif.
  static std::string detect_mime_type(std::string_view data);
  // png/jpeg/webp/gif: the formats every vision provider accepts.
  static bool is_supported_mime_type(std::string_view mime_type);
  static std::string base64_encode(std::string_view in);
};

}  // namespace qcode
