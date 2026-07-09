#include "ai/gemini_transform.h"

#include <regex>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace ai {
namespace gemini {
namespace {

using nlohmann::json;

inline json parsed(const char* s) { return json::parse(s); }

// ---- random helpers -------------------------------------------------------

TEST(GeminiTransformTest, NewUuidHasCanonicalShape) {
  std::string uuid = new_uuid();
  // 8-4-4-4-12, version nibble '4', variant nibble in [89ab].
  std::regex re(
      "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
  EXPECT_TRUE(std::regex_match(uuid, re)) << "uuid=" << uuid;
}

TEST(GeminiTransformTest, NewUuidIsUniqueAcrossCalls) {
  EXPECT_NE(new_uuid(), new_uuid());
}

TEST(GeminiTransformTest, RandomHexHasExpectedLengthAndCharset) {
  for (size_t len : {0u, 1u, 16u, 64u}) {
    std::string h = random_hex(len);
    ASSERT_EQ(h.size(), len);
    EXPECT_TRUE(std::regex_match(h, std::regex("^[0-9a-f]*$")));
  }
}

// ---- request translation --------------------------------------------------

TEST(GeminiTransformTest, ConvertMapsRolesAndSystem) {
  json openai_req = parsed(R"({
    "messages": [
      {"role": "system", "content": "be brief"},
      {"role": "user", "content": "hi"},
      {"role": "assistant", "content": "ok"}
    ],
    "temperature": 0.7,
    "max_completion_tokens": 100
  })");

  json gem = convert_openai_to_gemini(openai_req);

  ASSERT_EQ(gem["contents"].size(), 2u);  // system excluded from contents
  EXPECT_EQ(gem["contents"][0]["role"].get<std::string>(), "user");
  EXPECT_EQ(gem["contents"][0]["parts"][0]["text"].get<std::string>(), "hi");
  EXPECT_EQ(gem["contents"][1]["role"].get<std::string>(), "model");
  EXPECT_EQ(gem["contents"][1]["parts"][0]["text"].get<std::string>(), "ok");

  ASSERT_TRUE(gem.contains("systemInstruction"));
  EXPECT_EQ(gem["systemInstruction"]["parts"][0]["text"].get<std::string>(),
            "be brief");

  ASSERT_TRUE(gem.contains("generationConfig"));
  EXPECT_DOUBLE_EQ(gem["generationConfig"]["temperature"].get<double>(), 0.7);
  EXPECT_EQ(gem["generationConfig"]["maxOutputTokens"].get<int>(), 100);  // renamed
}

TEST(GeminiTransformTest, ConvertEmptyRequestYieldsEmptyStructures) {
  json gem = convert_openai_to_gemini(json::object());
  EXPECT_EQ(gem["contents"].size(), 0u);
  EXPECT_TRUE(gem["generationConfig"].is_object());
  EXPECT_FALSE(gem.contains("systemInstruction"));
}

// ---- envelope -------------------------------------------------------------

TEST(GeminiTransformTest, WrapEnvelopeWrapsRequestAndMapsFlashModel) {
  json gem = parsed(R"({"contents":[]})");
  json env = wrap_antigravity_envelope(gem, "gemini-3-flash");

  EXPECT_EQ(env["project"].get<std::string>(), "tuned-keel-d72qv");
  EXPECT_EQ(env["model"].get<std::string>(), "gemini-3-flash-agent");  // mapped
  EXPECT_EQ(env["userAgent"].get<std::string>(), "antigravity");
  EXPECT_EQ(env["requestType"].get<std::string>(), "agent");
  EXPECT_TRUE(env["requestId"].get<std::string>().rfind("agent/", 0) == 0);
  EXPECT_EQ(env["request"], gem);  // inner request preserved
}

TEST(GeminiTransformTest, WrapEnvelopeLeavesNonFlashModelUnchanged) {
  json env = wrap_antigravity_envelope(json::object(), "claude-sonnet");
  EXPECT_EQ(env["model"].get<std::string>(), "claude-sonnet");
}

// ---- unwrap ---------------------------------------------------------------

TEST(GeminiTransformTest, UnwrapExtractsInnerResponse) {
  json wrapped = parsed(R"({"response":{"foo":1},"bar":2})");
  EXPECT_EQ(unwrap_envelope(wrapped), parsed(R"({"foo":1})"));
}

TEST(GeminiTransformTest, UnwrapPassesThroughWhenNoResponseKey) {
  json plain = parsed(R"({"foo":1})");
  EXPECT_EQ(unwrap_envelope(plain), plain);
}

// ---- response normalization ----------------------------------------------

TEST(GeminiTransformTest, NormalizeUnwrapsAndShapesOpenAI) {
  json resp = parsed(R"({
    "response": {
      "candidates": [ { "content": { "parts": [ { "text": "hello" } ] } } ],
      "modelVersion": "gemini-3-flash",
      "usageMetadata": {
        "promptTokenCount": 5,
        "candidatesTokenCount": 3,
        "totalTokenCount": 8
      }
    }
  })");

  json norm = normalize_gemini_response(resp);

  ASSERT_EQ(norm["choices"].size(), 1u);
  EXPECT_EQ(norm["choices"][0]["message"]["role"].get<std::string>(), "assistant");
  EXPECT_EQ(norm["choices"][0]["message"]["content"].get<std::string>(), "hello");
  EXPECT_EQ(norm["choices"][0]["finish_reason"].get<std::string>(), "stop");
  EXPECT_EQ(norm["model"].get<std::string>(), "gemini-3-flash");
  EXPECT_EQ(norm["created"].get<int>(), 0);
  EXPECT_EQ(norm["usage"]["prompt_tokens"].get<int>(), 5);
  EXPECT_EQ(norm["usage"]["completion_tokens"].get<int>(), 3);
  EXPECT_EQ(norm["usage"]["total_tokens"].get<int>(), 8);
}

TEST(GeminiTransformTest, NormalizeMapsFinishReasons) {
  json cand_stop =
      parsed(R"({"candidates":[{"finishReason":"STOP","content":{"parts":[{"text":"a"}]}}]})");
  EXPECT_EQ(normalize_gemini_response(cand_stop)["choices"][0]["finish_reason"]
                .get<std::string>(),
            "stop");

  json cand_max = parsed(
      R"({"candidates":[{"finishReason":"MAX_TOKENS","content":{"parts":[{"text":"a"}]}}]})");
  EXPECT_EQ(normalize_gemini_response(cand_max)["choices"][0]["finish_reason"]
                .get<std::string>(),
            "length");
}

TEST(GeminiTransformTest, NormalizeUsesResponseIdWhenPresent) {
  json resp = parsed(
      R"({"response":{"responseId":"r1","candidates":[{"content":{"parts":[{"text":"x"}]}}]}})");
  EXPECT_EQ(normalize_gemini_response(resp)["id"].get<std::string>(), "r1");
}

TEST(GeminiTransformTest, NormalizeReturnsNonCandidatePayloadUnchanged) {
  json already_openai = parsed(R"({"choices":[]})");
  EXPECT_EQ(normalize_gemini_response(already_openai), already_openai);
}

}  // namespace
}  // namespace gemini
}  // namespace ai
