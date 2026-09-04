#include <qcode/transform/gemini_transform.h>

#include <regex>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace qcode {
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

TEST(GeminiTransformTest, ConvertReadsMultipartOpenAIContent) {
  json openai_req = parsed(R"({
    "messages": [
      {"role": "system", "content": [
        {"type": "text", "text": "be brief", "cache_control": {"type": "ephemeral"}}
      ]},
      {"role": "user", "content": [
        {"type": "text", "text": "hello cache"}
      ]}
    ]
  })");

  json gem = convert_openai_to_gemini(openai_req);
  EXPECT_EQ(gem["systemInstruction"]["parts"][0]["text"].get<std::string>(),
            "be brief");
  ASSERT_EQ(gem["contents"].size(), 1u);
  EXPECT_EQ(gem["contents"][0]["parts"][0]["text"].get<std::string>(),
            "hello cache");
}

TEST(GeminiTransformTest, ConvertEmptyRequestYieldsEmptyStructures) {
  json gem = convert_openai_to_gemini(json::object());
  EXPECT_EQ(gem["contents"].size(), 0u);
  EXPECT_TRUE(gem["generationConfig"].is_object());
  EXPECT_FALSE(gem.contains("systemInstruction"));
}

TEST(GeminiTransformTest, ConvertPreservesToolsCallsAndResults) {
  json request = parsed(R"({
    "messages": [
      {"role":"assistant","tool_calls":[{"id":"call-1","type":"function",
       "thought_signature":"sig","function":{"name":"lookup","arguments":"{\"q\":\"x\"}"}}]},
      {"role":"tool","tool_call_id":"call-1","content":"{\"value\":1}"}
    ],
    "tools":[{"type":"function","function":{"name":"lookup","description":"Lookup",
      "parameters":{"type":"object"}}}],
    "tool_choice":"required",
    "top_p":0.9,
    "reasoning_effort":"high"
  })");
  const auto gemini = convert_openai_to_gemini(request);
  EXPECT_EQ(gemini["contents"][0]["parts"][0]["functionCall"]["name"], "lookup");
  EXPECT_EQ(gemini["contents"][0]["parts"][0]["thoughtSignature"], "sig");
  EXPECT_EQ(gemini["contents"][1]["parts"][0]["functionResponse"]["name"],
            "lookup");
  EXPECT_EQ(gemini["tools"][0]["functionDeclarations"][0]["name"], "lookup");
  EXPECT_EQ(gemini["toolConfig"]["functionCallingConfig"]["mode"], "ANY");
  EXPECT_DOUBLE_EQ(gemini["generationConfig"]["topP"], 0.9);
  EXPECT_EQ(gemini["generationConfig"]["thinkingConfig"]["thinkingLevel"],
            "high");
  EXPECT_TRUE(
      gemini["generationConfig"]["thinkingConfig"]["includeThoughts"].get<bool>());
}

TEST(GeminiTransformTest, ReasoningObjectEffortMapsToThinkingLevel) {
  json request = parsed(R"({"messages":[{"role":"user","content":"hi"}],
    "reasoning":{"effort":"medium","exclude":false}})");
  const auto gemini = convert_openai_to_gemini(request);
  EXPECT_EQ(gemini["generationConfig"]["thinkingConfig"]["thinkingLevel"],
            "medium");
  EXPECT_TRUE(
      gemini["generationConfig"]["thinkingConfig"]["includeThoughts"].get<bool>());
}

TEST(GeminiTransformTest, MaxEffortMapsToHighBudget) {
  json request = parsed(R"({"messages":[{"role":"user","content":"hi"}],
    "reasoning_effort":"max"})");
  const auto gemini = convert_openai_to_gemini(request);
  EXPECT_EQ(gemini["generationConfig"]["thinkingConfig"]["thinkingLevel"],
            "high");
  EXPECT_EQ(gemini["generationConfig"]["thinkingConfig"]["thinkingBudget"],
            24576);
  EXPECT_TRUE(
      gemini["generationConfig"]["thinkingConfig"]["includeThoughts"].get<bool>());
}

// ---- envelope -------------------------------------------------------------

TEST(GeminiTransformTest, WrapEnvelopeWrapsRequestAndMapsFlashModel) {
  json gem = parsed(R"({"contents":[]})");
  json env = wrap_antigravity_envelope(gem, "gemini-3-flash");

  EXPECT_EQ(env["project"].get<std::string>(), "rising-fact-p41fc");
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

TEST(GeminiTransformTest, WrapEnvelopeMarksClaudeCacheBreakpoints) {
  json gem = parsed(R"({
    "systemInstruction": {"parts": [{"text": "sys"}]},
    "contents": [
      {"role": "user", "parts": [{"text": "u1"}]},
      {"role": "model", "parts": [{"text": "a1"}]},
      {"role": "user", "parts": [{"text": "u2"}]}
    ],
    "tools": [{"functionDeclarations": [{"name": "lookup"}]}]
  })");
  json env = wrap_antigravity_envelope(gem, "claude-sonnet-4-6");
  const auto& req = env["request"];
  EXPECT_EQ(req["systemInstruction"]["parts"][0]["cache_control"]["type"],
            "ephemeral");
  EXPECT_FALSE(req["contents"][0]["parts"][0].contains("cache_control"));
  EXPECT_EQ(req["contents"][1]["parts"][0]["cache_control"]["type"],
            "ephemeral");
  EXPECT_EQ(req["contents"][2]["parts"][0]["cache_control"]["type"],
            "ephemeral");
  EXPECT_EQ(req["tools"][0]["cache_control"]["type"], "ephemeral");
}

TEST(GeminiTransformTest, WrapEnvelopeSkipsCacheMarkersForGemini) {
  json gem = parsed(R"({
    "systemInstruction": {"parts": [{"text": "sys"}]},
    "contents": [{"role": "user", "parts": [{"text": "hi"}]}]
  })");
  json env = wrap_antigravity_envelope(gem, "gemini-3-flash");
  EXPECT_FALSE(
      env["request"]["systemInstruction"]["parts"][0].contains("cache_control"));
  EXPECT_FALSE(env["request"]["contents"][0]["parts"][0].contains("cache_control"));
}

TEST(GeminiTransformTest, WrapEnvelopeUsesConfiguredProjectAndLegacyAlias) {
  const auto env = wrap_antigravity_envelope(
      json::object(), "antigravity-gemini-3-flash", "project-1");
  EXPECT_EQ(env["project"], "project-1");
  EXPECT_EQ(env["model"], "gemini-3-flash-agent");
}

TEST(GeminiTransformTest, WrapEnvelopeMapsGemini38FlashEffort) {
  json gem = parsed(R"({
    "generationConfig": {"thinkingConfig": {"thinkingLevel": "low"}}
  })");
  json env = wrap_antigravity_envelope(gem, "gemini-3.8-flash");
  EXPECT_EQ(env["model"].get<std::string>(), "gemini-3.8-flash-low");
}

TEST(GeminiTransformTest, WrapEnvelopeMapsGemini38FlashHighEffort) {
  json gem = parsed(R"({
    "generationConfig": {"thinkingConfig": {"thinkingLevel": "high"}}
  })");
  json env = wrap_antigravity_envelope(gem, "gemini-3.8-flash");
  EXPECT_EQ(env["model"].get<std::string>(), "gemini-3.8-flash-high");
}

TEST(GeminiTransformTest, WrapEnvelopeDefaultsGemini38FlashToMediumSku) {
  // Bare gemini-3.8-flash 404s on Antigravity. Off/medium thinking must
  // still send the -medium wire id.
  json env = wrap_antigravity_envelope(json::object(), "gemini-3.8-flash");
  EXPECT_EQ(env["model"].get<std::string>(), "gemini-3.8-flash-medium");
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

TEST(GeminiTransformTest, NormalizePreservesAllPartsAndFunctionCalls) {
  const auto response = parsed(R"({
    "candidates":[{"finishReason":"STOP","content":{"parts":[
      {"text":"a"},{"text":"thinking","thought":true,"thoughtSignature":"sig"},
      {"text":"b"},{"functionCall":{"id":"call-1","name":"lookup","args":{"q":"x"}},
       "thoughtSignature":"tool-sig"}
    ]}}]
  })");
  const auto normalized = normalize_gemini_response(response);
  const auto& message = normalized["choices"][0]["message"];
  EXPECT_EQ(message["content"], "ab");
  EXPECT_EQ(message["reasoning"], "thinking");
  EXPECT_EQ(message["tool_calls"][0]["function"]["name"], "lookup");
  EXPECT_EQ(message["tool_calls"][0]["thought_signature"], "tool-sig");
  EXPECT_EQ(normalized["choices"][0]["finish_reason"], "tool_calls");
}

TEST(GeminiTransformTest, NormalizeReturnsNonCandidatePayloadUnchanged) {
  json already_openai = parsed(R"({"choices":[]})");
  EXPECT_EQ(normalize_gemini_response(already_openai), already_openai);
}

TEST(GeminiTransformTest, NormalizeExtractsCachedContentTokenCount) {
  json gemini_payload = parsed(R"({
    "response": {
      "candidates": [
        {
          "content": {
            "parts": [{"text": "Hello world"}]
          },
          "finishReason": "STOP"
        }
      ],
      "usageMetadata": {
        "promptTokenCount": 87548,
        "candidatesTokenCount": 6,
        "totalTokenCount": 87667,
        "cachedContentTokenCount": 81878
      }
    }
  })");

  json normalized = normalize_gemini_response(gemini_payload);
  ASSERT_TRUE(normalized.contains("usage"));
  EXPECT_EQ(normalized["usage"]["prompt_tokens"], 87548);
  EXPECT_EQ(normalized["usage"]["completion_tokens"], 6);
  EXPECT_EQ(normalized["usage"]["total_tokens"], 87667);
  ASSERT_TRUE(normalized["usage"].contains("prompt_tokens_details"));
  EXPECT_EQ(normalized["usage"]["prompt_tokens_details"]["cached_tokens"], 81878);
}

TEST(GeminiTransformTest, NormalizeExtractsThoughtsTokenCount) {
  json payload = parsed(R"({
    "response": {
      "candidates": [{"content":{"parts":[{"text":"ok"}]},"finishReason":"STOP"}],
      "usageMetadata": {
        "promptTokenCount": 10,
        "candidatesTokenCount": 2,
        "totalTokenCount": 40,
        "thoughtsTokenCount": 28
      }
    }
  })");
  json normalized = normalize_gemini_response(payload);
  EXPECT_EQ(normalized["usage"]["completion_tokens_details"]["reasoning_tokens"],
            28);
  EXPECT_EQ(normalized["usage"]["reasoning_tokens"], 28);
}

}  // namespace
}  // namespace gemini
}  // namespace qcode
