// Image attachments as user-message input: ImageContentPart must convert to
// each provider's own wire format (OpenAI data URL / Responses input_image,
// Anthropic base64 image block, Gemini inlineData) and survive persistence.

#include "providers/anthropic/anthropic_request_builder.h"
#include "providers/openai/openai_request_builder.h"

#include <qcode/core/message.h>
#include <qcode/session/session_store.h>
#include <qcode/session/token_budget.h>
#include <qcode/transform/gemini_transform.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace qcode {
namespace {

// 1x1 red PNG (69 bytes).
constexpr const char* kTinyPngB64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQD"
    "J/pLvAAAAAElFTkSuQmCC";

Message make_attachment_message() {
  return Message::user_with_images(
      "look at this image",
      {ImageContentPart{kTinyPngB64, "image/png", "a red pixel"}});
}

TEST(ImageAttachmentTest, HelpersExposeImages) {
  const auto msg = make_attachment_message();
  EXPECT_TRUE(msg.has_images());
  EXPECT_TRUE(msg.has_text());
  const auto images = msg.get_images();
  ASSERT_EQ(images.size(), 1u);
  EXPECT_EQ(images[0].mime_type, "image/png");
  EXPECT_EQ(images[0].data, kTinyPngB64);
  EXPECT_EQ(msg.get_text(), "look at this image");
}

TEST(ImageAttachmentTest, OpenAIChatCompletionsEmitsImageDataUrl) {
  openai::OpenAIRequestBuilder builder;
  builder.set_base_url("https://openrouter.ai/api/v1");
  GenerateOptions opts;
  opts.model = "openrouter/free";
  opts.messages = {make_attachment_message()};

  const auto req = builder.build_request_json(opts);
  ASSERT_TRUE(req.contains("messages"));
  const auto& msg = req["messages"].back();
  EXPECT_EQ(msg["role"], "user");
  ASSERT_TRUE(msg["content"].is_array());
  const auto& arr = msg["content"];
  ASSERT_EQ(arr.size(), 2u);
  EXPECT_EQ(arr[0]["type"], "text");
  EXPECT_EQ(arr[0]["text"], "look at this image");
  EXPECT_EQ(arr[1]["type"], "image_url");
  EXPECT_EQ(arr[1]["image_url"]["url"],
            std::string("data:image/png;base64,") + kTinyPngB64);
}

TEST(ImageAttachmentTest, OpenAIResponsesEmitsInputImage) {
  openai::OpenAIRequestBuilder builder(/*use_responses=*/true);
  GenerateOptions opts;
  opts.model = "muse-spark-1.3-contributor-free";
  opts.messages = {make_attachment_message()};

  const auto req = builder.build_request_json(opts);
  // The Responses API path may rename `messages` to `input`.
  ASSERT_TRUE(req.contains("messages") || req.contains("input")) << req.dump(2);
  const auto& wire_msgs =
      req.contains("messages") ? req["messages"] : req["input"];
  ASSERT_FALSE(wire_msgs.empty()) << req.dump(2);
  const auto& last = wire_msgs.back();
  ASSERT_TRUE(last.contains("content")) << req.dump(2);
  const auto& arr = last["content"];
  ASSERT_TRUE(arr.is_array()) << req.dump(2);
  EXPECT_EQ(arr[0]["type"], "input_text");
  ASSERT_EQ(arr.size(), 2u);
  EXPECT_EQ(arr[1]["type"], "input_image");
  EXPECT_EQ(arr[1]["image_url"],
            std::string("data:image/png;base64,") + kTinyPngB64);
}

TEST(ImageAttachmentTest, AnthropicEmitsBase64ImageBlock) {
  anthropic::AnthropicRequestBuilder builder;
  GenerateOptions opts;
  opts.model = "claude-sonnet-5-5";
  opts.messages = {make_attachment_message()};

  const auto req = builder.build_request_json(opts);
  const auto& arr = req["messages"].back()["content"];
  ASSERT_TRUE(arr.is_array());
  ASSERT_EQ(arr.size(), 2u);
  EXPECT_EQ(arr[0]["type"], "text");
  EXPECT_EQ(arr[1]["type"], "image");
  EXPECT_EQ(arr[1]["source"]["type"], "base64");
  EXPECT_EQ(arr[1]["source"]["media_type"], "image/png");
  EXPECT_EQ(arr[1]["source"]["data"], kTinyPngB64);
}

TEST(ImageAttachmentTest, GeminiConvertsUserDataUrlToInlineData) {
  openai::OpenAIRequestBuilder builder;
  builder.set_base_url("https://openrouter.ai/api/v1");
  GenerateOptions opts;
  opts.model = "openrouter/free";
  opts.messages = {make_attachment_message()};
  const auto openai_req = builder.build_request_json(opts);

  const auto gemini_req = gemini::convert_openai_to_gemini(openai_req);
  ASSERT_TRUE(gemini_req.contains("contents"));
  ASSERT_FALSE(gemini_req["contents"].empty());
  const auto& parts = gemini_req["contents"].back()["parts"];
  bool found_text = false;
  bool found_image = false;
  for (const auto& part : parts) {
    if (part.contains("text") &&
        part["text"].get<std::string>() == "look at this image") {
      found_text = true;
    }
    if (part.contains("inlineData")) {
      EXPECT_EQ(part["inlineData"]["mimeType"], "image/png");
      EXPECT_EQ(part["inlineData"]["data"], kTinyPngB64);
      found_image = true;
    }
  }
  EXPECT_TRUE(found_text);
  EXPECT_TRUE(found_image);
}

TEST(ImageAttachmentTest, TokenBudgetCountsImagePayload) {
  const size_t text_only = estimate_tokens({Message::user("look at this image")});
  const size_t with_image = estimate_tokens({make_attachment_message()});
  EXPECT_GT(with_image, text_only);
  EXPECT_GE(with_image, std::string(kTinyPngB64).size() / 4);
}

// ── Persistence envelope round-trip ──

class ImageEnvelopeStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::error_code ec;
    std::filesystem::create_directories("/tmp/qcode_image_attach_test", ec);
    db_path_ = "/tmp/qcode_image_attach_test/test_" +
               std::to_string(::getpid()) + "_" +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()) +
               ".db";
    std::filesystem::remove(db_path_, ec);
    std::filesystem::remove(db_path_ + "-wal", ec);
    std::filesystem::remove(db_path_ + "-shm", ec);
    setenv("QCODE_DB_PATH", db_path_.c_str(), 1);
    session::init_database();
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove(db_path_, ec);
    std::filesystem::remove(db_path_ + "-wal", ec);
    std::filesystem::remove(db_path_ + "-shm", ec);
    unsetenv("QCODE_DB_PATH");
  }
  std::string db_path_;
};

TEST_F(ImageEnvelopeStoreTest, SaveAndReloadUserImageEnvelope) {
  const std::string sid =
      session::create_new_session("prov", "model", "/ws");
  ASSERT_FALSE(sid.empty());

  nlohmann::json env{{"text", "look at this image"},
                     {"images", nlohmann::json::array({{
                                     {"mime_type", "image/png"},
                                     {"data", kTinyPngB64},
                                     {"description", "a red pixel"}}})}};
  session::save_message(sid, "User", env.dump());
  session::save_message(sid, "Assistant", "seen");

  auto msgs = session::load_session_history_parsed(sid);
  ASSERT_EQ(msgs.size(), 2u);
  ASSERT_EQ(msgs[0].role, kMessageRoleUser);
  EXPECT_EQ(msgs[0].get_text(), "look at this image");
  ASSERT_TRUE(msgs[0].has_images());
  const auto images = msgs[0].get_images();
  ASSERT_EQ(images.size(), 1u);
  EXPECT_EQ(images[0].mime_type, "image/png");
  EXPECT_EQ(images[0].data, kTinyPngB64);
  EXPECT_EQ(images[0].description, "a red pixel");

  // Legacy plain-text rows still load as plain user messages.
  session::save_message(sid, "User", "plain text stays plain");
  auto msgs2 = session::load_session_history_parsed(sid);
  ASSERT_EQ(msgs2.size(), 3u);
  EXPECT_FALSE(msgs2[2].has_images());
  EXPECT_EQ(msgs2[2].get_text(), "plain text stays plain");
}

TEST_F(ImageEnvelopeStoreTest, OverwriteSessionHistoryKeepsImages) {
  const std::string sid =
      session::create_new_session("prov", "model", "/ws");
  ASSERT_FALSE(sid.empty());

  session::overwrite_session_history(sid, {make_attachment_message()});
  auto msgs = session::load_session_history_parsed(sid);
  ASSERT_EQ(msgs.size(), 1u);
  ASSERT_TRUE(msgs[0].has_images());
  EXPECT_EQ(msgs[0].get_text(), "look at this image");
  const auto images = msgs[0].get_images();
  ASSERT_EQ(images.size(), 1u);
  EXPECT_EQ(images[0].data, kTinyPngB64);
}

}  // namespace
}  // namespace qcode
