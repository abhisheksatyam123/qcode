#include <gtest/gtest.h>
#include <bus_json_codec.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>

namespace qcode {
namespace server {
namespace {

using namespace qcode::contract;

TEST(BusJsonCodecTest, SerializeMessageDelta) {
    MessageDelta::Payload payload{"sess-123", "Hello, world!", true};
    auto json_opt = serialize_event(MessageDelta::type, payload, std::type_index(typeid(MessageDelta::Payload)));
    ASSERT_TRUE(json_opt.has_value());
    const auto& j = *json_opt;
    EXPECT_EQ(j["type"], MessageDelta::type);
    EXPECT_EQ(j["session_id"], "sess-123");
    EXPECT_EQ(j["text"], "Hello, world!");
    EXPECT_EQ(j["done"], true);
}

TEST(BusJsonCodecTest, SerializeToolCallEvents) {
    ToolCallStarted::Payload start_payload{"sess-1", "call-abc", "bash", "{\"command\":\"ls\"}"};
    auto start_json = serialize_event(ToolCallStarted::type, start_payload, std::type_index(typeid(ToolCallStarted::Payload)));
    ASSERT_TRUE(start_json.has_value());
    EXPECT_EQ((*start_json)["tool_call_id"], "call-abc");
    EXPECT_EQ((*start_json)["tool_name"], "bash");

    ToolCallCompleted::Payload comp_payload{"sess-1", "call-abc", "bash", "file.txt\n", false, 42};
    auto comp_json = serialize_event(ToolCallCompleted::type, comp_payload, std::type_index(typeid(ToolCallCompleted::Payload)));
    ASSERT_TRUE(comp_json.has_value());
    EXPECT_EQ((*comp_json)["tool_call_id"], "call-abc");
    EXPECT_EQ((*comp_json)["result"], "file.txt\n");
    EXPECT_EQ((*comp_json)["is_error"], false);
    EXPECT_EQ((*comp_json)["duration_ms"], 42);
}

TEST(BusJsonCodecTest, SerializeStatusAndError) {
    SessionStatusChanged::Payload stat_payload{"sess-1", "busy"};
    auto stat_json = serialize_event(SessionStatusChanged::type, stat_payload, std::type_index(typeid(SessionStatusChanged::Payload)));
    ASSERT_TRUE(stat_json.has_value());
    EXPECT_EQ((*stat_json)["status"], "busy");

    ErrorOccurred::Payload err_payload{"sess-1", "rate limit exceeded", "error"};
    auto err_json = serialize_event(ErrorOccurred::type, err_payload, std::type_index(typeid(ErrorOccurred::Payload)));
    ASSERT_TRUE(err_json.has_value());
    EXPECT_EQ((*err_json)["message"], "rate limit exceeded");
    EXPECT_EQ((*err_json)["severity"], "error");
}

TEST(BusJsonCodecTest, SerializeTokenUsageAndReasoning) {
    TokenUsageUpdated::Payload usage_payload{100, 50, 150, 20};
    auto usage_json = serialize_event(TokenUsageUpdated::type, usage_payload, std::type_index(typeid(TokenUsageUpdated::Payload)));
    ASSERT_TRUE(usage_json.has_value());
    EXPECT_EQ((*usage_json)["prompt_tokens"], 100);
    EXPECT_EQ((*usage_json)["completion_tokens"], 50);
    EXPECT_EQ((*usage_json)["total_tokens"], 150);
    EXPECT_EQ((*usage_json)["cached_prompt_tokens"], 20);

    ReasoningDelta::Payload reasoning_payload{"sess-1", "Thinking step 1...", "sig-xyz", false};
    auto reasoning_json = serialize_event(ReasoningDelta::type, reasoning_payload, std::type_index(typeid(ReasoningDelta::Payload)));
    ASSERT_TRUE(reasoning_json.has_value());
    EXPECT_EQ((*reasoning_json)["text"], "Thinking step 1...");
    EXPECT_EQ((*reasoning_json)["signature"], "sig-xyz");
    EXPECT_EQ((*reasoning_json)["done"], false);
}

TEST(BusJsonCodecTest, SerializeUnknownEventReturnsNullopt) {
    auto res = serialize_event("unknown.event", 42, std::type_index(typeid(int)));
    EXPECT_FALSE(res.has_value());
}

TEST(BusJsonCodecTest, DispatchPromptSubmitted) {
    qcode::bus::BusRuntime bus;
    bus.register_event<PromptSubmitted>();

    bool received = false;
    std::string text_received;
    std::vector<std::string> paths_received;

    auto sub = bus.subscribe<PromptSubmitted>([&](const PromptSubmitted::Payload& p) {
        received = true;
        text_received = p.text;
        paths_received = p.attachment_paths;
    });

    nlohmann::json msg = {
        {"type", PromptSubmitted::type},
        {"text", "Write a test"},
        {"attachment_paths", {"/path/one", "/path/two"}}
    };

    EXPECT_TRUE(dispatch_json(msg, bus));
    bus.drain();
    EXPECT_TRUE(received);
    EXPECT_EQ(text_received, "Write a test");
    ASSERT_EQ(paths_received.size(), 2u);
    EXPECT_EQ(paths_received[0], "/path/one");
}

TEST(BusJsonCodecTest, DispatchModelAndSessionSelected) {
    qcode::bus::BusRuntime bus;
    bus.register_event<ModelSelected>();
    bus.register_event<SessionSelected>();

    std::string provider_received;
    std::string model_received;
    auto sub1 = bus.subscribe<ModelSelected>([&](const ModelSelected::Payload& p) {
        provider_received = p.provider_name;
        model_received = p.model_id;
    });

    std::string session_received;
    auto sub2 = bus.subscribe<SessionSelected>([&](const SessionSelected::Payload& p) {
        session_received = p.session_id;
    });

    nlohmann::json model_msg = {
        {"type", ModelSelected::type},
        {"provider_name", "anthropic"},
        {"model_id", "claude-3-7-sonnet"}
    };
    EXPECT_TRUE(dispatch_json(model_msg, bus));
    bus.drain();
    EXPECT_EQ(provider_received, "anthropic");
    EXPECT_EQ(model_received, "claude-3-7-sonnet");

    nlohmann::json sess_msg = {
        {"type", SessionSelected::type},
        {"session_id", "session-42"}
    };
    EXPECT_TRUE(dispatch_json(sess_msg, bus));
    bus.drain();
    EXPECT_EQ(session_received, "session-42");
}

TEST(BusJsonCodecTest, DispatchInvalidTypeReturnsFalse) {
    qcode::bus::BusRuntime bus;
    nlohmann::json no_type = {{"foo", "bar"}};
    EXPECT_FALSE(dispatch_json(no_type, bus));

    nlohmann::json unknown_type = {{"type", "nonexistent.type"}};
    EXPECT_FALSE(dispatch_json(unknown_type, bus));
}

} // namespace
} // namespace server
} // namespace qcode
