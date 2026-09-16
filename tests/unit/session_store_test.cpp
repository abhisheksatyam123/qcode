#include <qcode/session/session_store.h>
#include <qcode/core/message.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <unistd.h>
#include <string>

namespace qcode {
namespace session {
namespace {

class SessionStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        std::filesystem::create_directories("/tmp/qcode_session_store_test", ec);
        db_path_ = "/tmp/qcode_session_store_test/test_" +
                   std::to_string(::getpid()) + "_" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                   ".db";
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(db_path_ + "-wal", ec);
        std::filesystem::remove(db_path_ + "-shm", ec);
        setenv("QCODE_DB_PATH", db_path_.c_str(), 1);
        init_database();
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

TEST_F(SessionStoreTest, CreateListRenameDelete) {
    const std::string sid =
        create_new_session("prov", "model", "/ws", "First");
    ASSERT_FALSE(sid.empty());
    EXPECT_TRUE(is_valid_session_id(sid));
    EXPECT_EQ(get_session_title(sid), "First");
    EXPECT_EQ(get_session_workspace(sid), "/ws");

    rename_session(sid, "Renamed");
    EXPECT_EQ(get_session_title(sid), "Renamed");

    set_session_provider_model(sid, "openai", "gpt-4");
    auto list = list_sessions_full();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].provider, "openai");
    EXPECT_EQ(list[0].model, "gpt-4");

    delete_session(sid);
    EXPECT_TRUE(list_sessions_full().empty());
}

TEST_F(SessionStoreTest, SaveAndLoadMessages) {
    const std::string sid = create_new_session("prov", "model", "/ws");
    save_message(sid, "User", "hello");
    save_message(sid, "Assistant", "world");
    auto msgs = load_session_messages(sid);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[0].first, "User");
    EXPECT_EQ(msgs[0].second, "hello");
    EXPECT_EQ(msgs[1].first, "Assistant");
    EXPECT_EQ(msgs[1].second, "world");

    qcode::Messages overwrite;
    overwrite.push_back(qcode::Message::user("only"));
    overwrite_session_history(sid, overwrite);
    auto parsed = load_session_history_parsed(sid);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].get_text(), "only");
}

TEST_F(SessionStoreTest, InvalidSessionIdRejected) {
    EXPECT_FALSE(is_valid_session_id(""));
    EXPECT_FALSE(is_valid_session_id(std::string(256, 'a')));
    EXPECT_FALSE(is_valid_session_id("has/slash"));
    EXPECT_TRUE(is_valid_session_id("plain-id"));
}


TEST_F(SessionStoreTest, RoundTripToolTurnKeepsTextAndThoughtSignature) {
    const std::string sid = create_new_session("google", "gemini-3.1-pro", "/ws");
    qcode::Messages original;
    original.push_back(qcode::Message::user("list files"));
    qcode::ToolCallContentPart call{"call_1", "bash",
                                    nlohmann::json{{"command", "ls"}},
                                    "thought-sig"};
    original.push_back(qcode::Message::assistant_with_tools("I'll list them", {call}));
    original.push_back(qcode::Message::tool_results(
        {{"call_1", nlohmann::json{{"output", "a.cpp"}}, false}}));
    overwrite_session_history(sid, original);

    auto parsed = load_session_history_parsed(sid);
    ASSERT_EQ(parsed.size(), 3u);
    EXPECT_EQ(parsed[0].get_text(), "list files");
    EXPECT_EQ(parsed[1].get_text(), "I'll list them");
    ASSERT_TRUE(parsed[1].has_tool_calls());
    ASSERT_EQ(parsed[1].get_tool_calls().size(), 1u);
    EXPECT_EQ(parsed[1].get_tool_calls()[0].id, "call_1");
    EXPECT_EQ(parsed[1].get_tool_calls()[0].thought_signature, "thought-sig");
    ASSERT_TRUE(parsed[2].has_tool_results());
    EXPECT_EQ(parsed[2].get_tool_results()[0].tool_call_id, "call_1");
}

TEST_F(SessionStoreTest, MergesParallelToolCallsIntoOneAssistantTurn) {
    const std::string sid = create_new_session("opencode", "muse-spark", "/ws");
    qcode::Messages original;
    original.push_back(qcode::Message::user("do both"));
    original.push_back(qcode::Message::assistant_with_tools(
        "", {{"c1", "bash", nlohmann::json{{"command", "ls"}}}}));
    original.push_back(qcode::Message::assistant_with_tools(
        "", {{"c2", "bash", nlohmann::json{{"command", "pwd"}}}}));
    overwrite_session_history(sid, original);
    auto parsed = load_session_history_parsed(sid);
    ASSERT_EQ(parsed.size(), 2u);
    ASSERT_TRUE(parsed[1].has_tool_calls());
    EXPECT_EQ(parsed[1].get_tool_calls().size(), 2u);
}

TEST_F(SessionStoreTest, RoundTripAssistantReasoningPreserved) {
    const std::string sid = create_new_session("opencode", "muse-spark", "/ws");
    qcode::Messages original;
    original.push_back(qcode::Message::user("solve this"));
    original.push_back(qcode::Message::assistant_with_reasoning(
        "42 is the answer", "deep thinking about life"));
    overwrite_session_history(sid, original);

    auto parsed = load_session_history_parsed(sid);
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].get_text(), "solve this");
    EXPECT_EQ(parsed[1].get_text(), "42 is the answer");
    EXPECT_TRUE(parsed[1].has_reasoning());
    EXPECT_EQ(parsed[1].get_reasoning(), "deep thinking about life");
    EXPECT_EQ(parsed[1].role, qcode::kMessageRoleAssistant);

    auto msgs = load_session_messages(sid);
    bool found_reasoning = false;
    for (const auto& [sender, content] : msgs) {
        if (sender == "Reasoning") {
            found_reasoning = true;
            EXPECT_EQ(content, "deep thinking about life");
        }
        EXPECT_NE(sender, "System");
    }
    EXPECT_TRUE(found_reasoning);
}

TEST_F(SessionStoreTest, SubagentsAreScopedToParentSession) {
    const std::string parent = create_new_session("prov", "model", "/ws", "Parent Main");
    ensure_session_row("ses_child_one", "Child One", "prov", "model", "/ws", parent);
    ensure_session_row("ses_child_two", "Child Two", "prov", "model", "/ws", parent);
    ensure_session_row("ses_orphan", "Orphan", "prov", "model", "/ws", "deadbeef-dead-beef-dead-deadbeefdead");

    auto all = list_sessions_full(true);
    ASSERT_EQ(all.size(), 4u);

    auto scoped = list_sessions_full(true, parent);
    ASSERT_EQ(scoped.size(), 2u);
    for (const auto& s : scoped) {
        EXPECT_EQ(s.parent_session_id, parent);
    }

    auto children = get_child_session_ids(parent);
    ASSERT_EQ(children.size(), 2u);
}

TEST_F(SessionStoreTest, DeleteParentCascadesToChildSessions) {
    const std::string parent = create_new_session("prov", "model", "/ws", "Cascade Parent");
    ensure_session_row("ses_cascade_a", "Cascade A", "prov", "model", "/ws", parent);
    ensure_session_row("ses_cascade_b", "Cascade B", "prov", "model", "/ws", parent);
    save_message("ses_cascade_a", "User", "hello child");

    ASSERT_EQ(list_sessions_full(true).size(), 3u);

    delete_session(parent);

    auto remaining = list_sessions_full(true);
    EXPECT_TRUE(remaining.empty());
    EXPECT_TRUE(load_session_messages("ses_cascade_a").empty());
}

TEST_F(SessionStoreTest, SubagentSessionsAreFilteredFromInteractiveLists) {
    const std::string sid1 = create_new_session("prov", "model", "/ws", "Main Chat");
    ensure_session_row("ses_subagent_123", "Subagent Task", "prov", "model", "/ws");

    // get_last_active_session should ignore subagents
    EXPECT_EQ(get_last_active_session(), sid1);

    // list_sessions should exclude subagents by default
    auto primary = list_sessions_full(false);
    ASSERT_EQ(primary.size(), 1u);
    EXPECT_EQ(primary[0].id, sid1);
    EXPECT_EQ(primary[0].title, "Main Chat");

    // list_sessions(true) includes subagents
    auto all = list_sessions_full(true);
    ASSERT_EQ(all.size(), 2u);

    // subagent message history is still loadable
    save_message("ses_subagent_123", "Assistant", "done");
    auto msgs = load_session_messages("ses_subagent_123");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].second, "done");
}

}  // namespace
}  // namespace session
}  // namespace qcode
