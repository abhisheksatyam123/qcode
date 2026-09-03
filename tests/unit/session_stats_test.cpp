#include <qcode/session/session_store.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace qcode {
namespace {

using namespace qcode::session;

// Exercises the persisted-token-stats path end to end. Uses an isolated temp
// DB via QCODE_DB_PATH so it leaves the developer's real ~/.qcode.db alone.
class SessionStatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Point the session store at a fresh temp database.
        std::error_code ec;
        std::filesystem::create_directories("/tmp/qcode_stats_test", ec);
        db_path_ = "/tmp/qcode_stats_test/test.db";
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(db_path_ + "-wal", ec);
        std::filesystem::remove(db_path_ + "-shm", ec);
        setenv("QCODE_DB_PATH", db_path_.c_str(), 1);
        // init_database runs migrations (v5 adds token columns).
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

TEST_F(SessionStatsTest, PersistedTokensAccumulateAndAreReadBack) {
    const std::string sid =
        create_new_session("prov", "model", "ws", "Test Session");
    ASSERT_FALSE(sid.empty());

    // Simulate two turns. Each TokenUsageUpdated is per-turn absolute; we
    // persist the per-turn delta exactly as AppStore/Server do.
    persist_session_token_stats(sid, /*prompt*/ 100, /*completion*/ 20, /*total*/ 120);
    persist_session_token_stats(sid, /*prompt*/ 50, /*completion*/ 10, /*total*/ 60);

    SessionStats st = get_session_stats(sid);
    EXPECT_EQ(st.prompt_tokens, 150);
    EXPECT_EQ(st.completion_tokens, 30);
    EXPECT_EQ(st.total_tokens, 180);
}

TEST_F(SessionStatsTest, LiveTokensAddedOnTopOfStoredNoDoubleCount) {
    const std::string sid = create_new_session("prov", "model", "ws", "S2");

    persist_session_token_stats(sid, 100, 20, 120);

    // The stats route passes the live token args as 0 (DB already holds the
    // cumulative total). get_session_stats must NOT re-add the stored value.
    SessionStats st = get_session_stats(sid, /*live_tool_calls*/ 0,
                                        /*live_tool_time_ms*/ 0.0,
                                        /*live_prompt*/ 0,
                                        /*live_completion*/ 0,
                                        /*live_total*/ 0);
    EXPECT_EQ(st.prompt_tokens, 100);
    EXPECT_EQ(st.total_tokens, 120);

    // If a caller genuinely passes live (in-flight) token counts, they are added
    // on top of the stored cumulative total.
    SessionStats st2 = get_session_stats(sid, 0, 0.0, 5, 1, 6);
    EXPECT_EQ(st2.prompt_tokens, 105);
    EXPECT_EQ(st2.total_tokens, 126);
}

}  // namespace
}  // namespace qcode

