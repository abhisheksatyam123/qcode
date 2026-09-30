#include <qcode/tools/image_tool.h>
#include <qcode/tools/tool_catalog.h>
#include <qcode/tools/tool_executor.h>
#include <qcode/providers/anthropic.h>
#include <qcode/providers/openai.h>
#include <qcode/transform/gemini_transform.h>
#include <providers/anthropic/anthropic_request_builder.h>
#include <providers/openai/openai_request_builder.h>
#include <providers/openai/openai_client.h>

#include <gtest/gtest.h>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

TEST(ImageToolTest, DefinitionAndSchema) {
  Tool tool = ImageTool::definition();
  EXPECT_EQ(tool.name, "image");
  EXPECT_TRUE(tool.has_execute());
  EXPECT_TRUE(tool.parameters_schema.contains("properties"));
  EXPECT_TRUE(tool.parameters_schema["properties"].contains("path"));
  EXPECT_TRUE(tool.parameters_schema["properties"].contains("detail"));
  EXPECT_TRUE(tool.parameters_schema["properties"].contains("description"));
  EXPECT_TRUE(tool.parameters_schema.contains("required"));
  EXPECT_EQ(tool.parameters_schema["required"][0], "path");
}

TEST(ImageToolTest, Base64EncodeAndDecodeMatch) {
  EXPECT_EQ(ImageTool::base64_encode(""), "");
  EXPECT_EQ(ImageTool::base64_encode("f"), "Zg==");
  EXPECT_EQ(ImageTool::base64_encode("fo"), "Zm8=");
  EXPECT_EQ(ImageTool::base64_encode("foo"), "Zm9v");
  EXPECT_EQ(ImageTool::base64_encode("hello world"), "aGVsbG8gd29ybGQ=");

  std::string binary_data = "\x00\x01\x02\xFF\xFE\xFD\x89PNG\r\n\x1a\n";
  std::string encoded = ImageTool::base64_encode(binary_data);
  std::string decoded = decode_base64_for_test(encoded);
  EXPECT_EQ(binary_data, decoded);
}

TEST(ImageToolTest, DetectMimeType) {
  // PNG magic bytes: \x89PNG\r\n\x1a\n
  std::string png_data = "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR";
  EXPECT_EQ(ImageTool::detect_mime_type("test.unknown", png_data), "image/png");

  // JPEG magic bytes: \xFF\xD8\xFF
  std::string jpeg_data = "\xFF\xD8\xFF\xE0\x00\x10JFIF";
  EXPECT_EQ(ImageTool::detect_mime_type("test.unknown", jpeg_data), "image/jpeg");

  // GIF magic bytes: GIF89a
  std::string gif_data = "GIF89a\x01\x00\x01\x00";
  EXPECT_EQ(ImageTool::detect_mime_type("test.unknown", gif_data), "image/gif");

  // WebP: RIFF....WEBP
  std::string webp_data = std::string("RIFF\x00\x00\x00\x00WEBPVP8 ", 16);
  EXPECT_EQ(ImageTool::detect_mime_type("test.unknown", webp_data), "image/webp");

  // BMP: BM
  std::string bmp_data = "BM\x36\x00\x00\x00";
  EXPECT_EQ(ImageTool::detect_mime_type("test.unknown", bmp_data), "image/bmp");

  // SVG: <svg
  std::string svg_data = "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>";
  EXPECT_EQ(ImageTool::detect_mime_type("test.unknown", svg_data), "image/svg+xml");

  // Extension fallbacks
  EXPECT_EQ(ImageTool::detect_mime_type("photo.png", ""), "image/png");
  EXPECT_EQ(ImageTool::detect_mime_type("photo.jpg", ""), "image/jpeg");
  EXPECT_EQ(ImageTool::detect_mime_type("photo.webp", ""), "image/webp");
  EXPECT_EQ(ImageTool::detect_mime_type("doc.txt", "hello"), "");
}

TEST(ImageToolTest, MissingOrEmptyPath) {
  ToolExecutionContext ctx;
  auto res1 = ImageTool::execute(nlohmann::json::object(), ctx);
  EXPECT_TRUE(res1.contains("error"));

  auto res2 = ImageTool::execute(nlohmann::json{{"path", ""}}, ctx);
  EXPECT_TRUE(res2.contains("error"));
}

TEST(ImageToolTest, NonExistentFile) {
  ToolExecutionContext ctx;
  ctx.workspace = "/tmp";
  auto res = ImageTool::execute(nlohmann::json{{"path", "non_existent_image_12345.png"}}, ctx);
  EXPECT_TRUE(res.contains("error"));
  EXPECT_NE(res["error"].get<std::string>().find("File not found"), std::string::npos);
}

TEST(ImageToolTest, ImageInputOutputVariousFormats) {
  const auto temp_dir = std::filesystem::temp_directory_path();

  // 1. PNG test
  const unsigned char kPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
      0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
      0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
      0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  auto png_path = temp_dir / "test_format.png";
  {
    std::ofstream out(png_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(kPng), sizeof(kPng));
  }

  // 2. JPEG test
  const unsigned char kJpeg[] = {
      0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01,
      0x01, 0x01, 0x00, 0x48, 0x00, 0x48, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43,
      0x00, 0xFF, 0xD9};
  auto jpeg_path = temp_dir / "test_format.jpg";
  {
    std::ofstream out(jpeg_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(kJpeg), sizeof(kJpeg));
  }

  // 3. SVG test
  std::string svg_content = "<svg width=\"100\" height=\"100\"><circle cx=\"50\" cy=\"50\" r=\"40\" fill=\"red\"/></svg>";
  auto svg_path = temp_dir / "test_format.svg";
  {
    std::ofstream out(svg_path, std::ios::binary);
    out.write(svg_content.data(), svg_content.size());
  }

  ToolExecutionContext ctx;
  ctx.workspace = temp_dir.string();

  // Test PNG execution
  {
    auto res = ImageTool::execute(JsonValue{{"path", "test_format.png"}, {"detail", "high"}}, ctx);
    EXPECT_TRUE(res.value("success", false));
    EXPECT_EQ(res.value("mime_type", ""), "image/png");
    EXPECT_EQ(res.value("size_bytes", 0), static_cast<int>(sizeof(kPng)));
    EXPECT_EQ(res.value("detail", ""), "high");
    std::string decoded = decode_base64_for_test(res.value("data", ""));
    EXPECT_EQ(decoded.size(), sizeof(kPng));
  }

  // Test JPEG execution
  {
    auto res = ImageTool::execute(JsonValue{{"path", "test_format.jpg"}, {"description", "A photo"}}, ctx);
    EXPECT_TRUE(res.value("success", false));
    EXPECT_EQ(res.value("mime_type", ""), "image/jpeg");
    EXPECT_EQ(res.value("size_bytes", 0), static_cast<int>(sizeof(kJpeg)));
    EXPECT_EQ(res.value("description", ""), "A photo");
    std::string decoded = decode_base64_for_test(res.value("data", ""));
    EXPECT_EQ(decoded.size(), sizeof(kJpeg));
  }

  // Test SVG execution
  {
    auto res = ImageTool::execute(JsonValue{{"path", "test_format.svg"}}, ctx);
    EXPECT_TRUE(res.value("success", false));
    EXPECT_EQ(res.value("mime_type", ""), "image/svg+xml");
    EXPECT_EQ(res.value("size_bytes", 0), static_cast<int>(svg_content.size()));
    std::string decoded = decode_base64_for_test(res.value("data", ""));
    EXPECT_EQ(decoded, svg_content);
  }

  std::filesystem::remove(png_path);
  std::filesystem::remove(jpeg_path);
  std::filesystem::remove(svg_path);
}

TEST(ImageToolTest, ToolExecutionViaToolExecutor) {
  ToolSet tools;
  tools["image"] = ImageTool::definition();

  const auto temp_path = std::filesystem::temp_directory_path() / "qcode_exec_img.png";
  const unsigned char kMinimalPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
      0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
      0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
      0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

  {
    std::ofstream out(temp_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(kMinimalPng), sizeof(kMinimalPng));
  }

  GenerateOptions opts;
  opts.workspace = std::filesystem::temp_directory_path().string();

  ToolCall call("call_test_001", "image", JsonValue{{"path", "qcode_exec_img.png"}, {"description", "exec test"}});
  auto result = ToolExecutor::execute_tool(call, tools, {}, &opts);

  EXPECT_TRUE(result.is_success());
  EXPECT_FALSE(result.error.has_value());
  EXPECT_EQ(result.tool_name, "image");
  EXPECT_EQ(result.tool_call_id, "call_test_001");
  EXPECT_TRUE(result.result.is_object());
  EXPECT_TRUE(result.result.value("success", false));
  EXPECT_EQ(result.result.value("mime_type", ""), "image/png");
  EXPECT_FALSE(result.result.value("data", "").empty());

  std::filesystem::remove(temp_path);
}

TEST(ImageToolTest, AnthropicRequestBuilderFormatsImageResult) {
  anthropic::AnthropicRequestBuilder builder;
  GenerateOptions options;
  options.model = "claude-sonnet-4-6";

  std::vector<ToolCallContentPart> tool_calls;
  tool_calls.emplace_back("call_1", "image", nlohmann::json{{"path", "test.png"}});
  Message assistant_msg = Message::assistant_with_tools("", tool_calls);
  options.messages.push_back(assistant_msg);

  JsonValue img_result = {
      {"success", true},
      {"path", "test.png"},
      {"mime_type", "image/png"},
      {"data", "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJ"},
      {"size_bytes", 32}
  };

  std::vector<ToolResultContentPart> results;
  results.emplace_back("call_1", img_result, false);
  Message tool_msg = Message::tool_results(results);
  options.messages.push_back(tool_msg);

  auto req = builder.build_request_json(options);
  ASSERT_TRUE(req.contains("messages"));
  ASSERT_EQ(req["messages"].size(), 2u);

  const auto& tool_res_msg = req["messages"][1];
  EXPECT_EQ(tool_res_msg["role"], "user");
  ASSERT_TRUE(tool_res_msg["content"].is_array());
  ASSERT_FALSE(tool_res_msg["content"].empty());

  const auto& first_block = tool_res_msg["content"][0];
  EXPECT_EQ(first_block["type"], "tool_result");
  EXPECT_EQ(first_block["tool_use_id"], "call_1");

  ASSERT_TRUE(first_block["content"].is_array());
  bool found_image_block = false;
  for (const auto& part : first_block["content"]) {
    if (part.value("type", "") == "image") {
      found_image_block = true;
      EXPECT_EQ(part["source"]["type"], "base64");
      EXPECT_EQ(part["source"]["media_type"], "image/png");
      EXPECT_EQ(part["source"]["data"], "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJ");
    }
  }
  EXPECT_TRUE(found_image_block);
}

TEST(ImageToolTest, OpenAIRequestBuilderFormatsImageResult) {
  openai::OpenAIRequestBuilder builder(/*use_responses=*/false);
  GenerateOptions options;
  options.model = "gpt-4o";

  std::vector<ToolCallContentPart> tool_calls;
  tool_calls.emplace_back("call_1", "image", nlohmann::json{{"path", "test.png"}});
  Message assistant_msg = Message::assistant_with_tools("", tool_calls);
  options.messages.push_back(assistant_msg);

  JsonValue img_result = {
      {"success", true},
      {"path", "test.png"},
      {"mime_type", "image/png"},
      {"data", "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJ"},
      {"size_bytes", 32}
  };

  std::vector<ToolResultContentPart> results;
  results.emplace_back("call_1", img_result, false);
  Message tool_msg = Message::tool_results(results);
  options.messages.push_back(tool_msg);

  auto req = builder.build_request_json(options);
  ASSERT_TRUE(req.contains("messages"));
  ASSERT_EQ(req["messages"].size(), 2u);

  const auto& tool_res_msg = req["messages"][1];
  EXPECT_EQ(tool_res_msg["role"], "tool");
  EXPECT_EQ(tool_res_msg["tool_call_id"], "call_1");

  ASSERT_TRUE(tool_res_msg["content"].is_array());
  bool found_image_url = false;
  for (const auto& part : tool_res_msg["content"]) {
    if (part.value("type", "") == "image_url") {
      found_image_url = true;
      EXPECT_TRUE(part["image_url"]["url"].get<std::string>().starts_with("data:image/png;base64,"));
    }
  }
  EXPECT_TRUE(found_image_url);
}

TEST(ImageToolTest, GeminiTransformFormatsImageResult) {
  nlohmann::json openai_req;
  openai_req["messages"] = nlohmann::json::array({
      {
          {"role", "assistant"},
          {"tool_calls", nlohmann::json::array({
              {
                  {"id", "call_1"},
                  {"type", "function"},
                  {"function", {{"name", "image"}, {"arguments", "{\"path\":\"test.png\"}"}}}
              }
          })}
      },
      {
          {"role", "tool"},
          {"tool_call_id", "call_1"},
          {"content", nlohmann::json::array({
              {{"type", "text"}, {"text", "Image loaded"}},
              {{"type", "image_url"}, {"image_url", {{"url", "data:image/png;base64,iVBORw0KGgo="}}}}
          })}
      }
  });

  auto gemini_req = gemini::convert_openai_to_gemini(openai_req);
  ASSERT_TRUE(gemini_req.contains("contents"));
  ASSERT_EQ(gemini_req["contents"].size(), 2u);

  const auto& user_turn = gemini_req["contents"][1];
  EXPECT_EQ(user_turn["role"], "user");
  ASSERT_TRUE(user_turn.contains("parts"));

  bool found_function_response = false;
  bool found_inline_data = false;
  for (const auto& part : user_turn["parts"]) {
    if (part.contains("functionResponse")) {
      found_function_response = true;
      EXPECT_EQ(part["functionResponse"]["id"], "call_1");
    }
    if (part.contains("inlineData")) {
      found_inline_data = true;
      EXPECT_EQ(part["inlineData"]["mimeType"], "image/png");
      EXPECT_EQ(part["inlineData"]["data"], "iVBORw0KGgo=");
    }
  }
  EXPECT_TRUE(found_function_response);
  EXPECT_TRUE(found_inline_data);
}

TEST(ImageToolTest, LiveOpenRouterVisionWithImageToolResult) {
  const char* api_key_env = std::getenv("OPENROUTER_API_KEY");
  if (!api_key_env || std::string(api_key_env).empty()) {
    GTEST_SKIP() << "OPENROUTER_API_KEY not set; skipping live vision test";
  }

  // 1. Create a real image on disk: 1x1 red PNG
  const auto temp_path = std::filesystem::temp_directory_path() / "qcode_live_red.png";
  const unsigned char kRedPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
      0x0c, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xfc, 0xcf, 0xc0, 0x50,
      0x0f, 0x00, 0x04, 0x85, 0x01, 0x80, 0xa4, 0xa9, 0x8c, 0xa1, 0x00, 0x00,
      0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

  {
    std::ofstream out(temp_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(kRedPng), sizeof(kRedPng));
  }

  // 2. Execute ImageTool to get the tool output
  ToolExecutionContext ctx;
  ctx.workspace = std::filesystem::temp_directory_path().string();
  auto tool_output = ImageTool::execute(JsonValue{{"path", "qcode_live_red.png"}, {"description", "Red dot"}}, ctx);
  ASSERT_TRUE(tool_output.value("success", false));
  ASSERT_EQ(tool_output.value("mime_type", ""), "image/png");

  // 3. Assemble conversation with tool call and tool result containing image
  GenerateOptions opts;
  opts.model = "nex-agi/nex-n2.5-mini:free";
  opts.max_tokens = 80;

  opts.messages.push_back(Message::user("What color is the pixel in the image? Answer concisely."));
  std::vector<ToolCallContentPart> calls;
  calls.emplace_back("call_img_live", "image", JsonValue{{"path", "qcode_live_red.png"}});
  opts.messages.push_back(Message::assistant_with_tools("", calls));

  std::vector<ToolResultContentPart> results;
  results.emplace_back("call_img_live", tool_output, false);
  opts.messages.push_back(Message::tool_results(results));

  // 4. Send to OpenRouter via OpenAIClient
  std::map<std::string, std::string> headers{
      {"HTTP-Referer", "https://qcode.ai"},
      {"X-Title", "qcode"}
  };
  openai::OpenAIClient client(api_key_env, "https://openrouter.ai/api/v1", /*use_responses=*/false, headers);

  auto res = client.generate_text(opts);
  EXPECT_TRUE(res.is_success()) << "OpenRouter generation failed: " << res.error_message();
  EXPECT_FALSE(res.text.empty());

  std::string text_lower = res.text;
  for (char& c : text_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  bool found_color_keyword = (text_lower.find("red") != std::string::npos ||
                              text_lower.find("salmon") != std::string::npos ||
                              text_lower.find("coral") != std::string::npos ||
                              text_lower.find("pink") != std::string::npos ||
                              text_lower.find("rgb") != std::string::npos);
  EXPECT_TRUE(found_color_keyword) << "Response text was: " << res.text;

  std::filesystem::remove(temp_path);
}

}  // namespace
}  // namespace qcode
