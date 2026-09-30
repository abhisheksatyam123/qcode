#pragma once

#include <qcode/core/tool.h>

#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace qcode {

using JsonValue = nlohmann::json;

namespace ImageToolSchema {

inline JsonValue parameters() {
  JsonValue s;
  s["type"] = "object";
  s["properties"] = JsonValue::object();
  auto& p = s["properties"];

  p["path"] = JsonValue{
    {"type", "string"},
    {"description", "Path to the image file (relative to workspace or absolute). Supported formats: png, jpg, jpeg, webp, gif, bmp, svg."}
  };
  p["detail"] = JsonValue{
    {"type", "string"},
    {"enum", {"auto", "low", "high"}},
    {"description", "Optional fidelity level for vision models (auto, low, high). Default auto."}
  };
  p["description"] = JsonValue{
    {"type", "string"},
    {"description", "Optional brief description or question about what to observe in the image."}
  };

  s["required"] = JsonValue::array({"path"});
  return s;
}

}  // namespace ImageToolSchema

class ImageTool {
 public:
  static JsonValue execute(const JsonValue& args, const ToolExecutionContext& context);
  static Tool definition();

  static std::string detect_mime_type(const std::string& path, std::string_view data);
  static bool is_supported_image(const std::string& path);
  static std::string base64_encode(std::string_view in);
};

}  // namespace qcode
