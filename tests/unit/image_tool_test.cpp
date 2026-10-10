#include <qcode/session/token_budget.h>
#include <qcode/tools/image_tool.h>
#include <qcode/tools/tool_executor.h>
#include <qcode/transform/gemini_transform.h>
#include <qcode/transform/provider_transform.h>
#include <providers/anthropic/anthropic_request_builder.h>
#include <providers/cursor/cursor_request_builder.h>
#include <providers/openai/openai_client.h>
#include <providers/openai/openai_request_builder.h>

#include <gtest/gtest.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

namespace qcode {
namespace {

std::string decode_base64_for_test(std::string_view in) {
  static const int kTable[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  };
  std::string out;
  int val = 0, valb = -8;
  for (unsigned char c : in) {
    if (c == '=') break;
    if (kTable[c] == -1) continue;
    val = (val << 6) + kTable[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

// 1x1 red PNG.
const unsigned char kRedPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
    0x0c, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xfc, 0xcf, 0xc0, 0x50,
    0x0f, 0x00, 0x04, 0x85, 0x01, 0x80, 0xa4, 0xa9, 0x8c, 0xa1, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

std::string png_bytes() {
  return std::string(reinterpret_cast<const char*>(kRedPng), sizeof(kRedPng));
}

constexpr const char* kB64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJ";

JsonValue image_result(const std::string& mime = "image/png") {
  return JsonValue{{"path", "shot.png"},
                   {"mime_type", mime},
                   {"size_bytes", 32},
                   {"data", kB64}};
}

// user -> assistant calls `image` -> successful image tool result.
Messages image_tool_history(const std::string& mime = "image/png") {
  std::vector<ToolCallContentPart> calls;
  calls.emplace_back("call_1", "image", JsonValue{{"path", "shot.png"}});
  std::vector<ToolResultContentPart> results;
  results.emplace_back("call_1", image_result(mime), false);
  return {Message::user("look at the screenshot"),
          Message::assistant_with_tools("", calls),
          Message::tool_results(results)};
}

// Parallel `image` + `bash` calls, one result message each (what a session
// reload produces), image first.
Messages parallel_history() {
  std::vector<ToolCallContentPart> calls;
  calls.emplace_back("call_1", "image", JsonValue{{"path", "shot.png"}});
  calls.emplace_back("call_2", "bash", JsonValue{{"command", "ls"}});
  return {Message::user("look"),
          Message::assistant_with_tools("", calls),
          Message::tool_results({{"call_1", image_result(), false}}),
          Message::tool_results({{"call_2", JsonValue{{"output", "ok"}}, false}})};
}

class ImageToolFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("qcode_image_tool_test_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir_);
    opts_.workspace = dir_.string();
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  void write(const std::string& name, const std::string& bytes) {
    std::ofstream(dir_ / name, std::ios::binary) << bytes;
  }

  ToolResult run(const std::string& path) {
    ToolSet tools{{"image", ImageTool::definition()}};
    return ToolExecutor::execute_tool(
        ToolCall("call_img", "image", JsonValue{{"path", path}}), tools,
        &opts_);
  }

  std::filesystem::path dir_;
  GenerateOptions opts_;
};

TEST(ImageToolTest, SchemaIsJustPath) {
  Tool tool = ImageTool::definition();
  EXPECT_EQ(tool.name, "image");
  EXPECT_TRUE(tool.has_execute());
  const auto& props = tool.parameters_schema["properties"];
  EXPECT_EQ(props.size(), 1u);
  EXPECT_TRUE(props.contains("path"));
  EXPECT_EQ(tool.parameters_schema["required"], JsonValue::array({"path"}));
}

TEST(ImageToolTest, Base64EncodeAndDecodeMatch) {
  EXPECT_EQ(ImageTool::base64_encode(""), "");
  EXPECT_EQ(ImageTool::base64_encode("f"), "Zg==");
  EXPECT_EQ(ImageTool::base64_encode("fo"), "Zm8=");
  EXPECT_EQ(ImageTool::base64_encode("foo"), "Zm9v");
  EXPECT_EQ(ImageTool::base64_encode("hello world"), "aGVsbG8gd29ybGQ=");
  EXPECT_EQ(decode_base64_for_test(ImageTool::base64_encode(png_bytes())),
            png_bytes());
}

TEST(ImageToolTest, DetectsOnlyFormatsEveryProviderAccepts) {
  EXPECT_EQ(ImageTool::detect_mime_type(png_bytes()), "image/png");
  EXPECT_EQ(ImageTool::detect_mime_type("\xFF\xD8\xFF\xE0\x00\x10JFIF"),
            "image/jpeg");
  EXPECT_EQ(ImageTool::detect_mime_type("GIF89a\x01\x00\x01\x00"), "image/gif");
  EXPECT_EQ(ImageTool::detect_mime_type(
                std::string("RIFF\x00\x00\x00\x00WEBPVP8 ", 16)),
            "image/webp");
  EXPECT_EQ(ImageTool::detect_mime_type("BM\x36\x00\x00\x00"), "");
  EXPECT_EQ(ImageTool::detect_mime_type("<svg xmlns=\"x\"></svg>"), "");
  EXPECT_EQ(ImageTool::detect_mime_type("hello"), "");
  EXPECT_EQ(ImageTool::detect_mime_type(""), "");
}

TEST_F(ImageToolFileTest, LoadsImageRelativeToWorkspace) {
  write("red.png", png_bytes());
  const auto res = run("red.png");
  ASSERT_TRUE(res.is_success()) << res.error_message();
  EXPECT_EQ(res.result["path"], "red.png");
  EXPECT_EQ(res.result["mime_type"], "image/png");
  EXPECT_EQ(res.result["size_bytes"], sizeof(kRedPng));
  EXPECT_EQ(decode_base64_for_test(res.result["data"].get<std::string>()),
            png_bytes());
  EXPECT_EQ(res.result["width"], 1);
  EXPECT_EQ(res.result["height"], 1);
  EXPECT_EQ(res.result.size(), 6u);
  // The model reads this line, so it must carry the pixel size.
  EXPECT_NE(ImageTool::summary(res.result).find("1x1"), std::string::npos)
      << ImageTool::summary(res.result);
}

TEST_F(ImageToolFileTest, FailuresAreToolErrors) {
  write("vector.svg", "<svg width=\"1\" height=\"1\"></svg>");
  write("bitmap.bmp", std::string("BM\x36\x00\x00\x00", 6));
  std::string big = png_bytes();
  big.resize(ImageTool::kMaxBytes + 1, '\0');
  write("big.png", big);

  const auto expect_error = [&](const std::string& path,
                                const std::string& needle) {
    const auto res = run(path);
    EXPECT_FALSE(res.is_success()) << path;
    EXPECT_NE(res.error_message().find(needle), std::string::npos)
        << res.error_message();
    // `result` is what becomes the is_error tool result the model reads.
    ASSERT_TRUE(res.result.contains("error")) << res.result.dump();
    EXPECT_NE(res.result["error"].get<std::string>().find(needle),
              std::string::npos);
  };
  expect_error("vector.svg", "Unsupported image format");
  expect_error("bitmap.bmp", "Unsupported image format");
  expect_error("big.png", "Downscale");
  expect_error("missing.png", "File not found");
  expect_error("", "path");
}

// Anthropic: text tool_result first, then the image block in the same turn.
TEST(ImageToolTest, AnthropicSendsImageAfterToolResult) {
  anthropic::AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-sonnet-5-5";
  options.messages = image_tool_history();

  const auto req = builder.build_request_json(options);
  ASSERT_EQ(req["messages"].size(), 3u) << req["messages"].dump(2);
  const auto& turn = req["messages"][2];
  EXPECT_EQ(turn["role"], "user");
  ASSERT_EQ(turn["content"].size(), 2u);
  const auto& block = turn["content"][0];
  EXPECT_EQ(block["type"], "tool_result");
  EXPECT_EQ(block["tool_use_id"], "call_1");
  EXPECT_FALSE(block.contains("is_error"));
  ASSERT_TRUE(block["content"].is_string());
  const auto text = block["content"].get<std::string>();
  EXPECT_NE(text.find("Loaded image shot.png"), std::string::npos);
  EXPECT_EQ(text.find(kB64), std::string::npos);
  const auto& image = turn["content"][1];
  EXPECT_EQ(image["type"], "image");
  EXPECT_EQ(image["source"]["type"], "base64");
  EXPECT_EQ(image["source"]["media_type"], "image/png");
  EXPECT_EQ(image["source"]["data"], kB64);
}

// OpenAI tool messages are text-only: the image follows as a user message.
TEST(ImageToolTest, OpenAIChatSendsImageAsUserMessage) {
  openai::OpenAIRequestBuilder builder(/*use_responses=*/false);
  GenerateOptions options;
  options.model = "gpt-4o";
  options.messages = image_tool_history();

  const auto req = builder.build_request_json(options);
  const auto& msgs = req["messages"];
  ASSERT_EQ(msgs.size(), 4u) << msgs.dump(2);
  EXPECT_EQ(msgs[2]["role"], "tool");
  EXPECT_EQ(msgs[2]["tool_call_id"], "call_1");
  ASSERT_TRUE(msgs[2]["content"].is_string());
  const auto tool_text = msgs[2]["content"].get<std::string>();
  EXPECT_NE(tool_text.find("Loaded image shot.png"), std::string::npos);
  EXPECT_EQ(tool_text.find(kB64), std::string::npos);

  EXPECT_EQ(msgs[3]["role"], "user");
  ASSERT_EQ(msgs[3]["content"].size(), 1u);
  EXPECT_EQ(msgs[3]["content"][0]["type"], "image_url");
  EXPECT_EQ(msgs[3]["content"][0]["image_url"]["url"],
            std::string("data:image/png;base64,") + kB64);
}

TEST(ImageToolTest, OpenAIResponsesKeepsBase64OutOfFunctionOutput) {
  openai::OpenAIRequestBuilder builder(/*use_responses=*/true);
  GenerateOptions options;
  options.model = "gpt-5";
  options.messages = image_tool_history();

  const auto req = builder.build_request_json(options);
  const auto& input = req["input"];
  bool saw_output = false;
  for (const auto& item : input) {
    if (item.value("type", "") != "function_call_output") continue;
    saw_output = true;
    const auto output = item["output"].get<std::string>();
    EXPECT_NE(output.find("Loaded image shot.png"), std::string::npos);
    EXPECT_EQ(output.find(kB64), std::string::npos);
  }
  EXPECT_TRUE(saw_output) << input.dump(2);

  const auto& last = input.back();
  EXPECT_EQ(last["role"], "user");
  EXPECT_EQ(last["content"][0]["type"], "input_image");
  EXPECT_EQ(last["content"][0]["image_url"],
            std::string("data:image/png;base64,") + kB64);
}

// Gemini/Antigravity: OpenAI-shaped request -> inlineData next to the
// functionResponse it belongs to.
TEST(ImageToolTest, GeminiKeepsImageWithFunctionResponse) {
  openai::OpenAIRequestBuilder builder;
  GenerateOptions options;
  options.model = "gemini-3-pro";
  options.messages = image_tool_history();

  const auto gem =
      gemini::convert_openai_to_gemini(builder.build_request_json(options));
  const auto& contents = gem["contents"];
  ASSERT_EQ(contents.size(), 3u) << contents.dump(2);
  const auto& parts = contents[2]["parts"];
  EXPECT_EQ(contents[2]["role"], "user");
  ASSERT_EQ(parts.size(), 2u);
  EXPECT_EQ(parts[0]["functionResponse"]["id"], "call_1");
  EXPECT_TRUE(parts[0]["functionResponse"]["response"].is_object());
  EXPECT_EQ(parts[1]["inlineData"]["mimeType"], "image/png");
  EXPECT_EQ(parts[1]["inlineData"]["data"], kB64);
}

TEST(ImageToolTest, CursorPromptHasNoBase64) {
  cursor::CursorRequestBuilder builder;
  GenerateOptions options;
  options.model = "composer-2.5";
  options.messages = image_tool_history();

  const std::string payload = builder.build_agent_run_request(options);
  EXPECT_EQ(payload.find(kB64), std::string::npos);
  EXPECT_NE(payload.find("Loaded image shot.png"), std::string::npos);
  EXPECT_NE(payload.find("[image attachment: image/png"), std::string::npos);
}

// OpenAI needs every tool message right after the assistant tool_calls, and
// Anthropic needs tool_result blocks first: images wait for the whole run.
TEST(ImageToolTest, ImagesFollowTheWholeRunOfToolResults) {
  {
    openai::OpenAIRequestBuilder builder;
    GenerateOptions options;
    options.model = "gpt-4o";
    options.messages = parallel_history();
    const auto msgs = builder.build_request_json(options)["messages"];
    ASSERT_EQ(msgs.size(), 5u) << msgs.dump(2);
    EXPECT_EQ(msgs[2]["role"], "tool");
    EXPECT_EQ(msgs[2]["tool_call_id"], "call_1");
    EXPECT_EQ(msgs[3]["role"], "tool");
    EXPECT_EQ(msgs[3]["tool_call_id"], "call_2");
    EXPECT_EQ(msgs[4]["role"], "user");
    EXPECT_EQ(msgs[4]["content"][0]["type"], "image_url");
  }
  {
    anthropic::AnthropicRequestBuilder builder;
    GenerateOptions options;
    options.model = "claude-sonnet-5-5";
    options.messages = parallel_history();
    const auto msgs = builder.build_request_json(options)["messages"];
    ASSERT_EQ(msgs.size(), 3u) << msgs.dump(2);
    const auto& content = msgs[2]["content"];
    ASSERT_EQ(content.size(), 3u);
    EXPECT_EQ(content[0]["tool_use_id"], "call_1");
    EXPECT_EQ(content[1]["tool_use_id"], "call_2");
    EXPECT_EQ(content[2]["type"], "image");
  }
}

// Stored history keeps the raw result (persistence, pruning, UI); only the
// request copy is lifted.
TEST(ImageToolTest, LiftLeavesTheHistoryUntouched) {
  const Messages history = image_tool_history();
  const auto lifted = ProviderTransform::lift_tool_result_images(history);
  ASSERT_EQ(lifted.size(), 4u);
  EXPECT_TRUE(ImageTool::is_image_result(history[2].get_tool_results()[0]));
  const auto results = lifted[2].get_tool_results();
  ASSERT_EQ(results.size(), 1u);
  EXPECT_TRUE(results[0].result.is_string());
  const auto images = lifted[3].get_images();
  ASSERT_EQ(images.size(), 1u);
  EXPECT_EQ(images[0].mime_type, "image/png");
  EXPECT_EQ(images[0].data, kB64);
  EXPECT_EQ(lifted[3].role, kMessageRoleUser);
}

// Old sessions may hold svg/bmp results; sending them would 400 every turn.
TEST(ImageToolTest, UnsupportedStoredFormatIsDescribedNotSent) {
  openai::OpenAIRequestBuilder builder;
  GenerateOptions options;
  options.model = "gpt-4o";
  options.messages = image_tool_history("image/svg+xml");
  const auto msgs = builder.build_request_json(options)["messages"];
  ASSERT_EQ(msgs.size(), 3u) << msgs.dump(2);
  const auto text = msgs[2]["content"].get<std::string>();
  EXPECT_NE(text.find("cannot be shown"), std::string::npos);
  EXPECT_EQ(text.find(kB64), std::string::npos);
}

TEST_F(ImageToolFileTest, ErrorMessageReachesTheModel) {
  const auto res = run("missing.png");
  ASSERT_FALSE(res.is_success());
  std::vector<ToolCallContentPart> calls;
  calls.emplace_back("call_img", "image", JsonValue{{"path", "missing.png"}});
  GenerateOptions options;
  options.model = "gpt-4o";
  options.messages = {
      Message::user("look"), Message::assistant_with_tools("", calls),
      Message::tool_results({{res.tool_call_id, res.result, true}})};
  openai::OpenAIRequestBuilder builder;
  const auto msgs = builder.build_request_json(options)["messages"];
  ASSERT_EQ(msgs.size(), 3u) << msgs.dump(2);
  EXPECT_NE(msgs[2]["content"].get<std::string>().find("File not found"),
            std::string::npos);
}

TEST(ImageToolTest, TokenEstimateCountsImagesFlat) {
  const std::string huge(4'000'000, 'A');  // ~3 MB image as base64
  std::vector<ToolResultContentPart> results;
  results.emplace_back("c1",
                       JsonValue{{"path", "big.png"},
                                 {"mime_type", "image/png"},
                                 {"size_bytes", 3'000'000},
                                 {"data", huge}},
                       false);
  const size_t tool_tokens = estimate_tokens({Message::tool_results(results)});
  EXPECT_GE(tool_tokens, 1600u);
  EXPECT_LT(tool_tokens, 2000u);

  const size_t attachment_tokens = estimate_tokens(
      {Message::user_with_images("", {ImageContentPart{huge, "image/png"}})});
  EXPECT_GE(attachment_tokens, 1600u);
  EXPECT_LT(attachment_tokens, 2000u);
}

// ── Pixel dimensions, read from the header without decoding ────────────────

namespace {

// 24-byte png prefix: signature, an IHDR chunk header, then width/height.
// Enough for both detect_mime_type and dimensions().
// The size fields are not constrained to int, so a test can write values
// that would wrap negative if narrowed carelessly.
std::string png_with_raw_size(std::uint32_t width, std::uint32_t height) {
  std::string out("\x89PNG\r\n\x1a\n", 8);
  // IHDR chunk length 13. Spelled out byte by byte: a "\x00..." literal
  // would be an empty C string.
  for (int i = 0; i < 3; ++i) out.push_back('\0');
  out.push_back('\x0D');
  out += "IHDR";
  const auto be = [&out](std::uint32_t v) {
    for (int i = 3; i >= 0; --i)
      out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
  };
  be(width);
  be(height);
  return out;
}

std::string png_with_size(int width, int height) {
  return png_with_raw_size(static_cast<std::uint32_t>(width),
                           static_cast<std::uint32_t>(height));
}

// APP0/JFIF segment then a baseline SOF0 frame header.
std::string jpeg_with_size(int width, int height) {
  std::string out("\xFF\xD8", 2);
  out += "\xFF\xE0";
  out += '\0';
  out += '\x10';  // APP0 length = 16
  out += "JFIF";
  out.append(10, '\0');  // APP0 payload is 14 bytes (length includes itself)
  out += "\xFF\xC0";
  out += '\0';
  out += '\x11';  // SOF0 length = 17
  out.push_back('\x08');
  const auto put = [&out](int v) {
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>(v & 0xFF));
  };
  put(height);
  put(width);
  out.append(6, '\0');
  return out;
}

std::string gif_with_size(int width, int height) {
  std::string out("GIF89a", 6);
  out.push_back(static_cast<char>(width & 0xFF));
  out.push_back(static_cast<char>((width >> 8) & 0xFF));
  out.push_back(static_cast<char>(height & 0xFF));
  out.push_back(static_cast<char>((height >> 8) & 0xFF));
  return out;
}

std::string webp_vp8x(int width, int height) {
  std::string out("RIFF", 4);
  out.append(4, '\0');
  out += "WEBPVP8X";
  out.append(8, '\0');  // chunk size + flags/reserved
  const auto le24 = [&out](std::uint32_t v) {
    for (int i = 0; i < 3; ++i) out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
  };
  le24(static_cast<std::uint32_t>(width) - 1);
  le24(static_cast<std::uint32_t>(height) - 1);
  return out;
}

std::string webp_vp8(int width, int height) {
  std::string out("RIFF", 4);
  out.append(4, '\0');
  out += "WEBPVP8 ";
  out.append(4, '\0');
  out.append(3, '\0');   // frame tag
  out += "\x9D\x01\x2A";  // sync code
  const auto put = [&out](int v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
  };
  put(width);
  put(height);
  return out;
}

std::string webp_vp8l(int width, int height) {
  std::string out("RIFF", 4);
  out.append(4, '\0');
  out += "WEBPVP8L";
  out.append(4, '\0');
  out.push_back('\x2F');  // VP8L signature byte
  const auto bits = [width, height](std::string& s) {
    const unsigned w = static_cast<unsigned>(width) - 1;
    const unsigned h = static_cast<unsigned>(height) - 1;
    s.push_back(static_cast<char>(w & 0xFF));
    s.push_back(static_cast<char>((w >> 8) | ((h & 0x03) << 6)));
    s.push_back(static_cast<char>((h >> 2) & 0xFF));
    s.push_back(static_cast<char>(((h >> 10) & 0x0F) << 4));
  };
  bits(out);
  return out;
}

}  // namespace

TEST(ImageToolTest, ReadsDimensionsFromEveryHeaderItAccepts) {
  const auto expect = [](const std::string& bytes, int width, int height) {
    const auto dim = ImageTool::dimensions(bytes);
    ASSERT_TRUE(dim.has_value()) << ImageTool::detect_mime_type(bytes);
    EXPECT_EQ(dim->width, width);
    EXPECT_EQ(dim->height, height);
  };
  expect(png_bytes(), 1, 1);
  expect(png_with_size(1920, 1080), 1920, 1080);
  expect(gif_with_size(640, 480), 640, 480);
  expect(jpeg_with_size(800, 600), 800, 600);
  expect(webp_vp8x(1024, 768), 1024, 768);
  expect(webp_vp8(320, 240), 320, 240);
  expect(webp_vp8l(64, 32), 64, 32);
  EXPECT_EQ(ImageTool::dimensions(png_bytes()).value().pixels(), 1);
  EXPECT_EQ(ImageTool::dimensions(png_with_size(4000, 3000)).value().pixels(),
            12'000'000);
}

// A header claiming a zero side is nonsense: it must neither trip the limit
// check nor print "0x0" to the model.
TEST_F(ImageToolFileTest, ZeroSizedHeaderIsIgnoredNotRefused) {
  write("zero.png", png_with_size(0, 0));
  const auto res = run("zero.png");
  ASSERT_TRUE(res.is_success()) << res.error_message();
  EXPECT_FALSE(res.result.contains("width"));
  EXPECT_FALSE(res.result.contains("height"));
  EXPECT_EQ(ImageTool::summary(res.result).find("0x0"), std::string::npos);
}

// Truncated or foreign data must not read past the end, and an unsupported
// container has no dimensions to report.
TEST(ImageToolTest, DimensionsAreAbsentForShortOrUnknownData) {
  EXPECT_FALSE(ImageTool::dimensions("").has_value());
  EXPECT_FALSE(ImageTool::dimensions("hello").has_value());
  EXPECT_FALSE(ImageTool::dimensions("<svg width=\"1\" height=\"1\"></svg>").has_value());
  // Correct magic bytes, header truncated mid-dimensions.
  EXPECT_FALSE(ImageTool::dimensions(std::string("\x89PNG\r\n\x1a\n", 8)).has_value());
  EXPECT_FALSE(
      ImageTool::dimensions(std::string("\x89PNG\r\n\x1a\n", 8) + std::string(10, '\0'))
          .has_value());
  EXPECT_FALSE(ImageTool::dimensions("GIF89a\x01").has_value());
  EXPECT_FALSE(
      ImageTool::dimensions(std::string("RIFF\x00\x00\x00\x00WEBP", 12)).has_value());
  // A jpeg whose SOF never arrives (scan data only).
  EXPECT_FALSE(ImageTool::dimensions(std::string("\xFF\xD8\xFF\xDA", 4)).has_value());
}

// The byte limit cannot catch an image that is small on disk but enormous in
// pixels; Anthropic rejects anything over 8000px per side, so the tool must.
TEST_F(ImageToolFileTest, RefusesImagesProvidersWouldReject) {
  write("huge.png", png_with_size(9000, 6000));
  write("tall.webp", webp_vp8x(400, 12000));
  write("fine.png", png_with_size(1568, 1568));
  // A crafted header claiming 0xFFFFFFFF px. Narrowed to int this wraps
  // negative, which would pass a naive "> 8000" check.
  write("wrapped.png", png_with_raw_size(0xFFFFFFFFu, 0xFFFFFFFFu));

  const auto expect_error = [&](const std::string& path,
                                const std::string& needle) {
    const auto res = run(path);
    EXPECT_FALSE(res.is_success()) << path;
    EXPECT_NE(res.error_message().find(needle), std::string::npos)
        << res.error_message();
    ASSERT_TRUE(res.result.contains("error")) << res.result.dump();
    EXPECT_NE(res.result["error"].get<std::string>().find(needle),
              std::string::npos);
  };
  // Names the real size, the limit, and the exact command to run.
  expect_error("huge.png", "9000x6000");
  expect_error("huge.png", "8000px per side");
  expect_error("huge.png", "Downscale");
  expect_error("tall.webp", "400x12000");
  expect_error("wrapped.png", "the limit is 8000px per side");

  const auto ok = run("fine.png");
  ASSERT_TRUE(ok.is_success()) << ok.error_message();
  EXPECT_EQ(ok.result["width"], 1568);
  EXPECT_EQ(ok.result["height"], 1568);
}

// Results stored before this build have no width/height; the summary must
// still render rather than print "0x0".
TEST(ImageToolTest, SummaryOmitsDimensionsForOlderStoredResults) {
  const std::string legacy = ImageTool::summary(image_result());
  EXPECT_NE(legacy.find("Loaded image shot.png (image/png, 32 bytes)"),
            std::string::npos)
      << legacy;
  JsonValue with_dims = image_result();
  with_dims["width"] = 1920;
  with_dims["height"] = 1080;
  EXPECT_NE(ImageTool::summary(with_dims).find("1920x1080"), std::string::npos);
}

// ── Animated gif and EXIF orientation fixtures ────────────────────────────

namespace {

// A structurally valid gif with `frames` image descriptors. The colour table
// flags are settable because skipping them is exactly what a naive walker
// gets wrong: the global one lives in the screen descriptor, the local one
// inside each frame, and both sit before the image data.
std::string gif_with_frames(int frames, bool global_table, bool local_table) {
  std::string out("GIF89a", 6);
  out.push_back('\x01');
  out.push_back('\0');  // width 1
  out.push_back('\x01');
  out.push_back('\0');  // height 1
  out.push_back(static_cast<char>(global_table ? 0x80 : 0x00));
  out.push_back('\0');  // background colour index
  out.push_back('\0');  // pixel aspect ratio
  if (global_table) out.append(6, '\x01');  // 2 entries x 3 bytes
  for (int f = 0; f < frames; ++f) {
    out += "\x21\xF9\x04";  // graphic control extension
    out.append(4, '\0');
    out.push_back('\0');  // extension terminator
    out.push_back('\x2C');  // image descriptor
    out.append(4, '\0');  // left, top
    out.push_back('\x01');
    out.push_back('\0');  // width
    out.push_back('\x01');
    out.push_back('\0');  // height
    out.push_back(static_cast<char>(local_table ? 0x80 : 0x00));
    if (local_table) out.append(6, '\x01');
    out.push_back('\x02');   // LZW minimum code size
    out.push_back('\x02');   // one sub-block of 2 bytes
    out.append(2, '\x44');
    out.push_back('\0');     // block terminator
  }
  out.push_back('\x3B');  // trailer
  return out;
}

// APP0/JFIF, then an APP1 Exif block holding one IFD entry: Orientation.
std::string jpeg_with_exif_orientation(int orientation, bool little_endian) {
  std::string out("\xFF\xD8", 2);
  out += "\xFF\xE0";
  out.push_back('\0');
  out.push_back('\x10');  // APP0 length 16
  out += "JFIF";
  out.append(10, '\0');

  std::string tiff;
  const auto u16 = [&tiff, little_endian](int v) {
    if (little_endian) {
      tiff.push_back(static_cast<char>(v & 0xFF));
      tiff.push_back(static_cast<char>((v >> 8) & 0xFF));
    } else {
      tiff.push_back(static_cast<char>((v >> 8) & 0xFF));
      tiff.push_back(static_cast<char>(v & 0xFF));
    }
  };
  // Big-endian writes the high half first; sharing u16 would reverse it.
  const auto u32 = [&tiff, little_endian](int v) {
    const unsigned u = static_cast<unsigned>(v);
    for (int i = 0; i < 4; ++i) {
      const int shift = little_endian ? i * 8 : (3 - i) * 8;
      tiff.push_back(static_cast<char>((u >> shift) & 0xFF));
    }
  };
  tiff += little_endian ? "II" : "MM";
  u16(42);
  u32(8);  // IFD0 follows the 8-byte header
  u16(1);  // one entry
  u16(0x0112);  // Orientation
  u16(3);       // SHORT
  u32(1);       // count
  u16(orientation);
  u16(0);  // value padding
  u32(0);  // no next IFD

  out += "\xFF\xE1";
  const int payload = static_cast<int>(tiff.size()) + 6;  // + "Exif\0\0"
  const int segment = payload + 2;  // length field counts itself
  out.push_back(static_cast<char>((segment >> 8) & 0xFF));
  out.push_back(static_cast<char>(segment & 0xFF));
  out += "Exif";
  out.append(2, '\0');
  out += tiff;
  return out;
}

}  // namespace

// Providers decode frame 1 only, so an animation is megabytes of bytes that
// will never be seen. The model has to be told, or it will describe motion
// it cannot see.
TEST_F(ImageToolFileTest, AnimatedGifIsFlaggedStillGifIsNot) {
  write("still.gif", gif_with_frames(1, false, false));
  write("two.gif", gif_with_frames(2, false, false));
  // Both colour-table placements, which a naive block walker skips.
  write("gct.gif", gif_with_frames(3, true, false));
  write("lct.gif", gif_with_frames(3, false, true));
  write("both.gif", gif_with_frames(2, true, true));

  const auto still = run("still.gif");
  ASSERT_TRUE(still.is_success()) << still.error_message();
  EXPECT_EQ(still.result.value("mime_type", std::string()), "image/gif");
  EXPECT_FALSE(still.result.contains("animated"));
  EXPECT_EQ(ImageTool::summary(still.result).find("first frame"),
            std::string::npos);

  for (const char* name : {"two.gif", "gct.gif", "lct.gif", "both.gif"}) {
    const auto res = run(name);
    ASSERT_TRUE(res.is_success()) << name << ": " << res.error_message();
    EXPECT_TRUE(res.result.value("animated", false)) << name;
    EXPECT_NE(ImageTool::summary(res.result).find("first frame"),
              std::string::npos)
        << name;
  }
}

// A phone photo is stored sideways with an EXIF tag saying how to turn it.
// The provider does not apply the tag, so the raw pixels are all it sees.
TEST_F(ImageToolFileTest, ExifOrientationIsReported) {
  for (int orientation = 1; orientation <= 8; ++orientation) {
    for (const bool little : {true, false}) {
      const std::string bytes =
          jpeg_with_exif_orientation(orientation, little) +
          std::string(64, '\0');
      write("photo.jpg", bytes);
      const auto res = run("photo.jpg");
      ASSERT_TRUE(res.is_success()) << res.error_message();
      ASSERT_EQ(res.result.value("mime_type", std::string()), "image/jpeg");
      if (orientation == 1) {
        EXPECT_FALSE(res.result.contains("orientation"))
            << "upright jpeg should carry no orientation";
        continue;
      }
      EXPECT_EQ(res.result.value("orientation", 0), orientation)
          << "orientation " << orientation << " little=" << little;
      EXPECT_NE(ImageTool::summary(res.result).find("sideways"), std::string::npos);
    }
  }
  // A jpeg with no Exif block at all must not invent an orientation.
  write("plain.jpg", jpeg_with_size(800, 600) + std::string(64, '\0'));
  const auto plain = run("plain.jpg");
  ASSERT_TRUE(plain.is_success()) << plain.error_message();
  EXPECT_FALSE(plain.result.contains("orientation"));
  EXPECT_EQ(ImageTool::summary(plain.result).find("sideways"), std::string::npos);
}

// ── Honest errors ─────────────────────────────────────────────────────────

// "File not found" for a directory sends the model hunting for a file that is
// already there. Name the actual problem.
TEST_F(ImageToolFileTest, DirectoriesAndUnreadableFilesSayWhatIsWrong) {
  std::filesystem::create_directories(dir_ / "shots");
  write("empty.png", "");
  const auto expect_error = [&](const std::string& path,
                                const std::string& needle) {
    const auto res = run(path);
    EXPECT_FALSE(res.is_success()) << path;
    EXPECT_NE(res.error_message().find(needle), std::string::npos)
        << res.error_message();
  };
  expect_error("shots", "is a directory");
  expect_error("empty.png", "is empty");

  // Permission denied. Skipped as root, which reads anything.
  if (::geteuid() != 0) {
    const auto locked = dir_ / "locked.png";
    write("locked.png", png_bytes());
    std::filesystem::permissions(locked, std::filesystem::perms::none);
    expect_error("locked.png", "Could not read");
    std::filesystem::permissions(locked, std::filesystem::perms::owner_all);
  }
}

// ── Duplicate images are uploaded once per request ─────────────────────────

namespace {

// Two turns that loaded the same file: the tool ran again on a path the model
// already has, or the session was retried.
Messages duplicated_image_history(const std::string& second_data) {
  std::vector<ToolCallContentPart> calls;
  calls.emplace_back("call_1", "image", JsonValue{{"path", "shot.png"}});
  std::vector<ToolCallContentPart> calls2;
  calls2.emplace_back("call_2", "image", JsonValue{{"path", "shot2.png"}});
  JsonValue second = image_result();
  second["path"] = "shot2.png";
  second["data"] = second_data;
  return {Message::user("look at the screenshot"),
          Message::assistant_with_tools("", calls),
          Message::tool_results({{"call_1", image_result(), false}}),
          Message::assistant_with_tools("", calls2),
          Message::tool_results({{"call_2", second, false}})};
}

size_t count_lifted_images(const Messages& lifted) {
  size_t n = 0;
  for (const auto& msg : lifted) n += msg.get_images().size();
  return n;
}

// The lift inserts a user image message between turns, so positions shift
// with the dedupe. Find tool results by shape instead of by index.
std::vector<std::string> tool_result_texts(const Messages& messages) {
  std::vector<std::string> out;
  for (const auto& msg : messages) {
    if (!msg.has_tool_results()) continue;
    for (const auto& tr : msg.get_tool_results()) {
      out.push_back(tr.result.is_string() ? tr.result.get<std::string>()
                                          : tr.result.dump());
    }
  }
  return out;
}

size_t index_of_first_tool_results(const Messages& messages) {
  for (size_t i = 0; i < messages.size(); ++i) {
    if (messages[i].has_tool_results()) return i;
  }
  return messages.size();
}

}  // namespace

TEST(ImageToolTest, ByteIdenticalImagesAreSentOnce) {
  // Same payload under a different filename is still the same image.
  const Messages lifted =
      ProviderTransform::lift_tool_result_images(duplicated_image_history(kB64));
  EXPECT_EQ(count_lifted_images(lifted), 1u);
  const auto texts = tool_result_texts(lifted);
  ASSERT_EQ(texts.size(), 2u);
  // The first still carries pixels; the repeat says so in words instead.
  EXPECT_NE(texts[0].find("the image follows"), std::string::npos) << texts[0];
  EXPECT_NE(texts[1].find("already sent"), std::string::npos) << texts[1];
  // The repeat is still identified, so the model knows which file it was.
  EXPECT_NE(texts[1].find("shot2.png"), std::string::npos) << texts[1];
}

// Two genuinely different images must both reach the provider.
TEST(ImageToolTest, DifferentImagesAreBothSent) {
  const std::string other =
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAEElEQVR4nGP8//8/AzJgYkAD"  ;
  const Messages lifted =
      ProviderTransform::lift_tool_result_images(duplicated_image_history(other));
  EXPECT_EQ(count_lifted_images(lifted), 2u);
  const auto texts = tool_result_texts(lifted);
  ASSERT_EQ(texts.size(), 2u);
  EXPECT_EQ(texts[1].find("already sent"), std::string::npos) << texts[1];
}

// Same bytes, two different mime types: still two images, since a provider
// decodes them differently.
TEST(ImageToolTest, DedupeDoesNotCollapseDifferentTypes) {
  Messages history = image_tool_history();
  JsonValue second = image_result("image/jpeg");
  second["data"] = kB64;
  history.push_back(Message::assistant_with_tools(
      "", {ToolCallContentPart("call_2", "image", JsonValue{{"path", "s.jpg"}})}));
  history.push_back(
      Message::tool_results({{"call_2", second, false}}));
  const Messages lifted = ProviderTransform::lift_tool_result_images(history);
  EXPECT_EQ(count_lifted_images(lifted), 2u);
  const auto texts = tool_result_texts(lifted);
  ASSERT_EQ(texts.size(), 2u);
  EXPECT_EQ(texts[1].find("already sent"), std::string::npos) << texts[1];
}

// The dedupe is per request: the stored history keeps every full payload, so
// nothing is lost for the UI, persistence or a later turn.
TEST(ImageToolTest, DedupeDoesNotMutateStoredResults) {
  const Messages history = duplicated_image_history(kB64);
  const Messages lifted = ProviderTransform::lift_tool_result_images(history);

  const size_t first = index_of_first_tool_results(history);
  ASSERT_LT(first, history.size());
  const auto stored = history[first].get_tool_results();
  ASSERT_EQ(stored.size(), 1u);
  EXPECT_TRUE(ImageTool::is_image_result(stored[0]));
  EXPECT_EQ(stored[0].result.at("data").get<std::string>(), kB64);

  const auto texts = tool_result_texts(lifted);
  ASSERT_FALSE(texts.empty());
  // Only the request copy is rewritten.
  EXPECT_NE(texts[0].find("Loaded image"), std::string::npos) << texts[0];
}


TEST(ImageToolTest, LiveOpenRouterVisionWithImageToolResult) {
  const char* api_key_env = std::getenv("OPENROUTER_API_KEY");
  if (!api_key_env || std::string(api_key_env).empty()) {
    GTEST_SKIP() << "OPENROUTER_API_KEY not set; skipping live vision test";
  }

  const auto temp_dir = std::filesystem::temp_directory_path();
  std::ofstream(temp_dir / "qcode_live_red.png", std::ios::binary) << png_bytes();
  ToolExecutionContext ctx;
  ctx.workspace = temp_dir.string();
  auto tool_output = ImageTool::execute(JsonValue{{"path", "qcode_live_red.png"}}, ctx);
  std::filesystem::remove(temp_dir / "qcode_live_red.png");

  GenerateOptions opts;
  opts.model = "dots-studio/dots-3-note-preview:free";
  opts.max_tokens = 512;
  opts.messages.push_back(Message::user("What color is the pixel in the image? Answer concisely."));
  std::vector<ToolCallContentPart> calls;
  calls.emplace_back("call_img_live", "image", JsonValue{{"path", "qcode_live_red.png"}});
  opts.messages.push_back(Message::assistant_with_tools("", calls));
  std::vector<ToolResultContentPart> results;
  results.emplace_back("call_img_live", tool_output, false);
  opts.messages.push_back(Message::tool_results(results));

  std::map<std::string, std::string> headers{
      {"HTTP-Referer", "https://qcode.ai"},
      {"X-Title", "qcode"}
  };
  openai::OpenAIClient client(api_key_env, "https://openrouter.ai/api/v1", /*use_responses=*/false, headers);

  auto res = client.generate_text(opts);
  EXPECT_TRUE(res.is_success()) << "OpenRouter generation failed: " << res.error_message();
  std::string text_lower = res.text;
  for (char& c : text_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  EXPECT_NE(text_lower.find("red"), std::string::npos) << "Response text was: " << res.text;
}

}  // namespace
}  // namespace qcode
