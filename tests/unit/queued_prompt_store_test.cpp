// Round-trip coverage for the persisted per-session prompt queue: enqueue →
// restart simulation (fresh read) → pop/remove/clear lifecycle.

#include <qcode/session/session_store.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <unistd.h>
#include <string>

namespace qcode {
namespace {

using namespace qcode::session;

class QueuedPromptStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        std::filesystem::create_directories("/tmp/qcode_queue_test", ec);
        db_path_ = "/tmp/qcode_queue_test/test_" +
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

TEST_F(QueuedPromptStoreTest, AddAndLoadPreservesFifoOrder) {
    const std::string sid = create_new_session("prov", "model", "ws");
    ASSERT_FALSE(sid.empty());

    queued_prompt_add(sid, "first");
    queued_prompt_add(sid, "second");
    queued_prompt_add(sid, "third");

    const auto loaded = queued_prompt_load(sid);
    ASSERT_EQ(loaded.size(), 3u);
    EXPECT_EQ(loaded[0], "first");
    EXPECT_EQ(loaded[1], "second");
    EXPECT_EQ(loaded[2], "third");
}

TEST_F(QueuedPromptStoreTest, PopDropsOldestOnly) {
    const std::string sid = create_new_session("prov", "model", "ws");
    queued_prompt_add(sid, "a");
    queued_prompt_add(sid, "b");

    queued_prompt_pop(sid);

    const auto loaded = queued_prompt_load(sid);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0], "b");

    // Popping an empty queue is a no-op.
    queued_prompt_pop(sid);
    queued_prompt_pop(sid);
    EXPECT_TRUE(queued_prompt_load(sid).empty());
}

TEST_F(QueuedPromptStoreTest, RemoveAtDeletesPositionalEntry) {
    const std::string sid = create_new_session("prov", "model", "ws");
    queued_prompt_add(sid, "one");
    queued_prompt_add(sid, "two");
    queued_prompt_add(sid, "three");

    // 1-based positional removal.
    queued_prompt_remove_at(sid, 2);
    auto loaded = queued_prompt_load(sid);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0], "one");
    EXPECT_EQ(loaded[1], "three");

    // Out-of-range is a no-op.
    queued_prompt_remove_at(sid, 9);
    EXPECT_EQ(queued_prompt_load(sid).size(), 2u);
}

TEST_F(QueuedPromptStoreTest, ClearAndSessionIsolation) {
    const std::string sid_a = create_new_session("prov", "model", "ws");
    const std::string sid_b = create_new_session("prov", "model", "ws");

    queued_prompt_add(sid_a, "a1");
    queued_prompt_add(sid_b, "b1");
    queued_prompt_clear(sid_a);

    EXPECT_TRUE(queued_prompt_load(sid_a).empty());
    ASSERT_EQ(queued_prompt_load(sid_b).size(), 1u);
    EXPECT_EQ(queued_prompt_load(sid_b)[0], "b1");
}

TEST_F(QueuedPromptStoreTest, RowsSurviveReopen) {
    const std::string sid = create_new_session("prov", "model", "ws");
    queued_prompt_add(sid, "survives restart");
    queued_prompt_add(sid, "me too");

    // Simulate restart: re-run migrations/open against the same file.
    init_database();

    const auto loaded = queued_prompt_load(sid);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0], "survives restart");
    EXPECT_EQ(loaded[1], "me too");
}

TEST_F(QueuedPromptStoreTest, AppendLastUpdatesNewestRow) {
    const std::string sid = create_new_session("prov", "model", "ws");
    queued_prompt_add(sid, "first prompt");
    queued_prompt_append_last(sid, "additional context");

    auto loaded = queued_prompt_load(sid);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0], "first prompt\nadditional context");

    // Adding second prompt, then appending again only affects the newest
    queued_prompt_add(sid, "second prompt");
    queued_prompt_append_last(sid, "more for second");

    loaded = queued_prompt_load(sid);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0], "first prompt\nadditional context");
    EXPECT_EQ(loaded[1], "second prompt\nmore for second");
}

TEST_F(QueuedPromptStoreTest, DeletingSessionCascadesQueueRows) {
    const std::string sid = create_new_session("prov", "model", "ws");
    queued_prompt_add(sid, "doomed");
    delete_session(sid);
    EXPECT_TRUE(queued_prompt_load(sid).empty());
}

}  // namespace
}  // namespace qcode
