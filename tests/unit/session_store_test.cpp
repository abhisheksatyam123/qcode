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

}  // namespace
}  // namespace session
}  // namespace qcode
