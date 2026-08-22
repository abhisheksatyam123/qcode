// Tests for the opencode-mirroring provider transform layer and the
// OpenAI-compatible wire format used for OpenCode Zen / OpenRouter free
// models (ox-alpha, deepseek-v4-flash): reasoning effort placement,
// reasoning_content replay, usage accounting, and keyless Zen auth.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <qcode/core/generate_options.h>
#include <qcode/providers/provider_transform.h>

#include "providers/openai/openai_client.h"
#include "providers/openai/openai_request_builder.h"

namespace qcode {
namespace test {

using ProviderTransform::ChatTransport;

static ModelInfo ox_alpha_zen() {
  ModelInfo m;
  m.id = "x-preview-f-free";
  m.name = "Ox Alpha Free (Unlimited)";
  m.reasoning = true;
  m.reasoning_efforts = {"low", "high", "max"};
  m.reasoning_field = "reasoning_content";
  return m;
}

// ── Transport flavor detection ──

TEST(ProviderTransformTest, ChatTransportDetection) {
  EXPECT_EQ(ProviderTransform::chat_transport_for("https://opencode.ai/zen/v1"),
            ChatTransport::kOpenCodeZen);
  EXPECT_EQ(ProviderTransform::chat_transport_for("https://openrouter.ai/api/v1"),
            ChatTransport::kOpenRouter);
  EXPECT_EQ(ProviderTransform::chat_transport_for("https://api.openai.com/v1"),
            ChatTransport::kOpenAI);
  EXPECT_EQ(ProviderTransform::chat_transport_for("http://localhost:8080/v1"),
            ChatTransport::kCompatible);
}

// ── Reasoning variants (upstream reasoningVariants) ──

TEST(ProviderTransformTest, VariantsFromCatalogEfforts) {
  const auto efforts = ProviderTransform::reasoning_variants(ox_alpha_zen());
  EXPECT_THAT(efforts, testing::ElementsAre("low", "high", "max"));
}

TEST(ProviderTransformTest, VariantsFallbackForDeepSeekV4AddsMax) {
  ModelInfo m;
  m.id = "deepseek-v4-flash-free";
  m.reasoning = true;
  const auto efforts = ProviderTransform::reasoning_variants(m);
  EXPECT_THAT(efforts, testing::ElementsAre("low", "medium", "high", "max"));
}

TEST(ProviderTransformTest, DefaultVariantThinkingOn) {
  // ox-alpha: low/high/max declared -> high by default.
  EXPECT_EQ(ProviderTransform::default_variant(ox_alpha_zen()), "high");
  // gpt-5.x defaults to medium (upstream options()).
  ModelInfo gpt5;
  gpt5.id = "gpt-5.3-codex";
  gpt5.reasoning = true;
  gpt5.reasoning_efforts = {"low", "medium", "high"};
  EXPECT_EQ(ProviderTransform::default_variant(gpt5), "medium");
  // Non-reasoning models stay off.
  ModelInfo plain;
  plain.id = "laguna-s-2.1-free";
  plain.reasoning = false;
  EXPECT_EQ(ProviderTransform::default_variant(plain), "off");
}

// ── Effort placement per transport ──

TEST(ProviderTransformTest, ReasoningPlacementPerTransport) {
  nlohmann::json compatible;
  ProviderTransform::apply_reasoning_options(compatible, ChatTransport::kOpenCodeZen, std::string("max"));
  EXPECT_EQ(compatible.value("reasoning_effort", ""), "max");
  EXPECT_FALSE(compatible.contains("reasoning"));

  nlohmann::json openrouter;
  ProviderTransform::apply_reasoning_options(openrouter, ChatTransport::kOpenRouter, std::string("high"));
  EXPECT_EQ(openrouter["reasoning"].value("effort", ""), "high");
  EXPECT_FALSE(openrouter.contains("reasoning_effort"));

  nlohmann::json off;
  ProviderTransform::apply_reasoning_options(off, ChatTransport::kOpenCodeZen, std::string("off"));
  EXPECT_TRUE(off.empty());
}

TEST(ProviderTransformTest, StreamOptionsIncludeUsage) {
  nlohmann::json body;
  ProviderTransform::apply_stream_options(body, ChatTransport::kOpenCodeZen, true);
  EXPECT_TRUE(body["stream_options"].value("include_usage", false));

  nlohmann::json not_streaming;
  ProviderTransform::apply_stream_options(not_streaming, ChatTransport::kOpenCodeZen, false);
  EXPECT_TRUE(not_streaming.empty());
}

// ── Wire format through the real builder (Zen flavor) ──

class ZenWireFormatTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = std::make_unique<openai::OpenAIClient>(
        "", "https://opencode.ai/zen/v1", /*use_responses=*/false,
        std::map<std::string, std::string>{});
    config_.base_url = "https://opencode.ai/zen/v1";
    config_.api_key = "__EMPTY__";
    // Normally populated by OpenAIClient's constructor.
    config_.auth_header_name = "Authorization";
    config_.auth_header_prefix = "Bearer ";
  }

  std::unique_ptr<openai::OpenAIClient> client_;
  providers::ProviderConfig config_;
};

GenerateOptions conversation_with_reasoning() {
  GenerateOptions opts("x-preview-f-free", "");
  Message assistant = Message::assistant_with_tools(
      "done",
      {ToolCallContentPart("call_1", "read_file", nlohmann::json{{"path", "a"}})});
  assistant.content.emplace_back(ReasoningContentPart{"thinking hard", ""});
  opts.messages.push_back(Message::user("hi"));
  opts.messages.push_back(std::move(assistant));
  return opts;
}

TEST_F(ZenWireFormatTest, KeylessZenUsesBearerPublic) {
  openai::OpenAIRequestBuilder builder;
  const auto headers = builder.build_headers(config_);
  const auto auth = headers.find("Authorization");
  ASSERT_NE(auth, headers.end());
  EXPECT_EQ(auth->second, "Bearer public");
}

TEST_F(ZenWireFormatTest, ReasoningReplaysAsReasoningContent) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");
  const auto body = builder.build_request_json(conversation_with_reasoning());

  bool saw_reasoning_content = false;
  for (const auto& msg : body["messages"]) {
    if (msg.value("role", "") == "assistant") {
      saw_reasoning_content = msg.contains("reasoning_content") &&
                              !msg["reasoning_content"].get<std::string>().empty();
      // The legacy field must not ride along on chat transports.
      EXPECT_FALSE(msg.contains("reasoning"));
    }
  }
  EXPECT_TRUE(saw_reasoning_content);
}

TEST_F(ZenWireFormatTest, MaxTokensNotMaxCompletionTokens) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");
  auto opts = GenerateOptions("x-preview-f-free", "hello");
  opts.max_tokens = 1024;
  const auto body = builder.build_request_json(opts);
  EXPECT_EQ(body.value("max_tokens", 0), 1024);
  EXPECT_FALSE(body.contains("max_completion_tokens"));
}

TEST_F(ZenWireFormatTest, EffortLandsOnReasoningEffortField) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");
  auto opts = GenerateOptions("x-preview-f-free", "hello");
  opts.reasoning_effort = "max";
  const auto body = builder.build_request_json(opts);
  EXPECT_EQ(body.value("reasoning_effort", ""), "max");
  EXPECT_FALSE(body.contains("reasoning"));
  // Reasoning-capable requests carry a token budget via max_tokens here.
  EXPECT_GT(body.value("max_tokens", 0), 0);
}

// ── Wire format through the real builder (OpenRouter flavor) ──

TEST(OpenRouterWireFormatTest, EffortAndUsageAccounting) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://openrouter.ai/api/v1");

  auto opts = GenerateOptions("stealth/ox-alpha", "hello");
  opts.reasoning_effort = "high";
  const auto body = builder.build_request_json(opts);

  EXPECT_EQ(body["reasoning"].value("effort", ""), "high");
  EXPECT_FALSE(body.contains("reasoning_effort"));
  EXPECT_TRUE(body["usage"].value("include", false));
  EXPECT_EQ(body.value("max_tokens", 0), 8192);  // safe default budget
}

}  // namespace test
}  // namespace qcode
