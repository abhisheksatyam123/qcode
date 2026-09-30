// Transform-layer matrix: a rich, compaction-shaped conversation must survive
// every provider format qcode emits - OpenAI Chat Completions, OpenAI
// Responses, Anthropic Messages, Gemini generateContent, and the Cursor text
// prompt - without dropping attachments, breaking tool pairing, or emitting
// schema-invalid roles.

#include "providers/anthropic/anthropic_request_builder.h"
#include "providers/cursor/cursor_request_builder.h"
#include "providers/openai/openai_request_builder.h"

#include <qcode/core/message.h>
#include <qcode/transform/gemini_transform.h>
#include <qcode/transform/provider_transform.h>

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace qcode {
namespace {

constexpr const char* kTinyPngB64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQD"
    "J/pLvAAAAAElFTkSuQmCC";

// Compaction-shaped rich history: system handoff note, user attachment,
// assistant tool calls, tool results, assistant wrap-up, next user question.
Messages rich_history() {
  MessageContent user_parts;
  user_parts.emplace_back(TextContentPart{"look at this attached image"});
  user_parts.emplace_back(ImageContentPart{kTinyPngB64, "image/png", "shot"});

  std::vector<ToolCallContentPart> calls;
  calls.emplace_back("call_1", "bash", nlohmann::json{{"cmd", "ls"}}, "");
  calls.emplace_back("call_2", "read", nlohmann::json{{"path", "a.txt"}}, "");

  std::vector<ToolResultContentPart> results;
  results.emplace_back("call_1", nlohmann::json{{"result", "file.txt"}},
                       false, 12.0);
  results.emplace_back("call_2", nlohmann::json{{"result", "contents"}},
                       false, 3.0);

  return {
      Message::system("Conversation compacted - see ## Systems"),
      Message(kMessageRoleUser, std::move(user_parts)),
      Message::assistant_with_tools("calling tools", calls),
      Message::tool_results(results),
      Message::assistant("done"),
      Message::user("next question"),
  };
}

// ── Anthropic Messages API ──

TEST(TransformMatrixTest, AnthropicNeverEmitsSystemRole) {
  anthropic::AnthropicRequestBuilder builder;
  GenerateOptions opts;
  opts.model = "claude-sonnet-4-6";
  opts.messages = {Message::system("Conversation compacted"),
                   Message::user("hi")};

  const auto req = builder.build_request_json(opts);
  ASSERT_TRUE(req.contains("messages"));
  // The system note maps to a user turn and merges with the following user
  // message, because Anthropic requires strictly alternating roles.
  ASSERT_EQ(req["messages"].size(), 1u);
  for (const auto& m : req["messages"]) {
    const std::string role = m.value("role", "");
    EXPECT_TRUE(role == "user" || role == "assistant");
  }
  const std::string dump = req.dump();
  EXPECT_NE(dump.find("Conversation compacted"), std::string::npos);
  EXPECT_NE(dump.find("hi"), std::string::npos);
}

TEST(TransformMatrixTest, AnthropicRichHistoryAlternatesAndPairs) {
  anthropic::AnthropicRequestBuilder builder;
  GenerateOptions opts;
  opts.model = "claude-sonnet-4-6";
  opts.system = "BASE SYSTEM";
  opts.messages = rich_history();

  const auto req = builder.build_request_json(opts);
  const auto& msgs = req["messages"];
  ASSERT_FALSE(msgs.empty());

  std::set<std::string> pending_tool_uses;
  std::string prev_role;
  for (const auto& m : msgs) {
    const std::string role = m.value("role", "");
    ASSERT_TRUE(role == "user" || role == "assistant");
    if (!prev_role.empty()) {
      EXPECT_NE(role, prev_role);
    }
    prev_role = role;

    const auto& content = m["content"];
    if (role == "assistant" && content.is_array()) {
      for (const auto& block : content) {
        if (block.value("type", "") == "tool_use") {
          pending_tool_uses.insert(block.value("id", ""));
        }
      }
    }
    if (role == "user" && content.is_array()) {
      for (const auto& block : content) {
        if (block.value("type", "") == "tool_result") {
          const auto id = block.value("tool_use_id", "");
          EXPECT_TRUE(pending_tool_uses.erase(id) > 0);
        }
      }
    }
  }
  EXPECT_TRUE(pending_tool_uses.empty());
  // The attachment survives as a base64 image block.
  EXPECT_NE(req.dump().find("\"image\""), std::string::npos);
}

// ── Gemini generateContent (antigravity path) ──

TEST(TransformMatrixTest, GeminiCollectsSystemAndKeepsRolesAndParts) {
  openai::OpenAIRequestBuilder chat;
  chat.set_base_url("https://openrouter.ai/api/v1");
  GenerateOptions opts;
  opts.model = "openrouter/free";
  opts.system = "BASE SYSTEM";
  opts.messages = rich_history();
  const auto openai_req = chat.build_request_json(opts);

  const auto gemini_req = gemini::convert_openai_to_gemini(openai_req);

  ASSERT_TRUE(gemini_req.contains("systemInstruction"));
  const std::string sys = gemini_req["systemInstruction"].dump();
  EXPECT_NE(sys.find("BASE SYSTEM"), std::string::npos);
  EXPECT_NE(sys.find("Conversation compacted"), std::string::npos);

  ASSERT_TRUE(gemini_req.contains("contents"));
  std::set<std::string> roles;
  bool saw_inline_data = false;
  bool saw_function_call = false;
  bool saw_function_response = false;
  for (const auto& content : gemini_req["contents"]) {
    roles.insert(content.value("role", ""));
    for (const auto& part : content.value("parts", nlohmann::json::array())) {
      if (part.contains("inlineData")) saw_inline_data = true;
      if (part.contains("functionCall")) saw_function_call = true;
      if (part.contains("functionResponse")) saw_function_response = true;
    }
  }
  EXPECT_EQ(roles, (std::set<std::string>{"user", "model"}));
  EXPECT_TRUE(saw_inline_data);
  EXPECT_TRUE(saw_function_call);
  EXPECT_TRUE(saw_function_response);
}

// ── OpenAI Chat Completions (OpenRouter / Zen) ──

TEST(TransformMatrixTest, ChatTransportKeepsSystemImageAndToolPairing) {
  openai::OpenAIRequestBuilder chat;
  chat.set_base_url("https://openrouter.ai/api/v1");
  GenerateOptions opts;
  opts.model = "openrouter/free";
  opts.system = "BASE SYSTEM";
  opts.messages = rich_history();

  const auto req = chat.build_request_json(opts);
  const auto& msgs = req["messages"];
  ASSERT_GE(msgs.size(), rich_history().size());
  EXPECT_EQ(msgs[0]["role"], "system");
  EXPECT_EQ(msgs[0]["content"], "BASE SYSTEM");

  // History system note passes through as role system (accepted on
  // OpenAI-compatible transports and proven live after compaction).
  bool saw_history_system = false;
  bool saw_image = false;
  bool saw_tool_result = false;
  std::set<std::string> pending;
  for (const auto& m : msgs) {
    const std::string role = m.value("role", "");
    if (role == "system" && m["content"] != "BASE SYSTEM") {
      saw_history_system = true;
    }
    if (role == "assistant" && m.contains("tool_calls")) {
      for (const auto& tc : m["tool_calls"]) {
        pending.insert(tc.value("id", ""));
      }
    }
    if (role == "tool") {
      saw_tool_result = true;
      EXPECT_TRUE(pending.erase(m.value("tool_call_id", "")) > 0);
    }
    if (m.contains("content") && m["content"].is_array()) {
      for (const auto& part : m["content"]) {
        if (part.value("type", "") == "image_url") saw_image = true;
      }
    }
  }
  EXPECT_TRUE(saw_history_system);
  EXPECT_TRUE(saw_image);
  EXPECT_TRUE(saw_tool_result);
  EXPECT_TRUE(pending.empty());
}

// ── OpenAI Responses transport ──

TEST(TransformMatrixTest, ResponsesTransportShapesItems) {
  openai::OpenAIRequestBuilder responses(/*use_responses=*/true);
  GenerateOptions opts;
  opts.model = "muse-spark-1.3-contributor-free";
  opts.system = "BASE SYSTEM";
  opts.messages = rich_history();

  const auto req = responses.build_request_json(opts);
  ASSERT_TRUE(req.contains("input"));
  const auto& input = req["input"];
  ASSERT_TRUE(input.is_array());

  bool saw_system_string = false;
  bool saw_image = false;
  bool saw_function_output = false;
  bool saw_assistant = false;
  for (const auto& item : input) {
    if (item.contains("type")) {
      if (item.value("type", "") == "function_call_output") {
        saw_function_output = true;
      }
      continue;
    }
    const std::string role = item.value("role", "");
    ASSERT_TRUE(role == "system" || role == "user" || role == "assistant");
    if (role == "assistant") saw_assistant = true;
    if (role == "system" && item["content"].is_string()) {
      saw_system_string = true;
    }
    if (item["content"].is_array()) {
      for (const auto& part : item["content"]) {
        if (part.value("type", "") == "input_image") saw_image = true;
      }
    }
  }
  EXPECT_TRUE(saw_system_string);
  EXPECT_TRUE(saw_image);
  EXPECT_TRUE(saw_function_output);
  EXPECT_TRUE(saw_assistant);
}

// ── Cursor text prompt ──

TEST(TransformMatrixTest, CursorPromptCoversEveryTurn) {
  cursor::CursorRequestBuilder builder;
  GenerateOptions opts;
  opts.model = "grok-code-fast-1";
  opts.system = "BASE SYSTEM";
  opts.messages = rich_history();

  // The prompt is embedded verbatim in the AgentRunRequest protobuf; scan the
  // raw bytes for the expected labels and content.
  const std::string payload = builder.build_agent_run_request(opts);
  const auto contains = [&](const char* needle) {
    return payload.find(needle) != std::string::npos;
  };
  EXPECT_TRUE(contains("BASE SYSTEM"));
  EXPECT_TRUE(contains("Conversation compacted"));
  EXPECT_TRUE(contains("next question"));
  EXPECT_TRUE(contains("[tool call bash"));
  EXPECT_TRUE(contains("[image attachment: image/png"));
}

// ── Normalization invariants shared by every transport ──

TEST(TransformMatrixTest, NormalizeClaudeKeepsImagesDropsUnsignedReasoning) {
  const Model model("claude-sonnet-4-6", "anthropic");
  Messages in;
  MessageContent user_parts;
  user_parts.emplace_back(TextContentPart{"with image"});
  user_parts.emplace_back(ImageContentPart{kTinyPngB64, "image/png", ""});
  in.emplace_back(kMessageRoleUser, std::move(user_parts));
  in.push_back(Message::assistant_with_reasoning("", "unsigned thinking"));
  in.push_back(Message::assistant_with_reasoning("text", "signed", "sig1"));

  const auto out = ProviderTransform::normalize_messages(in, model);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_TRUE(out[0].has_images());
  // Claude keeps only signed reasoning; a message whose sole reasoning part
  // is unsigned disappears entirely.
  ASSERT_EQ(out[1].content.size(), 2u);
  EXPECT_TRUE(out[1].has_reasoning());
  EXPECT_EQ(out[1].get_reasoning(), "signed");
  EXPECT_EQ(out[1].get_text(), "text");
}

TEST(TransformMatrixTest, NormalizeClosesUnpairedToolCallsAtEndOfHistory) {
  const Model model("some-model", "openrouter");
  std::vector<ToolCallContentPart> calls;
  calls.emplace_back("call_9", "bash", nlohmann::json{{"cmd", "ls"}}, "");
  Messages in;
  in.push_back(Message::user("run it"));
  in.push_back(Message::assistant_with_tools("on it", calls));

  const auto out = ProviderTransform::normalize_messages(in, model);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_TRUE(out[2].has_tool_results());
}

}  // namespace
}  // namespace qcode
