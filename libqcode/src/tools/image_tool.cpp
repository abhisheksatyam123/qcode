#include <qcode/tools/image_tool.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <qcode/core/logger.h>

namespace qcode {

namespace {

constexpr size_t kMaxImageBytes = 20 * 1024 * 1024;  // 20 MB

std::string to_lower_ascii(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

std::filesystem::path resolve_image_path(const std::string& input_path,
                                         const std::string& workspace) {
  if (input_path.empty()) return {};

  std::filesystem::path p(input_path);
  if (input_path.rfind("~/", 0) == 0) {
    if (const char* home = std::getenv("HOME")) {
      p = std::filesystem::path(home) / input_path.substr(2);
    }
  } else if (p.is_relative() && !workspace.empty()) {
    p = std::filesystem::path(workspace) / p;
  }
  return p;
}

}  // namespace

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

bool ImageTool::is_supported_image(const std::string& path) {
  std::filesystem::path p(path);
  const std::string ext = to_lower_ascii(p.extension().string());
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
         ext == ".webp" || ext == ".gif" || ext == ".bmp" ||
         ext == ".svg";
}

std::string ImageTool::detect_mime_type(const std::string& path,
                                        std::string_view data) {
  // Check magic bytes first if enough data is available
  if (data.size() >= 8) {
    const auto* u = reinterpret_cast<const unsigned char*>(data.data());
    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (u[0] == 0x89 && u[1] == 'P' && u[2] == 'N' && u[3] == 'G' &&
        u[4] == 0x0D && u[5] == 0x0A && u[6] == 0x1A && u[7] == 0x0A) {
      return "image/png";
    }
  }
  if (data.size() >= 3) {
    const auto* u = reinterpret_cast<const unsigned char*>(data.data());
    // JPEG: FF D8 FF
    if (u[0] == 0xFF && u[1] == 0xD8 && u[2] == 0xFF) {
      return "image/jpeg";
    }
    // GIF: GIF87a or GIF89a
    if (data.starts_with("GIF87a") || data.starts_with("GIF89a")) {
      return "image/gif";
    }
    // BMP: BM
    if (data.size() >= 2 && u[0] == 'B' && u[1] == 'M') {
      return "image/bmp";
    }
  }
  if (data.size() >= 12 && data.starts_with("RIFF") &&
      data.substr(8, 4) == "WEBP") {
    return "image/webp";
  }
  // SVG check
  if (!data.empty()) {
    std::string_view head = data.substr(0, std::min<size_t>(data.size(), 512));
    if (head.find("<svg") != std::string_view::npos ||
        (head.find("<?xml") != std::string_view::npos &&
         head.find("<svg") != std::string_view::npos)) {
      return "image/svg+xml";
    }
  }

  // Fallback to extension check
  std::filesystem::path p(path);
  const std::string ext = to_lower_ascii(p.extension().string());
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".webp") return "image/webp";
  if (ext == ".gif") return "image/gif";
  if (ext == ".bmp") return "image/bmp";
  if (ext == ".svg") return "image/svg+xml";

  return {};
}

JsonValue ImageTool::execute(const JsonValue& args,
                             const ToolExecutionContext& context) {
  if (!args.is_object() || !args.contains("path") ||
      !args["path"].is_string()) {
    return JsonValue{{"error", "Missing required string parameter 'path'"}};
  }

  const std::string raw_path = args["path"].get<std::string>();
  if (raw_path.empty()) {
    return JsonValue{{"error", "Parameter 'path' cannot be empty"}};
  }

  std::string description;
  if (args.contains("description") && args["description"].is_string()) {
    description = args["description"].get<std::string>();
  }

  std::string detail = "auto";
  if (args.contains("detail") && args["detail"].is_string()) {
    detail = args["detail"].get<std::string>();
  }

  const auto target_path = resolve_image_path(raw_path, context.workspace);
  std::error_code ec;
  if (!std::filesystem::exists(target_path, ec)) {
    return JsonValue{{"error", "File not found: " + raw_path}};
  }
  if (!std::filesystem::is_regular_file(target_path, ec)) {
    return JsonValue{{"error", "Target is not a regular file: " + raw_path}};
  }

  const auto file_size = std::filesystem::file_size(target_path, ec);
  if (ec) {
    return JsonValue{{"error", "Failed to get file size for " + raw_path + ": " + ec.message()}};
  }
  if (file_size == 0) {
    return JsonValue{{"error", "Image file is empty (0 bytes): " + raw_path}};
  }
  if (file_size > kMaxImageBytes) {
    return JsonValue{
        {"error", "Image file exceeds 20MB limit (" +
                      std::to_string(file_size) + " bytes): " + raw_path}};
  }

  std::ifstream file(target_path, std::ios::binary);
  if (!file) {
    return JsonValue{{"error", "Could not open file for reading: " + raw_path}};
  }

  std::string buffer;
  buffer.resize(static_cast<size_t>(file_size));
  if (!file.read(buffer.data(), static_cast<std::streamsize>(file_size))) {
    return JsonValue{{"error", "Failed to read binary content of: " + raw_path}};
  }

  const std::string mime = detect_mime_type(target_path.string(), buffer);
  if (mime.empty()) {
    return JsonValue{
        {"error", "Unsupported image format for '" + raw_path +
                      "'. Supported formats: png, jpg, jpeg, webp, gif, bmp, svg"}};
  }

  std::string b64 = base64_encode(buffer);

  std::string display_path = raw_path;
  if (!context.workspace.empty()) {
    try {
      auto rel = std::filesystem::relative(target_path, context.workspace, ec);
      if (!ec && !rel.empty()) {
        display_path = rel.string();
      }
    } catch (...) {}
  }

  JsonValue res = JsonValue::object();
  res["success"] = true;
  res["path"] = display_path;
  res["mime_type"] = mime;
  res["size_bytes"] = file_size;
  res["data"] = std::move(b64);
  if (!description.empty()) {
    res["description"] = description;
  }
  if (!detail.empty()) {
    res["detail"] = detail;
  }

  LOG_INFO("ImageTool: loaded image '{}' mime={} bytes={}",
           display_path, mime, file_size);
  return res;
}

Tool ImageTool::definition() {
  Tool t(
      "Inspect and load image files into the model context for visual reasoning (png, jpg, webp, gif, bmp, svg).",
      ImageToolSchema::parameters(),
      [](const JsonValue& args, const ToolExecutionContext& context) -> JsonValue {
        return ImageTool::execute(args, context);
      });
  t.name = "image";
  return t;
}

}  // namespace qcode
