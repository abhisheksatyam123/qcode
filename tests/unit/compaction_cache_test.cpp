// Prompt-cache safety of /compact: the summarizer request must be a strict
// prefix replay of the last routed request (DeepSeek Harness compaction
// rule) - same system prompt, same tool schemas, history verbatim, and the
// compaction directive as the FINAL user message.

#include "providers/anthropic/anthropic_request_builder.h"
#include "providers/openai/openai_request_builder.h"

#include <qcode/compaction/compaction_request.h>
#include <qcode/generation/turn_prefix.h>
#include <qcode/transform/provider_transform.h>

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace qcode {
namespace {

std::vector<ProviderInfo> make_providers() {
  ProviderInfo p;
  p.name = "OpenRouter";
  p.id = "openrouter";
  p.api_url = "https://openrouter.ai/api/v1";
  ModelInfo m;
  m.name = "Claude Sonnet";
  m.id = "anthropic/claude-sonnet-4";
  m.vision = true;
  p.models.push_back(m);
  return {p};
}

Messages make_history() {
  Messages h;
  h.push_back(Message::user("First user message"));
  h.push_back(Message::assistant("Assistant reply one"));
  h.push_back(Message::user("Second user message"));
  h.push_back(Message::assistant("Assistant reply two"));
  return h;
}

std::set<std::string> tool_names(const ToolSet& tools) {
  std::set<std::string> names;
  for (const auto& [name, tool] : tools) {
    (void)tool;
    names.insert(name);
  }
  return names;
}

// Recursively drop cache_control markers: they are metadata that legitimately
// advance as the conversation grows (breakpoint policy), while the cached
// prefix is keyed on the content itself.
nlohmann::json strip_markers(const nlohmann::json& j) {
  if (j.is_object()) {
    nlohmann::json out = nlohmann::json::object();
    for (auto it = j.begin(); it != j.end(); ++it) {
      if (it.key() == "cache_control") continue;
      out[it.key()] = strip_markers(it.value());
    }
    return out;
  }
  if (j.is_array()) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& el : j) out.push_back(strip_markers(el));
    return out;
  }
  return j;
}

compaction::CacheReplayInput make_input(
    const std::vector<ProviderInfo>& providers) {
  compaction::CacheReplayInput in;
  in.system_prompt = "BASE SYSTEM PROMPT";
  in.agent_mode = "orchestrator";
  in.enable_tools = true;
  in.vision_supported = false;
  in.providers = &providers;
  in.session_id = "ses_cache_test";
  return in;
}

// System prompt must be byte-identical to the live turn prefix in every mode.
TEST(CompactionCacheTest, SystemPromptMatchesLiveTurnPrefixInEveryMode) {
  const auto providers = make_providers();
  const std::vector<std::pair<std::string, std::pair<bool, bool>>> modes = {
      {"orchestrator", {false, false}},
      {"plan", {true, false}},
      {"subagent", {false, true}},
  };
  for (const auto& [mode, flags] : modes) {
    SCOPED_TRACE(mode);
    auto in = make_input(providers);
    in.agent_mode = mode;
    const auto opts = compaction::build_cache_replay_request(
        in, "anthropic/claude-sonnet-4", "openrouter", make_history());

    EXPECT_EQ(opts.system, build_turn_system_prompt(
                               "BASE SYSTEM PROMPT", flags.first,
                               flags.second, providers));
    EXPECT_NE(opts.system.find("BASE SYSTEM PROMPT"), std::string::npos);
    // Orchestrator/plan carry their reminder; subagent stays raw.
    const bool expect_orchestrator = !flags.first && !flags.second;
    EXPECT_EQ(opts.system.find("Orchestrator Mode") != std::string::npos,
              expect_orchestrator);
    // The compaction directive must never replace the system prompt.
    EXPECT_EQ(opts.system.find("compaction engine"), std::string::npos);
  }
}

// The directive rides the final user message, never the system prompt.
TEST(CompactionCacheTest, DirectiveIsAppendedAsFinalUserMessage) {
  const auto providers = make_providers();
  auto in = make_input(providers);
  const auto history = make_history();
  const auto opts = compaction::build_cache_replay_request(
      in, "anthropic/claude-sonnet-4", "openrouter", history);

  ASSERT_EQ(opts.messages.size(), history.size() + 1);
  const auto& last = opts.messages.back();
  EXPECT_EQ(last.role, kMessageRoleUser);
  EXPECT_EQ(last.get_text(), compaction::directive());
  EXPECT_EQ(opts.system.find("compaction engine"), std::string::npos);
}

// History replayed through the same normalization pass as live turns.
TEST(CompactionCacheTest, HistoryIsReplayedThroughTheSameNormalization) {
  const auto providers = make_providers();
  auto in = make_input(providers);
  const auto history = make_history();
  const auto opts = compaction::build_cache_replay_request(
      in, "anthropic/claude-sonnet-4", "openrouter", history);

  const auto expected = ProviderTransform::normalize_messages(
      history, Model("anthropic/claude-sonnet-4", "openrouter"));
  ASSERT_EQ(opts.messages.size(), expected.size() + 1);
  for (size_t i = 0; i < expected.size(); ++i) {
    SCOPED_TRACE("index " + std::to_string(i));
    EXPECT_EQ(static_cast<int>(opts.messages[i].role),
              static_cast<int>(expected[i].role));
    EXPECT_EQ(opts.messages[i].get_text(), expected[i].get_text());
  }
}

// Tool schemas segment matches the live turn exactly.
TEST(CompactionCacheTest, ToolsMatchLiveTurnWhenEnabled) {
  const auto providers = make_providers();
  auto in = make_input(providers);
  in.enable_tools = true;
  const auto opts = compaction::build_cache_replay_request(
      in, "anthropic/claude-sonnet-4", "openrouter", make_history());

  const auto expected = build_turn_tools(/*enable_task_tool=*/true,
                                         /*vision_supported=*/false);
  EXPECT_EQ(tool_names(opts.tools), tool_names(expected));
  for (const auto& [name, tool] : expected) {
    auto it = opts.tools.find(name);
    ASSERT_NE(it, opts.tools.end());
    SCOPED_TRACE(name);
    EXPECT_EQ(it->second.description, tool.description);
    EXPECT_EQ(it->second.parameters_schema, tool.parameters_schema);
  }
}

TEST(CompactionCacheTest, ToolsOmittedWhenLiveTurnDisablesThem) {
  const auto providers = make_providers();
  auto in = make_input(providers);
  in.enable_tools = false;
  const auto opts = compaction::build_cache_replay_request(
      in, "anthropic/claude-sonnet-4", "openrouter", make_history());
  EXPECT_TRUE(opts.tools.empty());
}

// Wire-level prefix equality through the Anthropic request builder.
TEST(CompactionCacheTest, AnthropicWirePrefixMatchesTheLastRoutedRequest) {
  const auto providers = make_providers();
  auto in = make_input(providers);
  const auto history = make_history();
  const auto opts =
      compaction::build_cache_replay_request(in, "claude-sonnet-4",
                                             "anthropic", history);

  anthropic::AnthropicRequestBuilder builder;

  // What a live turn with the same context sends.
  GenerateOptions normal;
  normal.model = opts.model;
  normal.system = opts.system;
  normal.tools = opts.tools;
  normal.messages = ProviderTransform::normalize_messages(
      history, Model("claude-sonnet-4", "anthropic"));
  const auto normal_json = builder.build_request_json(normal);

  const auto comp_json = builder.build_request_json(opts);

  ASSERT_EQ(comp_json["system"], normal_json["system"]);
  ASSERT_EQ(comp_json["tools"], normal_json["tools"]);
  ASSERT_EQ(comp_json["messages"].size(), normal_json["messages"].size() + 1);
  // Content prefix must be byte-identical once cache_control markers (which
  // advance with the conversation by design) are removed.
  for (size_t i = 0; i < normal_json["messages"].size(); ++i) {
    SCOPED_TRACE("prefix diverges at message " + std::to_string(i));
    EXPECT_EQ(strip_markers(comp_json["messages"][i]),
              strip_markers(normal_json["messages"][i]));
  }
  const auto& last = comp_json["messages"].back();
  EXPECT_EQ(last["role"], "user");
}

// Anthropic requires alternating roles: a directive appended after a
// trailing user/tool-result message must merge instead of 400.
// Production path: OpenAI-compatible transports (OpenRouter/Zen) must also
// replay a byte-identical content prefix.
TEST(CompactionCacheTest, OpenAIWirePrefixMatchesTheLastRoutedRequest) {
  const auto providers = make_providers();
  auto in = make_input(providers);
  const auto history = make_history();
  const auto opts = compaction::build_cache_replay_request(
      in, "nvidia/nemotron-3-super-120b-a12b:free", "openrouter", history);

  openai::OpenAIRequestBuilder builder;
  builder.set_base_url("https://openrouter.ai/api/v1");

  GenerateOptions normal;
  normal.model = opts.model;
  normal.system = opts.system;
  normal.tools = opts.tools;
  normal.messages = ProviderTransform::normalize_messages(
      history, Model("nvidia/nemotron-3-super-120b-a12b:free", "openrouter"));
  const auto normal_json = builder.build_request_json(normal);

  const auto comp_json = builder.build_request_json(opts);

  // System prompt rides as the leading system message on this transport.
  ASSERT_EQ(comp_json["messages"].size(), normal_json["messages"].size() + 1);
  ASSERT_EQ(comp_json["messages"][0], normal_json["messages"][0]);
  ASSERT_EQ(comp_json["tools"], normal_json["tools"]);
  for (size_t i = 1; i < normal_json["messages"].size(); ++i) {
    SCOPED_TRACE("prefix diverges at message " + std::to_string(i));
    EXPECT_EQ(strip_markers(comp_json["messages"][i]),
              strip_markers(normal_json["messages"][i]));
  }
  const auto& last = comp_json["messages"].back();
  EXPECT_EQ(last["role"], "user");
}

TEST(CompactionCacheTest, AnthropicMergesTrailingConsecutiveUserMessages) {
  anthropic::AnthropicRequestBuilder builder;
  GenerateOptions opts;
  opts.model = "claude-sonnet-4";
  opts.messages = {Message::user("trailing user turn"),
                   Message::user(compaction::directive())};

  const auto req = builder.build_request_json(opts);
  ASSERT_TRUE(req.contains("messages"));
  ASSERT_EQ(req["messages"].size(), 1u);
  EXPECT_EQ(req["messages"][0]["role"], "user");
  const std::string merged = req["messages"][0].dump();
  EXPECT_NE(merged.find("trailing user turn"), std::string::npos);
  EXPECT_NE(merged.find("compaction engine"), std::string::npos);
}

// Cache breakpoints on the last tool definition and the latest message.
TEST(CompactionCacheTest, AnthropicMarksToolAndLatestMessageBreakpoints) {
  anthropic::AnthropicRequestBuilder builder;

  GenerateOptions claude;
  claude.model = "claude-sonnet-4";
  claude.system = "sys";
  claude.messages = {Message::user("hello")};
  claude.tools = {{"read", Tool("Read files", {{"type", "object"}})}};
  const auto req = builder.build_request_json(claude);
  ASSERT_TRUE(req.contains("tools"));
  EXPECT_TRUE(req["tools"].back().contains("cache_control"));
  EXPECT_EQ(req["tools"].back()["cache_control"]["type"], "ephemeral");
  const std::string last_msg = req["messages"].back().dump();
  EXPECT_NE(last_msg.find("cache_control"), std::string::npos);

  // Non-Claude model ids on an Anthropic-compatible endpoint stay clean.
  GenerateOptions other = claude;
  other.model = "deepseek-v4-flash";
  const auto req2 = builder.build_request_json(other);
  EXPECT_FALSE(req2["tools"].back().contains("cache_control"));
  EXPECT_EQ(req2["messages"].back()["content"], "hello");
}

TEST(CompactionCacheTest, OpenRouterClaudeMarksLastToolForCache) {
  openai::OpenAIRequestBuilder builder;
  builder.set_base_url("https://openrouter.ai/api/v1");

  GenerateOptions claude;
  claude.model = "anthropic/claude-sonnet-4";
  claude.messages = {Message::user("hello")};
  claude.tools = {{"read", Tool("Read files", {{"type", "object"}})}};
  const auto req = builder.build_request_json(claude);
  ASSERT_TRUE(req.contains("tools"));
  EXPECT_TRUE(req["tools"].back().contains("cache_control"));

  GenerateOptions other = claude;
  other.model = "deepseek/deepseek-chat";
  const auto req2 = builder.build_request_json(other);
  EXPECT_FALSE(req2["tools"].back().contains("cache_control"));
}

}  // namespace
}  // namespace qcode
