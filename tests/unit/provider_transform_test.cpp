// Tests for the opencode-mirroring provider transform layer and the
// OpenAI-compatible wire format used for OpenCode Zen / OpenRouter free
// models (ox-alpha, deepseek-v4-flash): reasoning effort placement,
// reasoning_content replay, usage accounting, and keyless Zen auth.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <qcode/core/generate_options.h>
#include <qcode/providers/provider_profile.h>
#include <qcode/providers/provider_transform.h>
#include <qcode/providers/zen_route.h>
#include <qcode/ui/commands.h>

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

TEST(ProviderTransformTest, ZenApiProtocolMatchesOpenCodeDocs) {
  EXPECT_EQ(ProviderTransform::zen_api_protocol("ox-alpha"), "chat_completions");
  EXPECT_EQ(ProviderTransform::zen_completions_path("ox-alpha"),
            "/chat/completions");
  EXPECT_EQ(ProviderTransform::zen_api_protocol("muse-spark-1.2-contributor-free"),
            "responses");
  EXPECT_EQ(ProviderTransform::zen_completions_path("muse-spark-1.2"),
            "/responses");
  EXPECT_EQ(ProviderTransform::zen_api_protocol("gemini-3-pro"), "google");
  EXPECT_EQ(ProviderTransform::zen_completions_path("gemini-3.7-flash"),
            "/models/gemini-3.7-flash:generateContent");
  EXPECT_EQ(zen_stream_path("/models/gemini-3.7-flash:generateContent"),
            "/models/gemini-3.7-flash:streamGenerateContent?alt=sse");
}

TEST(ProviderProfileTest, KindFromIdAndUrl) {
  EXPECT_EQ(provider_kind("opencode", ""), ProviderKind::kOpenCodeZen);
  EXPECT_EQ(provider_kind("openrouter", ""), ProviderKind::kOpenRouter);
  EXPECT_EQ(provider_kind("anthropic", ""), ProviderKind::kAnthropic);
  EXPECT_EQ(provider_kind("cursor", ""), ProviderKind::kCursor);
  EXPECT_EQ(provider_kind("antigravity", ""), ProviderKind::kAntigravity);
  EXPECT_EQ(provider_kind("openai", ""), ProviderKind::kOpenAI);
  EXPECT_EQ(provider_kind("qpilot", ""), ProviderKind::kQPilot);
  EXPECT_EQ(provider_kind("qgenie", ""), ProviderKind::kQGenie);
  EXPECT_EQ(provider_kind("custom", "https://opencode.ai/zen/v1"),
            ProviderKind::kOpenCodeZen);
  EXPECT_EQ(provider_kind("custom", "https://openrouter.ai/api/v1"),
            ProviderKind::kOpenRouter);
  EXPECT_EQ(provider_kind("custom", "https://api.anthropic.com"),
            ProviderKind::kAnthropic);
}

TEST(ProviderProfileTest, PrepareZenAndOpenRouterAndAnthropic) {
  providers::ProviderOptions zen;
  zen.base_url = "https://opencode.ai/zen/v1";
  zen.project_id = "proj";
  const auto zen_call =
      prepare_provider_call(zen, "opencode", "ox-alpha", "sess-1");
  EXPECT_EQ(zen_call.kind, ProviderKind::kOpenCodeZen);
  EXPECT_EQ(zen_call.wire_model_id, "big-pickle");
  EXPECT_EQ(zen.protocol, "chat_completions");
  EXPECT_EQ(zen.completions_path, "/chat/completions");
  EXPECT_EQ(zen.headers["x-opencode-session"], "sess-1");
  EXPECT_EQ(zen.headers["x-opencode-project"], "proj");

  providers::ProviderOptions muse;
  const auto muse_call =
      prepare_provider_call(muse, "opencode", "muse-spark-1.2-contributor-free");
  EXPECT_EQ(muse.protocol, "responses");
  EXPECT_EQ(muse.completions_path, "/responses");
  EXPECT_EQ(muse_call.wire_model_id, "muse-spark-1.2-contributor-free");

  providers::ProviderOptions claude;
  const auto claude_call =
      prepare_provider_call(claude, "opencode", "claude-sonnet-4-6");
  EXPECT_EQ(claude.protocol, "messages");
  EXPECT_EQ(claude.completions_path, "/messages");
  EXPECT_EQ(claude_call.wire_model_id, "claude-sonnet-4-6");

  providers::ProviderOptions gemini;
  prepare_provider_call(gemini, "opencode", "gemini-3-pro");
  EXPECT_EQ(gemini.protocol, "google");
  EXPECT_EQ(gemini.completions_path, "/models/gemini-3-pro:generateContent");

  providers::ProviderOptions openrouter;
  const auto or_call =
      prepare_provider_call(openrouter, "openrouter", "x-preview-f-free");
  EXPECT_EQ(or_call.kind, ProviderKind::kOpenRouter);
  EXPECT_EQ(or_call.wire_model_id, "stealth/ox-alpha");
  EXPECT_EQ(openrouter.protocol, "chat_completions");
  EXPECT_TRUE(openrouter.completions_path.empty());

  providers::ProviderOptions anthropic;
  const auto anthro =
      prepare_provider_call(anthropic, "anthropic", "claude-sonnet-4-6");
  EXPECT_EQ(anthro.kind, ProviderKind::kAnthropic);
  EXPECT_EQ(anthropic.protocol, "messages");
}

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

TEST(ProviderTransformTest, ReasoningWithoutEffortsGetsGenericLevels) {
  ModelInfo model;
  model.reasoning = true;
  ProviderTransform::apply_reasoning_defaults(model);
  EXPECT_THAT(model.reasoning_efforts,
              testing::ElementsAre("low", "medium", "high"));
}

TEST(ProviderTransformTest, VariantPickerListsModelEfforts) {
  const auto entries = build_variant_entries(ox_alpha_zen());
  std::vector<std::string> ids;
  ids.reserve(entries.size());
  for (const auto& e : entries) ids.push_back(e.id);
  EXPECT_THAT(ids, testing::ElementsAre("off", "low", "high", "max"));
}

TEST(ProviderTransformTest, ClampVariantSnapsUnsupportedEffort) {
  ModelInfo ox = ox_alpha_zen();  // low/high/max
  EXPECT_EQ(ProviderTransform::clamp_variant(ox, "off"), "off");
  EXPECT_EQ(ProviderTransform::clamp_variant(ox, "high"), "high");
  EXPECT_EQ(ProviderTransform::clamp_variant(ox, "medium"), "high");
  EXPECT_EQ(ProviderTransform::clamp_variant(ox, "max"), "max");
}

TEST(ProviderTransformTest, CursorFamilyAndWireIds) {
  EXPECT_EQ(ProviderTransform::cursor_picker_id("claude-opus-5-thinking-high"),
            "claude-opus-5");
  EXPECT_EQ(ProviderTransform::cursor_family_id("grok-4.6-high"), "grok-4.6");
  EXPECT_EQ(ProviderTransform::cursor_wire_model_id("grok-4.6", "medium"),
            "grok-4.6");
  EXPECT_EQ(ProviderTransform::cursor_wire_model_id("grok-4.6", "high"),
            "grok-4.6-high");
}

TEST(ProviderTransformTest, DefaultVariantUsesConfiguredValue) {
  ModelInfo model;
  model.reasoning = true;
  model.reasoning_efforts = {"low", "medium", "high"};
  model.reasoning_default = "medium";
  EXPECT_EQ(ProviderTransform::default_variant(model), "medium");
}

TEST(ProviderTransformTest, DefaultVariantFallsBackToFirstEffort) {
  ModelInfo model;
  model.reasoning = true;
  model.reasoning_efforts = {"low", "high", "max"};
  EXPECT_EQ(ProviderTransform::default_variant(model), "low");
}

TEST(ProviderTransformTest, NonReasoningDefaultIsOff) {
  ModelInfo plain;
  plain.reasoning = false;
  EXPECT_EQ(ProviderTransform::default_variant(plain), "off");
}

// ── Effort placement per transport ──

TEST(ProviderTransformTest, ReasoningPlacementPerTransport) {
  nlohmann::json compatible;
  ProviderTransform::apply_reasoning_options(compatible, ChatTransport::kOpenCodeZen, std::string("max"));
  EXPECT_EQ(compatible.value("reasoning_effort", ""), "max");
  EXPECT_FALSE(compatible.contains("include_reasoning"));
  EXPECT_FALSE(compatible.contains("reasoning"));

  nlohmann::json openrouter;
  ProviderTransform::apply_reasoning_options(openrouter, ChatTransport::kOpenRouter, std::string("high"));
  EXPECT_EQ(openrouter["reasoning"].value("effort", ""), "high");
  EXPECT_FALSE(openrouter["reasoning"].contains("exclude"));
  EXPECT_FALSE(openrouter.contains("reasoning_effort"));

  nlohmann::json off;
  ProviderTransform::apply_reasoning_options(off, ChatTransport::kOpenCodeZen, std::string("off"));
  EXPECT_TRUE(off.empty());
}

TEST(ProviderTransformTest, InterleavedReplayFieldMatchesOpenCode) {
  EXPECT_EQ(ProviderTransform::interleaved_replay_field(
                ChatTransport::kOpenCodeZen, "x-preview-f-free"),
            "reasoning_content");
  EXPECT_EQ(ProviderTransform::interleaved_replay_field(
                ChatTransport::kOpenCodeZen, "muse-spark-1.2-contributor-free"),
            "reasoning");
  EXPECT_TRUE(ProviderTransform::interleaved_replay_field(
                  ChatTransport::kOpenRouter, "stealth/ox-alpha")
                  .empty());
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
      // The legacy field must not ride along on ox-alpha chat transports.
      EXPECT_FALSE(msg.contains("reasoning"));
    }
  }
  EXPECT_TRUE(saw_reasoning_content);
}

TEST_F(ZenWireFormatTest, MuseSparkReplaysAsReasoningField) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");
  auto opts = conversation_with_reasoning();
  opts.model = "muse-spark-1.2-contributor-free";
  const auto body = builder.build_request_json(opts);
  bool saw_reasoning = false;
  for (const auto& msg : body["messages"]) {
    if (msg.value("role", "") != "assistant") continue;
    saw_reasoning = msg.contains("reasoning") &&
                    msg["reasoning"].is_string() &&
                    !msg["reasoning"].get<std::string>().empty();
    EXPECT_FALSE(msg.contains("reasoning_content"));
  }
  EXPECT_TRUE(saw_reasoning);
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

TEST_F(ZenWireFormatTest, RemapsOxAlphaAliasInRequestBody) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");
  auto opts = GenerateOptions("ox-alpha", "hello");
  const auto body = builder.build_request_json(opts);
  EXPECT_EQ(body.value("model", ""), "big-pickle");
}

TEST_F(ZenWireFormatTest, EffortLandsOnReasoningEffortField) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");
  auto opts = GenerateOptions("x-preview-f-free", "hello");
  opts.reasoning_effort = "max";
  const auto body = builder.build_request_json(opts);
  EXPECT_EQ(body.value("reasoning_effort", ""), "max");
  EXPECT_FALSE(body.contains("include_reasoning"));
  EXPECT_FALSE(body.contains("reasoning"));
  // Reasoning-capable requests carry a token budget via max_tokens here.
  EXPECT_GT(body.value("max_tokens", 0), 0);
}

TEST_F(ZenWireFormatTest, RemapsMuseSparkOpenRouterAlias) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://opencode.ai/zen/v1");
  auto opts = GenerateOptions("meta/muse-spark-1.2", "hello");
  const auto body = builder.build_request_json(opts);
  EXPECT_EQ(body.value("model", ""), "muse-spark-1.2-contributor-free");
}

TEST(OpenRouterWireFormatTest, RemapsZenOxAlphaAlias) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://openrouter.ai/api/v1");
  auto opts = GenerateOptions("x-preview-f-free", "hello");
  const auto body = builder.build_request_json(opts);
  EXPECT_EQ(body.value("model", ""), "stealth/ox-alpha");
}

// ── Wire format through the real builder (OpenRouter flavor) ──

TEST(OpenRouterWireFormatTest, EffortAndUsageAccounting) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://openrouter.ai/api/v1");

  auto opts = GenerateOptions("stealth/ox-alpha", "hello");
  opts.reasoning_effort = "high";
  const auto body = builder.build_request_json(opts);

  EXPECT_EQ(body["reasoning"].value("effort", ""), "high");
  EXPECT_FALSE(body["reasoning"].contains("exclude"));
  EXPECT_FALSE(body.contains("reasoning_effort"));
  EXPECT_FALSE(body.contains("include_reasoning"));
  EXPECT_TRUE(body["usage"].value("include", false));
  EXPECT_EQ(body.value("max_tokens", 0), 8192);  // safe default budget
}

TEST(ProviderTransformTest, NormalizeMessagesKeepsObjectToolResults) {
  // Follow-up turns replay bash/file tool results as JSON objects. Passing
  // those to sanitize_surrogates (std::string) used to throw type_error.302
  // before any provider request was sent.
  Messages history;
  history.push_back(Message::user("move the header"));
  history.push_back(Message::assistant_with_tools(
      "", {ToolCallContentPart{
              "call_1", "bash",
              nlohmann::json{{"command", "sed -n '1,10p' views.cpp"}}}}));
  history.push_back(Message::tool_results(
      {{"call_1",
        nlohmann::json{{"output", "int main() {}"},
                       {"metadata", {{"exit", 0}}}},
        false}}));
  history.push_back(Message::user("1 MOVE MODEL AND CONTEXT INFO ALL TO TOP HEADER"));

  Model model("muse-spark-1.3-contributor-free", "opencode");
  Messages normalized;
  EXPECT_NO_THROW(normalized =
                      ProviderTransform::normalize_messages(history, model));
  ASSERT_EQ(normalized.size(), 4u);
  ASSERT_TRUE(normalized[2].has_tool_results());
  const auto results = normalized[2].get_tool_results();
  ASSERT_EQ(results.size(), 1u);
  EXPECT_TRUE(results[0].result.is_object());
  EXPECT_EQ(results[0].result.value("output", ""), "int main() {}");

  openai::OpenAIRequestBuilder builder(true);
  builder.set_base_url("https://opencode.ai/zen/v1");
  GenerateOptions options;
  options.model = "muse-spark-1.3-contributor-free";
  options.messages = std::move(normalized);
  nlohmann::json body;
  EXPECT_NO_THROW(body = builder.build_request_json(options));
  ASSERT_TRUE(body.contains("input"));
  // Tool-only assistant turns must not emit {role:assistant, content:null} —
  // Zen Responses rejects that as input[n].content type mismatch.
  for (const auto& item : body["input"]) {
    if (item.contains("content")) {
      EXPECT_FALSE(item["content"].is_null());
    }
    if (item.value("type", "") == "function_call") {
      EXPECT_FALSE(item.contains("content"));
    }
  }
}

TEST(OpenRouterWireFormatTest, SkipsInterleavedReasoningReplay) {
  openai::OpenAIRequestBuilder builder(false);
  builder.set_base_url("https://openrouter.ai/api/v1");
  auto opts = conversation_with_reasoning();
  opts.model = "stealth/ox-alpha";
  const auto body = builder.build_request_json(opts);
  for (const auto& msg : body["messages"]) {
    if (msg.value("role", "") != "assistant") continue;
    EXPECT_FALSE(msg.contains("reasoning_content"));
    EXPECT_FALSE(msg.contains("reasoning"));
  }
}

}  // namespace test
}  // namespace qcode
