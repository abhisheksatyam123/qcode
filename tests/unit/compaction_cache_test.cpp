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

#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>
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
    // Collapse a lone text block [{type:text, text:X}] back to "X": the
    // rolling cache window wraps marked turns in block form, which is
    // semantically identical content for prefix comparison.
    if (out.size() == 1 && out[0].is_object() && out[0].size() == 2 &&
        out[0].value("type", "") == "text" && out[0].contains("text") &&
        out[0]["text"].is_string()) {
      return out[0]["text"];
    }
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
  const std::vector<std::pair<std::string, bool>> modes = {
      {"orchestrator", false},
      {"subagent", true},
  };
  for (const auto& [mode, is_subagent] : modes) {
    SCOPED_TRACE(mode);
    auto in = make_input(providers);
    in.agent_mode = mode;
    const auto opts = compaction::build_cache_replay_request(
        in, "anthropic/claude-sonnet-4", "openrouter", make_history());

    EXPECT_EQ(opts.system, build_turn_system_prompt(
                               "BASE SYSTEM PROMPT", is_subagent,
                               providers));
    EXPECT_NE(opts.system.find("BASE SYSTEM PROMPT"), std::string::npos);
    // Orchestrator carries its reminder; subagent stays raw.
    const bool expect_orchestrator = !is_subagent;
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

// ── The summarizer replays the turn's request parameters ─────────────

ModelInfo thinking_model() {
  ModelInfo m;
  m.id = m.name = "claude-sonnet-5-5";
  m.reasoning = true;
  m.tool_call = true;
  m.max_tokens = 32000;
  m.output_limit = 64000;
  m.thinking_type = "adaptive";
  m.thinking_display = "summarized";
  m.thinking_allow_off = false;
  VariantInfo high;
  high.id = "high";
  VariantInfo ultra;
  ultra.id = "ultra";
  ultra.effort = "max";
  ultra.max_tokens = 64000;
  ultra.prompt = "ULTRA VARIANT PROMPT";
  m.variants = {high, ultra};
  m.reasoning_efforts = {"high", "ultra"};
  return m;
}

// A thinking turn: every assistant step carries a signed thinking block.
Messages thinking_history() {
  Messages h;
  h.push_back(Message::user("Run ls"));
  Message step = Message::assistant_with_tools(
      "Listing.", {ToolCallContentPart{"toolu_1", "bash", {{"command", "ls"}}}});
  step.content.emplace_back(ReasoningContentPart{"Need the file list.", "sig-1"});
  h.push_back(std::move(step));
  h.push_back(Message::tool_results({{"toolu_1", {{"output", "a b"}}, false}}));
  h.push_back(Message::assistant_with_reasoning("Two files.", "Count them.", "sig-2"));
  return h;
}

// What run_generation routes for `history` under `variant`.
GenerateOptions live_turn(const std::vector<ProviderInfo>& providers,
                          const ModelInfo& model, const std::string& variant,
                          Messages history) {
  GenerateOptions o;
  o.model = model.id;
  o.system = build_turn_system_prompt("BASE SYSTEM PROMPT", false, providers);
  const Model transform_model(model.id, "anthropic");
  o.messages = ProviderTransform::normalize_messages(std::move(history), transform_model);
  apply_turn_sampling(o, &model, transform_model);
  apply_variant_options(o, &model, variant, model.id);
  o.tools = build_turn_tools(/*enable_task_tool=*/true, model.vision);
  o.session_id = "ses_cache_test";
  return o;
}

// Anthropic invalidates the message cache when thinking settings change and
// drops signed thinking blocks when thinking is off: the summarizer must send
// the turn's thinking, effort, output budget and variant prompt.
TEST(CompactionCacheTest, AnthropicSummarizerKeepsTheTurnsThinkingAndSignedBlocks) {
  const auto providers = make_providers();
  const ModelInfo model = thinking_model();
  for (const std::string variant : {"high", "ultra"}) {
    SCOPED_TRACE(variant);
    auto in = make_input(providers);
    in.model = &model;
    in.reasoning_mode = variant;
    const auto comp = compaction::build_cache_replay_request(
        in, model.id, "anthropic", thinking_history());
    const auto turn = live_turn(providers, model, variant, thinking_history());

    anthropic::AnthropicRequestBuilder builder;
    const auto comp_json = builder.build_request_json(comp);
    const auto turn_json = builder.build_request_json(turn);
    EXPECT_EQ(comp_json["thinking"], turn_json["thinking"]);
    EXPECT_EQ(comp_json["thinking"].value("type", ""), "adaptive");
    EXPECT_EQ(comp_json.value("output_config", nlohmann::json()),
              turn_json.value("output_config", nlohmann::json()));
    EXPECT_EQ(comp_json["max_tokens"], turn_json["max_tokens"]);
    EXPECT_EQ(comp_json["system"], turn_json["system"]);
    EXPECT_EQ(comp_json["system"].dump().find("ULTRA VARIANT PROMPT") != std::string::npos,
              variant == "ultra");
    EXPECT_EQ(comp_json["tools"], turn_json["tools"]);
    ASSERT_EQ(comp_json["messages"].size(), turn_json["messages"].size() + 1);
    for (size_t i = 0; i < turn_json["messages"].size(); ++i) {
      SCOPED_TRACE("prefix diverges at message " + std::to_string(i));
      EXPECT_EQ(strip_markers(comp_json["messages"][i]),
                strip_markers(turn_json["messages"][i]));
    }
    const auto& step = comp_json["messages"][1]["content"];
    ASSERT_TRUE(step.is_array());
    EXPECT_EQ(step[0].value("type", ""), "thinking");
    EXPECT_EQ(step[0].value("signature", ""), "sig-1");
  }
}

// Each app's turns and its /compact share one history transform.
TEST(AutoCompactTest, ThresholdIsConfiguredValueCappedBelowTheWindow) {
  ModelInfo m;
  EXPECT_EQ(compaction::auto_compact_threshold(nullptr), 0u);
  EXPECT_EQ(compaction::auto_compact_threshold(&m), 0u);  // nothing known
  m.compact_threshold = 250000;
  EXPECT_EQ(compaction::auto_compact_threshold(&m), 250000u);  // unknown window
  m.context_window = 1000000;
  m.max_tokens = 32000;
  EXPECT_EQ(compaction::auto_compact_threshold(&m), 250000u);
  // A 200k window keeps room for the 32k reply.
  m.context_window = 200000;
  EXPECT_EQ(compaction::auto_compact_threshold(&m), 168000u);
  // Unconfigured: the cap (window minus at least 10%).
  m.compact_threshold = 0;
  m.context_window = 1000000;
  EXPECT_EQ(compaction::auto_compact_threshold(&m), 900000u);
  m.auto_compact = false;
  m.compact_threshold = 250000;
  EXPECT_EQ(compaction::auto_compact_threshold(&m), 0u);
}

TEST(AutoCompactTest, InLoopRequestIsTheStepRequestPlusTheDirective) {
  GenerateOptions step;
  step.model = "m";
  step.system = "sys";
  step.messages = make_history();
  step.tools = build_turn_tools(true, false);
  step.max_tokens = 32000;
  step.on_tool_call_start = [](const ToolCall&) {};
  const GenerateOptions req = compaction::build_in_loop_request(step);
  EXPECT_EQ(req.system, step.system);
  EXPECT_EQ(req.max_tokens, step.max_tokens);
  EXPECT_EQ(tool_names(req.tools), tool_names(step.tools));
  ASSERT_EQ(req.messages.size(), step.messages.size() + 1);
  EXPECT_EQ(req.messages.back().role, kMessageRoleUser);
  EXPECT_EQ(std::get<TextContentPart>(req.messages.back().content[0]).text,
            compaction::directive());
  EXPECT_FALSE(req.on_tool_call_start.has_value());
  EXPECT_EQ(req.max_steps, 1);
}

TEST(AutoCompactTest, ContinuationCarriesMarkerTaskFileAndContinueInstruction) {
  const auto ws = std::filesystem::temp_directory_path() /
                  ("qcode_autocompact_" + std::to_string(::getpid()));
  std::filesystem::create_directories(ws / "scratchpad");
  {
    std::ofstream(ws / "scratchpad" / "todo.md")
        << "## Tasks\n- [ ] T1 port parser\n## Systems\n## Log\n";
  }
  const std::string text = compaction::continuation_message(
      "## Tasks\n- T1 in progress", "/x/handoff.md", ws.string(), true);
  EXPECT_EQ(text.rfind(compaction::kSummaryMarker, 0), 0u);
  EXPECT_NE(text.find("written to: /x/handoff.md"), std::string::npos);
  EXPECT_NE(text.find("T1 port parser"), std::string::npos);
  EXPECT_NE(text.find("without asking the user"), std::string::npos);
  // Stored history is cut at it.
  Messages h = make_history();
  h.push_back(Message::user(text));
  h.push_back(Message::assistant("continuing"));
  const Messages cut = apply_compaction_cutoff(h);
  ASSERT_EQ(cut.size(), 2u);
  EXPECT_FALSE(compaction::continuation_message("s", "", ws.string(), false)
                   .find("without asking") != std::string::npos);
  std::filesystem::remove_all(ws);
}

TEST(CompactionCacheTest, TurnHistoryCutsAtTheSummaryAndDropsTuiNotes) {
  Messages h;
  h.push_back(Message::user("old question"));
  h.push_back(Message::system("Conversation compacted: 9 messages -> handoff packet"));
  h.push_back(Message::user(
      "This conversation was compacted into a handoff packet.\n\nsummary"));
  h.push_back(Message::assistant("ok"));
  h.push_back(Message::system("Unknown command: /x"));
  h.push_back(Message::user("next"));

  const auto tui = prepare_turn_history(h, /*drop_system_notes=*/true);
  ASSERT_EQ(tui.size(), 3u);
  EXPECT_EQ(tui[0].get_text().rfind("This conversation was compacted", 0), 0u);
  EXPECT_EQ(tui[1].get_text(), "ok");
  EXPECT_EQ(tui[2].get_text(), "next");

  const auto server = prepare_turn_history(h, /*drop_system_notes=*/false);
  ASSERT_EQ(server.size(), 4u);
  EXPECT_EQ(server[2].role, kMessageRoleSystem);
}

}  // namespace
}  // namespace qcode
