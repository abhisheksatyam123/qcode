#include <qcode/providers/registry.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/transform/provider_transform.h>
#include <qcode/core/generate_options.h>
#include "providers/cursor/cursor_agent_session.h"
#include "providers/cursor/cursor_bidi.h"
#include "providers/cursor/cursor_exec.h"
#include "providers/cursor/cursor_kv.h"
#include "providers/cursor/cursor_proto.h"
#include "providers/cursor/cursor_request_builder.h"
#include "providers/cursor/cursor_response_parser.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

namespace qcode {
namespace cursor {
namespace {

TEST(CursorProviderTest, ParsesLiveModelCatalogMetadata) {
  const auto sol = proto::bytes_field(1, "gpt-5.6-sol") +
                   proto::bytes_field(4, "GPT-5.6 Sol");
  const auto fable = proto::bytes_field(1, "claude-fable-5-thinking-high") +
                     proto::bytes_field(3, "Claude Fable 5 High");
  const auto response =
      proto::bytes_field(1, sol) + proto::bytes_field(1, fable);

  const auto models =
      CursorResponseParser::parse_get_usable_models(response);

  ASSERT_EQ(models.size(), 2u);
  EXPECT_EQ(models[0].id, "gpt-5.6-sol");
  EXPECT_EQ(models[0].name, "GPT-5.6 Sol");
  EXPECT_EQ(models[1].id, "claude-fable-5-thinking-high");
  EXPECT_EQ(models[1].name, "Claude Fable 5 High");
}

TEST(CursorProviderTest, FallsBackToModelIdWhenDisplayNameIsMissing) {
  const auto model = proto::bytes_field(1, "composer-2.5");
  const auto response = proto::bytes_field(1, model);

  const auto models =
      CursorResponseParser::parse_get_usable_models(response);

  ASSERT_EQ(models.size(), 1u);
  EXPECT_EQ(models.front().id, "composer-2.5");
  EXPECT_EQ(models.front().name, "composer-2.5");
}

TEST(CursorProviderTest, SendsStableConversationPrefixForAutomaticCaching) {
  GenerateOptions options;
  options.model = "gpt-5.6-sol";
  options.system = "Follow the project rules.";
  options.messages = {
      Message::user("Inspect the provider."),
      Message::assistant("I found the provider."),
      Message::user("Fix the model catalog."),
  };

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);

  EXPECT_NE(request.find("System:\nFollow the project rules."),
            std::string::npos);
  EXPECT_NE(request.find("user:\nInspect the provider."),
            std::string::npos);
  EXPECT_NE(request.find("assistant:\nI found the provider."),
            std::string::npos);
  EXPECT_NE(request.find("user:\nFix the model catalog."),
            std::string::npos);
}

TEST(CursorProviderTest, DoesNotReuseSessionIdAsConversationId) {
  GenerateOptions options;
  options.model = "composer-2.5";
  options.session_id = "11111111-2222-3333-4444-555555555555";
  options.messages = {Message::user("ping")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);
  // A reused session id poisons the next turn after an idle-end/abort.
  EXPECT_EQ(request.find(options.session_id), std::string::npos);
}

TEST(CursorProviderTest, KeepsPromptPrefixStableForAutomaticCache) {
  GenerateOptions options;
  options.model = "claude-opus-5";
  options.session_id = "cache-session-cursor";
  options.system = "Stable prefix for Cursor automatic cache.";
  options.reasoning_effort = "high";
  options.messages = {Message::user("first"), Message::assistant("ack"),
                      Message::user("second")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);
  EXPECT_EQ(request.find(options.session_id), std::string::npos);
  EXPECT_NE(request.find("claude-opus-5-thinking-high"), std::string::npos);
  EXPECT_NE(request.find("Stable prefix for Cursor automatic cache."),
            std::string::npos);
}

TEST(CursorProviderTest, MapsGrokVariantOntoWireModelSlug) {
  GenerateOptions options;
  options.model = "grok-4.6";
  options.system = "Be concise.";
  options.reasoning_effort = "high";
  options.messages = {Message::user("hi")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);

  EXPECT_NE(request.find("cursor-grok-4.6-high"), std::string::npos);
  EXPECT_EQ(request.find("Reasoning effort:"), std::string::npos);
  EXPECT_NE(request.find("Be concise."), std::string::npos);
}

TEST(CursorProviderTest, GrokDefaultEffortMapsOntoMediumSku) {
  GenerateOptions options;
  options.model = "cursor-grok-4.6-high";
  options.messages = {Message::user("hi")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);

  EXPECT_NE(request.find("cursor-grok-4.6-medium"), std::string::npos);
}

TEST(CursorProviderTest, MapsOpusLowVariantOntoThinkingSlug) {
  GenerateOptions options;
  options.model = "claude-opus-5";
  options.reasoning_effort = "low";
  options.messages = {Message::user("hi")};

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);
  EXPECT_NE(request.find("claude-opus-5-thinking-low"), std::string::npos);
}

TEST(CursorProviderTest, ClassifyAgentPayloadHandlesErrorsAndPreventsFalsePositives) {
  // Real JSON error object
  const std::string real_error = "{\"error\": \"Invalid API key\", \"code\": 401}";
  auto ev1 = CursorResponseParser::classify_agent_payload(real_error);
  EXPECT_EQ(ev1.kind, AgentStreamEvent::Kind::kError);
  EXPECT_EQ(ev1.error, real_error);

  // Real JSON message error object
  const std::string real_message_error = "{\"message\": \"Quota exceeded\"}";
  auto ev2 = CursorResponseParser::classify_agent_payload(real_message_error);
  EXPECT_EQ(ev2.kind, AgentStreamEvent::Kind::kError);
  EXPECT_EQ(ev2.error, real_message_error);

  // Binary payload containing the word "error" and '{' but not valid JSON
  const std::string false_positive = " o-\"-MpQ>7J&System:Tools are available when needed, including error checks { ... }";
  auto ev3 = CursorResponseParser::classify_agent_payload(false_positive);
  EXPECT_NE(ev3.kind, AgentStreamEvent::Kind::kError);
}

TEST(CursorProviderTest, ClassifiesThinkingDeltaAsReasoning) {
  const auto thinking_delta = proto::bytes_field(1, "ponder this");
  const auto update = proto::bytes_field(4, thinking_delta);
  const auto payload = proto::bytes_field(1, update);
  const auto ev = CursorResponseParser::classify_agent_payload(payload);
  EXPECT_EQ(ev.kind, AgentStreamEvent::Kind::kReasoningDelta);
  EXPECT_EQ(ev.text, "ponder this");
}

TEST(CursorProviderTest, ClassifiesShellExecAsExec) {
  const auto shell_args = proto::bytes_field(1, "ls") + proto::bytes_field(2, "/tmp");
  const auto exec = proto::varint_field(1, 9) + proto::bytes_field(15, "exec-1") +
                    proto::bytes_field(2, shell_args);
  const auto payload = proto::bytes_field(2, exec);
  const auto ev = CursorResponseParser::classify_agent_payload(payload);
  EXPECT_EQ(ev.kind, AgentStreamEvent::Kind::kExec);
  EXPECT_EQ(ev.exec_id, 9u);
  EXPECT_EQ(ev.exec_id_str, "exec-1");
  EXPECT_EQ(ev.exec_field, 2u);
  EXPECT_NE(ev.exec_args.find("ls"), std::string::npos);
}

TEST(CursorProviderTest, ClassifiesPiBashExecAsExec) {
  const auto args = proto::bytes_field(1, "echo hi");
  const auto exec = proto::varint_field(1, 3) + proto::bytes_field(46, args);
  const auto payload = proto::bytes_field(2, exec);
  const auto ev = CursorResponseParser::classify_agent_payload(payload);
  EXPECT_EQ(ev.kind, AgentStreamEvent::Kind::kExec);
  EXPECT_EQ(ev.exec_field, 46u);
}

TEST(CursorProviderTest, ClassifiesKvServerMessage) {
  const auto args = proto::bytes_field(1, "blob-key");
  const auto kv = proto::varint_field(1, 7) + proto::bytes_field(2, args);
  const auto payload = proto::bytes_field(4, kv);
  const auto ev = CursorResponseParser::classify_agent_payload(payload);
  EXPECT_EQ(ev.kind, AgentStreamEvent::Kind::kKv);
  EXPECT_FALSE(ev.kv_message.empty());
}

TEST(CursorProviderTest, KvSetThenGetRoundTrip) {
  CursorKv store;
  const auto set_args =
      proto::bytes_field(1, "k1") + proto::bytes_field(2, "payload");
  const auto set_msg =
      proto::varint_field(1, 3) + proto::bytes_field(3, set_args);
  const auto set_reply = store.handle(set_msg);
  const auto set_top = proto::parse_fields(set_reply);
  ASSERT_EQ(set_top.size(), 1u);
  EXPECT_EQ(set_top[0].num, 3u);  // AgentClientMessage.kv_client_message

  const auto get_msg =
      proto::varint_field(1, 4) + proto::bytes_field(2, proto::bytes_field(1, "k1"));
  const auto get_reply = store.handle(get_msg);
  const auto get_top = proto::parse_fields(get_reply);
  ASSERT_EQ(get_top.size(), 1u);
  EXPECT_EQ(get_top[0].num, 3u);
  const auto client = proto::parse_fields(get_top[0].bytes);
  bool saw_data = false;
  for (const auto& f : client) {
    if (f.num != 2) continue;
    const auto result = proto::parse_fields(f.bytes);
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result[0].num, 1u);
    EXPECT_EQ(result[0].bytes, "payload");
    saw_data = true;
  }
  EXPECT_TRUE(saw_data);
}

TEST(CursorProviderTest, ClassifiesRequestContextExec) {
  const auto exec = proto::varint_field(1, 1) + proto::bytes_field(10, std::string{});
  const auto payload = proto::bytes_field(2, exec);
  const auto ev = CursorResponseParser::classify_agent_payload(payload);
  EXPECT_EQ(ev.kind, AgentStreamEvent::Kind::kRequestContext);
  EXPECT_EQ(ev.exec_id, 1u);
}

TEST(CursorProviderTest, ExecNonShellToolsAreRejected) {
  CursorExecRequest write_req;
  write_req.id = 4;
  write_req.exec_id = "w1";
  write_req.args_field = 3;
  write_req.args = proto::bytes_field(1, "note.txt") + proto::bytes_field(2, "x");
  const auto write = CursorExec::handle(write_req, "/tmp");
  EXPECT_TRUE(write.is_error);
  EXPECT_EQ(write.tool_name, "write");
  ASSERT_TRUE(write.result.contains("error"));
  EXPECT_NE(write.result["error"].get<std::string>().find("only bash"),
            std::string::npos);
  ASSERT_FALSE(write.client_messages.empty());

  CursorExecRequest read_req;
  read_req.id = 5;
  read_req.exec_id = "r1";
  read_req.args_field = 7;
  const auto read = CursorExec::handle(read_req, "/tmp");
  EXPECT_TRUE(read.is_error);
  EXPECT_EQ(read.tool_name, "read");
}

TEST(CursorProviderTest, ExecHookReplySetsMatchingCase) {
  CursorExecRequest req;
  req.id = 2;
  req.exec_id = "hook-1";
  req.args_field = 27;
  req.hook_request_field = 4;  // pre_tool_use
  const auto reply = CursorExec::handle(req, ".");
  ASSERT_EQ(reply.client_messages.size(), 1u);
  const auto top = proto::parse_fields(reply.client_messages.front());
  ASSERT_FALSE(top.empty());
  EXPECT_EQ(top[0].num, 2u);  // AgentClientMessage.exec_client_message
  const auto exec = proto::parse_fields(top[0].bytes);
  bool saw_hook_result = false;
  for (const auto& f : exec) {
    if (f.num == 27) {
      saw_hook_result = true;
      const auto result = proto::parse_fields(f.bytes);
      ASSERT_FALSE(result.empty());
      EXPECT_EQ(result[0].num, 1u);  // ExecuteHookResult.response
      const auto response = proto::parse_fields(result[0].bytes);
      ASSERT_FALSE(response.empty());
      EXPECT_EQ(response[0].num, 4u);  // pre_tool_use
    }
  }
  EXPECT_TRUE(saw_hook_result);
}

TEST(CursorProviderTest, ExecShellStreamStartSetsSandboxPolicy) {
  CursorExecRequest req;
  req.id = 3;
  req.exec_id = "stream-1";
  req.args_field = 14;
  const auto msg = CursorExec::shell_stream_start_message(req);
  const auto top = proto::parse_fields(msg);
  ASSERT_FALSE(top.empty());
  EXPECT_EQ(top[0].num, 2u);
  const auto exec = proto::parse_fields(top[0].bytes);
  bool saw_stream = false;
  for (const auto& f : exec) {
    if (f.num != 14) continue;
    saw_stream = true;
    const auto stream = proto::parse_fields(f.bytes);
    ASSERT_FALSE(stream.empty());
    EXPECT_EQ(stream[0].num, 4u);  // start
    const auto start = proto::parse_fields(stream[0].bytes);
    ASSERT_FALSE(start.empty());
    EXPECT_EQ(start[0].num, 1u);  // sandbox_policy
    const auto policy = proto::parse_fields(start[0].bytes);
    ASSERT_FALSE(policy.empty());
    EXPECT_EQ(policy[0].num, 1u);
    EXPECT_EQ(policy[0].varint, 1u);  // TYPE_INSECURE_NONE
  }
  EXPECT_TRUE(saw_stream);
}

TEST(CursorProviderTest, ExecShellStreamAlsoSendsUnaryShellResult) {
  CursorExecRequest req;
  req.id = 2;
  req.exec_id = "stream-unary";
  req.args_field = 14;
  req.args = proto::bytes_field(1, "echo STREAM_UNARY_OK");
  const auto reply = CursorExec::handle(req, ".");
  ASSERT_GE(reply.client_messages.size(), 2u);
  bool saw_stream_exit = false;
  bool saw_shell_result = false;
  for (const auto& msg : reply.client_messages) {
    const auto top = proto::parse_fields(msg);
    ASSERT_FALSE(top.empty());
    EXPECT_EQ(top[0].num, 2u);
    const auto exec = proto::parse_fields(top[0].bytes);
    for (const auto& f : exec) {
      if (f.num == 14) {
        const auto stream = proto::parse_fields(f.bytes);
        for (const auto& ev : stream) {
          if (ev.num == 3) saw_stream_exit = true;
        }
      }
      if (f.num == 2) saw_shell_result = true;
    }
  }
  EXPECT_TRUE(saw_stream_exit);
  EXPECT_TRUE(saw_shell_result);
  EXPECT_NE(reply.result.value("output", "").find("STREAM_UNARY_OK"),
            std::string::npos);
}

TEST(CursorProviderTest, ExecShellStreamStartEchoesRequestedPolicy) {
  CursorExecRequest req;
  req.id = 9;
  req.args_field = 14;
  const auto policy = proto::varint_field(1, 2);  // WORKSPACE_READWRITE
  req.args = proto::bytes_field(1, "true") + proto::bytes_field(9, policy);
  const auto msg = CursorExec::shell_stream_start_message(req);
  const auto top = proto::parse_fields(msg);
  ASSERT_FALSE(top.empty());
  const auto exec = proto::parse_fields(top[0].bytes);
  bool echoed = false;
  for (const auto& f : exec) {
    if (f.num != 14) continue;
    const auto stream = proto::parse_fields(f.bytes);
    ASSERT_FALSE(stream.empty());
    const auto start = proto::parse_fields(stream[0].bytes);
    ASSERT_FALSE(start.empty());
    const auto sent = proto::parse_fields(start[0].bytes);
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(sent[0].num, 1u);
    EXPECT_EQ(sent[0].varint, 2u);
    echoed = true;
  }
  EXPECT_TRUE(echoed);
}

TEST(CursorProviderTest, ExecStreamCloseIsControlMessage) {
  const auto msg = CursorExec::stream_close_message(4);
  const auto top = proto::parse_fields(msg);
  ASSERT_EQ(top.size(), 1u);
  EXPECT_EQ(top[0].num, 5u);  // AgentClientMessage.exec_client_control_message
  const auto control = proto::parse_fields(top[0].bytes);
  ASSERT_EQ(control.size(), 1u);
  EXPECT_EQ(control[0].num, 1u);  // stream_close
  const auto close = proto::parse_fields(control[0].bytes);
  ASSERT_EQ(close.size(), 1u);
  EXPECT_EQ(close[0].num, 1u);
  EXPECT_EQ(close[0].varint, 4u);
}

TEST(CursorProviderTest, ExecPiBashEchoRunsLocally) {
  CursorExecRequest req;
  req.id = 11;
  req.exec_id = "bash-1";
  req.args_field = 46;
  req.args = proto::bytes_field(1, "echo CURSOR_EXEC_OK");
  const auto reply = CursorExec::handle(req, ".");
  EXPECT_EQ(reply.tool_name, "bash");
  ASSERT_FALSE(reply.client_messages.empty());
  EXPECT_NE(reply.result.value("output", "").find("CURSOR_EXEC_OK"),
            std::string::npos);
}

TEST(CursorProviderTest, ExecAllowlistIsTrue) {
  CursorExecRequest req;
  req.id = 8;
  req.args_field = 41;
  const auto reply = CursorExec::handle(req, ".");
  ASSERT_EQ(reply.client_messages.size(), 1u);
  const auto top = proto::parse_fields(reply.client_messages.front());
  const auto exec = proto::parse_fields(top[0].bytes);
  bool saw = false;
  for (const auto& f : exec) {
    if (f.num == 41) {
      saw = true;
      const auto body = proto::parse_fields(f.bytes);
      ASSERT_FALSE(body.empty());
      EXPECT_EQ(body[0].num, 1u);
      EXPECT_EQ(body[0].varint, 1u);
    }
  }
  EXPECT_TRUE(saw);
}

TEST(CursorProviderTest, ThinkingPauseDoesNotIdleEndAfterTwelveSeconds) {
  const auto now = std::chrono::steady_clock::now();
  // Grok 4.6 regularly thinks 30–90s between visible tokens. The old 12s
  // heartbeat idle-end cut those turns short.
  EXPECT_FALSE(idle_end_on_heartbeat(
      /*has_text=*/true, /*saw_exec=*/false, now - std::chrono::seconds(12),
      now));
  EXPECT_FALSE(idle_end_on_heartbeat(
      /*has_text=*/true, /*saw_exec=*/false, now - std::chrono::seconds(179),
      now));
  EXPECT_TRUE(idle_end_on_heartbeat(
      /*has_text=*/true, /*saw_exec=*/false, now - std::chrono::seconds(180),
      now));
}

TEST(CursorProviderTest, ConnectHeartbeatIsNotTurnEnded) {
  // Connect keepalive: field 1 = "j\0" (0a 02 6a 00). Must stay heartbeat so
  // the client can keep the native agent tool loop open across keepalives.
  const std::string heartbeat("\x0a\x02\x6a\x00", 4);
  const auto ev = CursorResponseParser::classify_agent_payload(heartbeat);
  EXPECT_EQ(ev.kind, AgentStreamEvent::Kind::kHeartbeat);
}

TEST(CursorProviderTest, MultiTurnPromptIncludesPriorTurns) {
  GenerateOptions options;
  options.model = "composer-2.5";
  options.session_id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
  options.system = "Be helpful.";
  options.messages = {
      Message::user("First question"),
      Message::assistant("First answer"),
      Message::user("Follow up"),
  };

  CursorRequestBuilder builder;
  const auto request = builder.build_agent_run_request(options);

  EXPECT_NE(request.find("First question"), std::string::npos);
  EXPECT_NE(request.find("First answer"), std::string::npos);
  EXPECT_NE(request.find("Follow up"), std::string::npos);
}

TEST(CursorProviderTest, BidiSessionShutsDownWithoutWork) {
  CursorBidi bidi("https://example.invalid", "token", "req", "cli-test");
  EXPECT_TRUE(bidi.is_ok());
  EXPECT_TRUE(bidi.flush());
}

TEST(CursorProviderTest, ResolvesFromProviderRegistryWithToken) {
  providers::register_authenticated_providers();
  providers::ProviderOptions options;
  options.api_key = "test-cursor-token";
  auto res = providers::ProviderRegistry::instance().resolve("cursor", options);
  EXPECT_TRUE(res.ok()) << res.error;
  EXPECT_TRUE(res.client.is_valid());
  EXPECT_EQ(res.client.tool_execution_model(), ToolExecutionModel::ServerSideDuplex);
}

TEST(CursorProviderTest, RemapsAndResolvesCursorModelSlugs) {
  // Test grok-4.6 remapping to cursor-grok-4.6
  EXPECT_EQ(ProviderTransform::cursor_picker_id("grok-4.6"), "cursor-grok-4.6");
  EXPECT_EQ(ProviderTransform::cursor_picker_id("cursor-grok-4.6"), "cursor-grok-4.6");
  EXPECT_EQ(ProviderTransform::cursor_picker_id("composer-2.5"), "composer-2.5");

  // Wire slugs with different reasoning efforts
  EXPECT_EQ(ProviderTransform::cursor_wire_model_id("cursor-grok-4.6", "low"), "cursor-grok-4.6-low");
  EXPECT_EQ(ProviderTransform::cursor_wire_model_id("cursor-grok-4.6", "medium"), "cursor-grok-4.6-medium");
  EXPECT_EQ(ProviderTransform::cursor_wire_model_id("cursor-grok-4.6", "high"), "cursor-grok-4.6-high");
  EXPECT_EQ(ProviderTransform::cursor_wire_model_id("cursor-grok-4.6", "max"), "cursor-grok-4.6-xhigh");
  EXPECT_EQ(ProviderTransform::cursor_wire_model_id("composer-2.5", "high"), "composer-2.5-high");
}

TEST(CursorProviderTest, ExecGrepIsDisabled) {
  CursorExecRequest req;
  req.id = 2;
  req.exec_id = "grep-empty";
  req.args_field = 5;
  req.args = proto::bytes_field(1, "CursorExec") +
             proto::bytes_field(2, "/tmp");
  const auto reply = CursorExec::handle(req, "/tmp");
  EXPECT_TRUE(reply.is_error);
  EXPECT_EQ(reply.tool_name, "grep");
  ASSERT_TRUE(reply.result.contains("error"));
  EXPECT_NE(reply.result["error"].get<std::string>().find("only bash"),
            std::string::npos);
}

TEST(CursorProviderTest, ExecMcpCanInvokeTaskTool) {
  bool called = false;
  GenerateOptions options;
  options.subagent_runner = [&](const nlohmann::json& args,
                                std::shared_ptr<std::atomic<bool>>) {
    called = true;
    EXPECT_EQ(args.value("prompt", ""), "find TODOs");
    return nlohmann::json{{"output", "found none"}};
  };

  CursorExecRequest req;
  req.id = 9;
  req.exec_id = "task-1";
  req.args_field = 11;
  req.args = proto::bytes_field(1, "task") +
             proto::bytes_field(2, R"({"prompt":"find TODOs","description":"scan"})");

  const auto reply = CursorExec::handle(req, "/tmp", nullptr, &options);
  EXPECT_TRUE(called);
  EXPECT_FALSE(reply.is_error);
  EXPECT_EQ(reply.tool_name, "task");
  EXPECT_NE(reply.result.value("output", "").find("found none"),
            std::string::npos);
  ASSERT_FALSE(reply.client_messages.empty());
}

TEST(CursorProviderTest, ExecMcpProtobufTaskArgsInvokeRunner) {
  bool called = false;
  GenerateOptions options;
  options.subagent_runner = [&](const nlohmann::json& args,
                                std::shared_ptr<std::atomic<bool>>) {
    called = true;
    EXPECT_EQ(args.value("prompt", ""), "summarize README");
    EXPECT_EQ(args.value("description", ""), "readme");
    EXPECT_EQ(args.value("subagent_type", ""), "explore");
    return nlohmann::json{{"output", "ok proto"}};
  };

  const std::string task_args = proto::bytes_field(1, "readme") +
                                proto::bytes_field(2, "summarize README") +
                                proto::bytes_field(3, "explore");
  CursorExecRequest req;
  req.id = 10;
  req.exec_id = "task-proto";
  req.args_field = 11;
  req.args = proto::bytes_field(1, "task") + proto::bytes_field(2, task_args);

  const auto reply = CursorExec::handle(req, "/tmp", nullptr, &options);
  EXPECT_TRUE(called);
  EXPECT_FALSE(reply.is_error);
  EXPECT_EQ(reply.tool_name, "task");
  EXPECT_NE(reply.result.value("output", "").find("ok proto"),
            std::string::npos);
}

TEST(CursorProviderTest, ExecUnknownFieldProtobufTaskDoesNotReturnEmptyError) {
  bool called = false;
  GenerateOptions options;
  options.subagent_runner = [&](const nlohmann::json& args,
                                std::shared_ptr<std::atomic<bool>>) {
    called = true;
    EXPECT_EQ(args.value("prompt", ""), "list cmake targets");
    return nlohmann::json{{"output", "libqcode"}};
  };

  CursorExecRequest req;
  req.id = 11;
  req.exec_id = "task-unknown";
  req.args_field = 16;  // not a mapped bash/file tool
  req.args = proto::bytes_field(1, "cmake") +
             proto::bytes_field(2, "list cmake targets") +
             proto::bytes_field(3, "explore");

  const auto reply = CursorExec::handle(req, "/tmp", nullptr, &options);
  EXPECT_TRUE(called);
  EXPECT_FALSE(reply.is_error);
  EXPECT_EQ(reply.tool_name, "task");
  EXPECT_NE(reply.result.value("output", "").find("libqcode"),
            std::string::npos);
}

TEST(CursorProviderTest, ExecTaskEmptyRunnerErrorIsNonEmpty) {
  GenerateOptions options;
  options.subagent_runner = [&](const nlohmann::json&,
                                std::shared_ptr<std::atomic<bool>>) {
    return nlohmann::json{{"error", ""}};
  };

  CursorExecRequest req;
  req.id = 12;
  req.exec_id = "task-empty-err";
  req.args_field = 11;
  req.args = proto::bytes_field(1, "task") +
             proto::bytes_field(2, R"({"prompt":"ping"})");

  const auto reply = CursorExec::handle(req, "/tmp", nullptr, &options);
  EXPECT_TRUE(reply.is_error);
  ASSERT_TRUE(reply.result.contains("error"));
  EXPECT_FALSE(reply.result.value("error", "").empty());
}

TEST(CursorProviderTest, ExecWriteIsNotStolenAsTask) {
  bool called = false;
  GenerateOptions options;
  options.subagent_runner = [&](const nlohmann::json&,
                                std::shared_ptr<std::atomic<bool>>) {
    called = true;
    return nlohmann::json{{"output", "should not run"}};
  };

  CursorExecRequest req;
  req.id = 4;
  req.exec_id = "w-task";
  req.args_field = 3;
  req.args = proto::bytes_field(1, "note.txt") +
             proto::bytes_field(2, "file body that looks like a prompt");
  const auto reply = CursorExec::handle(req, "/tmp", nullptr, &options);
  EXPECT_FALSE(called);
  EXPECT_TRUE(reply.is_error);
  EXPECT_EQ(reply.tool_name, "write");
}

}  // namespace
}  // namespace cursor
}  // namespace qcode

