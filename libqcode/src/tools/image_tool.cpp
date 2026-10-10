#include <qcode/tools/image_tool.h>

#include <qcode/core/logger.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>

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

namespace {

// Big/little endian readers over a byte range. The buffers are untrusted
// file bytes, so every read is bounds checked by the caller's size test.
uint32_t be16(const std::string_view d, std::size_t off) {
  return (static_cast<uint32_t>(static_cast<unsigned char>(d[off])) << 8) |
         static_cast<uint32_t>(static_cast<unsigned char>(d[off + 1]));
}

uint32_t be32(const std::string_view d, std::size_t off) {
  return (static_cast<uint32_t>(static_cast<unsigned char>(d[off])) << 24) |
         (static_cast<uint32_t>(static_cast<unsigned char>(d[off + 1])) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(d[off + 2])) << 8) |
         static_cast<uint32_t>(static_cast<unsigned char>(d[off + 3]));
}

uint32_t le16(const std::string_view d, std::size_t off) {
  return static_cast<uint32_t>(static_cast<unsigned char>(d[off])) |
         (static_cast<uint32_t>(static_cast<unsigned char>(d[off + 1])) << 8);
}

// Container headers are attacker-controlled: a crafted 32-bit png field can
// exceed int range, and the narrowing would wrap negative and slip past the
// limit check below. Every value this large is refused anyway, so clamp into a
// range the comparison can actually see.
int dim_from_bits(uint32_t v) {
  return v > 100'000u ? 100'000 : static_cast<int>(v);
}

uint32_t le24(const std::string_view d, std::size_t off) {
  return static_cast<uint32_t>(static_cast<unsigned char>(d[off])) |
         (static_cast<uint32_t>(static_cast<unsigned char>(d[off + 1])) << 8) |
         (static_cast<uint32_t>(static_cast<unsigned char>(d[off + 2])) << 16);
}

}  // namespace

// Pixel size straight out of the container header: png IHDR, gif logical
// screen descriptor, the first jpeg SOFn frame header, or the webp VP8X /
// VP8 / VP8L chunk. Costs a few byte comparisons and never decodes pixels,
// which is all the tool needs to tell the model what it is looking at and to
// refuse an image a provider would reject.
std::optional<ImageSize> ImageTool::dimensions(std::string_view data) {
  // png: 8-byte signature, then an IHDR chunk whose data starts at 16.
  if (data.size() >= 24 && data.starts_with("\x89PNG\r\n\x1a\n")) {
    return ImageSize{dim_from_bits(be32(data, 16)),
                     dim_from_bits(be32(data, 20))};
  }

  // gif: width/height follow the 6-byte header, little endian.
  if (data.size() >= 10 &&
      (data.starts_with("GIF87a") || data.starts_with("GIF89a"))) {
    return ImageSize{static_cast<int>(le16(data, 6)),
                     static_cast<int>(le16(data, 8))};
  }

  // webp: RIFF container, then a VP8X / VP8 / VP8L chunk.
  if (data.size() >= 16 && data.starts_with("RIFF") &&
      data.substr(8, 4) == "WEBP") {
    const std::string_view chunk = data.substr(12, 4);
    if (chunk == "VP8X") {  // 24-bit canvas size, stored as value - 1
      if (data.size() < 30) return std::nullopt;
      return ImageSize{static_cast<int>(le24(data, 24) + 1),
                       static_cast<int>(le24(data, 27) + 1)};
    }
    if (chunk == "VP8 ") {  // lossy: 14-bit dimensions after the frame tag
      if (data.size() < 30) return std::nullopt;
      return ImageSize{static_cast<int>(le16(data, 26) & 0x3FFF),
                       static_cast<int>(le16(data, 28) & 0x3FFF)};
    }
    if (chunk == "VP8L") {  // lossless: 14-bit width/minus-one, 14-bit height
      if (data.size() < 25) return std::nullopt;
      const unsigned b1 = static_cast<unsigned char>(data[21]);
      const unsigned b2 = static_cast<unsigned char>(data[22]);
      const unsigned b3 = static_cast<unsigned char>(data[23]);
      const unsigned b4 = static_cast<unsigned char>(data[24]);
      return ImageSize{static_cast<int>((b1 | ((b2 & 0x3F) << 8)) + 1),
                       static_cast<int>(((b2 >> 6) | (b3 << 2) |
                                         ((b4 & 0x0F) << 10)) + 1)};
    }
    return std::nullopt;
  }

  // jpeg: walk the marker segments to the first SOFn (frame header), which
  // carries the size. SOI, TEM and restart markers have no length; DHT (C4),
  // JPG (C8) and DAC (CC) share the SOF number range but are not frame
  // headers. EOI (D9) and SOS (DA) mean no frame header follows.
  if (data.size() >= 4 && data.starts_with("\xFF\xD8\xFF")) {
    std::size_t i = 2;
    while (i + 1 < data.size()) {
      if (static_cast<unsigned char>(data[i]) != 0xFF) {
        ++i;
        continue;
      }
      // 0xFF may be repeated as fill before the marker byte.
      std::size_t m = i + 1;
      while (m < data.size() && static_cast<unsigned char>(data[m]) == 0xFF) ++m;
      if (m >= data.size()) break;
      const unsigned marker = static_cast<unsigned char>(data[m]);
      if (marker == 0xD9 || marker == 0xDA) break;
      if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7) ||
          marker == 0xD8) {
        i = m + 1;
        continue;
      }
      if (m + 3 > data.size()) break;
      const std::size_t seg_len = be16(data, m + 1);
      if (seg_len < 2) break;
      if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
          marker != 0xC8 && marker != 0xCC) {
        // marker(1) length(2) precision(1) height(2) width(2)
        if (m + 8 > data.size()) break;
        return ImageSize{static_cast<int>(be16(data, m + 6)),
                         static_cast<int>(be16(data, m + 4))};
      }
      i = m + 1 + seg_len;
    }
  }

  return std::nullopt;
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
  std::string out =
      "Loaded image " + result.value("path", std::string("image")) + " (" +
      result.value("mime_type", std::string()) + ", " +
      std::to_string(result.value("size_bytes", std::uint64_t{0})) + " bytes";
  // Width/height are absent from results written by an older build, and from
  // a container whose header did not yield them; the size is still useful.
  const int width = result.value("width", 0);
  const int height = result.value("height", 0);
  if (width > 0 && height > 0) {
    out += ", " + std::to_string(width) + "x" + std::to_string(height);
  }
  out += ")";
  return out;
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

  // Byte size alone does not bound what a provider accepts: Anthropic
  // rejects any image over 8000px on a side, so refuse it here with the same
  // actionable hint the byte limit gives, instead of 400ing the turn later.
  std::optional<ImageSize> dim = dimensions(bytes);
  if (dim.has_value() && (dim->width <= 0 || dim->height <= 0)) dim.reset();
  if (dim.has_value() &&
      (dim->width > kMaxDimension || dim->height > kMaxDimension)) {
    throw ToolError("Image " + path + " is " + std::to_string(dim->width) + "x" +
                    std::to_string(dim->height) +
                    "; the limit is " + std::to_string(kMaxDimension) +
                    "px per side. Downscale it first (e.g. `convert " + path +
                    " -resize " + std::to_string(kRecommendedDimension) + "x" +
                    std::to_string(kRecommendedDimension) + " smaller.png`) and "
                    "load that.");
  }

  JsonValue out{{"path", path},
                {"mime_type", mime},
                {"size_bytes", size},
                {"data", base64_encode(bytes)}};
  if (dim.has_value()) {
    out["width"] = dim->width;
    out["height"] = dim->height;
  }
  LOG_INFO("ImageTool: loaded '{}' mime={} bytes={} dims={}x{}", path, mime, size,
           dim.has_value() ? dim->width : 0, dim.has_value() ? dim->height : 0);
  return out;
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
