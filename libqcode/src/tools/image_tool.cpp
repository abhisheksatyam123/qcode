#include <qcode/tools/image_tool.h>

#include <qcode/core/logger.h>

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace qcode {

std::string ImageTool::base64_encode(std::string_view in) {
  static const char* kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  int val = 0;
  int valb = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(kAlphabet[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) {
    out.push_back(kAlphabet[((val << 8) >> (valb + 8)) & 0x3F]);
  }
  while (out.size() % 4) {
    out.push_back('=');
  }
  return out;
}

// Only formats every vision provider accepts. Anything else (svg, bmp, ...)
// would be persisted in the session and 400 every later request.
std::string ImageTool::detect_mime_type(std::string_view data) {
  if (data.starts_with("\x89PNG\r\n\x1a\n")) return "image/png";
  if (data.starts_with("\xFF\xD8\xFF")) return "image/jpeg";
  if (data.starts_with("GIF87a") || data.starts_with("GIF89a")) return "image/gif";
  if (data.size() >= 12 && data.starts_with("RIFF") &&
      data.substr(8, 4) == "WEBP") {
    return "image/webp";
  }
  return {};
}

bool ImageTool::is_supported_mime_type(std::string_view mime_type) {
  return mime_type == "image/png" || mime_type == "image/jpeg" ||
         mime_type == "image/webp" || mime_type == "image/gif";
}

bool ImageTool::is_image_result(const ToolResultContentPart& part) {
  const auto& r = part.result;
  if (part.is_error || !r.is_object()) return false;
  const auto data = r.find("data");
  const auto mime = r.find("mime_type");
  return data != r.end() && data->is_string() && mime != r.end() &&
         mime->is_string();
}

std::string ImageTool::summary(const JsonValue& result) {
  return "Loaded image " + result.value("path", std::string("image")) + " (" +
         result.value("mime_type", std::string()) + ", " +
         std::to_string(result.value("size_bytes", std::uint64_t{0})) +
         " bytes)";
}

JsonValue ImageTool::execute(const JsonValue& args,
                             const ToolExecutionContext& context) {
  namespace fs = std::filesystem;
  if (!args.is_object() || !args.contains("path") ||
      !args["path"].is_string() || args["path"].get<std::string>().empty()) {
    throw ToolError("Missing required string parameter 'path'");
  }
  const std::string path = args["path"].get<std::string>();

  fs::path target(path);
  if (target.is_relative() && !context.workspace.empty()) {
    target = fs::path(context.workspace) / target;
  }

  std::error_code ec;
  if (!fs::is_regular_file(target, ec)) {
    throw ToolError("File not found: " + path);
  }
  const auto size = fs::file_size(target, ec);
  if (ec || size == 0) {
    throw ToolError("Image file is empty or unreadable: " + path);
  }
  if (size > kMaxBytes) {
    throw ToolError("Image is " + std::to_string(size) + " bytes; the limit is " +
                    std::to_string(kMaxBytes) +
                    ". Downscale it first (e.g. `convert " + path +
                    " -resize 1568x1568 smaller.png`) and load that.");
  }

  std::string bytes(size, '\0');
  std::ifstream in(target, std::ios::binary);
  if (!in.read(bytes.data(), static_cast<std::streamsize>(size))) {
    throw ToolError("Could not read " + path);
  }

  const std::string mime = detect_mime_type(bytes);
  if (mime.empty()) {
    throw ToolError("Unsupported image format: " + path +
                    ". Supported: png, jpeg, webp, gif; convert others first "
                    "(e.g. `convert in.svg out.png`).");
  }

  LOG_INFO("ImageTool: loaded '{}' mime={} bytes={}", path, mime, size);
  return JsonValue{{"path", path},
                   {"mime_type", mime},
                   {"size_bytes", size},
                   {"data", base64_encode(bytes)}};
}

Tool ImageTool::definition() {
  JsonValue schema{
      {"type", "object"},
      {"properties",
       {{"path",
         {{"type", "string"},
          {"description",
           "Image file (png, jpeg, webp or gif), relative to the workspace "
           "or absolute."}}}}},
      {"required", JsonValue::array({"path"})}};
  Tool t("Load a local image (png, jpeg, webp, gif; max 3.75 MB) so you can "
         "see it.",
         std::move(schema), &ImageTool::execute);
  t.name = "image";
  return t;
}

}  // namespace qcode
