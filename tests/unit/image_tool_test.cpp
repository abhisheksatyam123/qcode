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
  EXPECT_EQ(res.result.size(), 4u);
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
