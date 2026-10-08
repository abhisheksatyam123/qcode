// Mid-turn prompt injection: AppStore::take_queued_prompts drains the queue for
// the owning session only, and the UserMessageInjected subscriber persists the
// interrupted reply before the injected User row.

#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>
#include <qcode/session/session_store.h>
#include <qcode/ui/app_store.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace qcode {
namespace {

using namespace qcode::session;

class QueueInjectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path() / "qcode_queue_injection_test";
        std::filesystem::create_directories(dir, ec);
        db_path_ = (dir / ("test_" + std::to_string(::getpid()) + "_" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                           ".db")).string();
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

TEST_F(QueueInjectionTest, TakeReturnsPromptsInOrderAndEmptiesQueue) {
    bus::BusRuntime bus;
    contract::register_all_events(bus);
    AppStore store(bus);
    store.wire();
    const std::string sid = create_new_session("prov", "model", "ws");
    store.set_session_id(sid);

    store.enqueue_prompt("first");
    store.enqueue_prompt("second");
    store.enqueue_prompt("third");

    const auto taken = store.take_queued_prompts(sid);
    ASSERT_EQ(taken.size(), 3u);
    EXPECT_EQ(taken[0], "first");
    EXPECT_EQ(taken[1], "second");
    EXPECT_EQ(taken[2], "third");
    EXPECT_EQ(store.queue_size(), 0u);
    EXPECT_TRUE(queued_prompt_load(sid).empty());
}

TEST_F(QueueInjectionTest, TakeForDifferentSessionReturnsNothingAndKeepsQueue) {
    bus::BusRuntime bus;
    contract::register_all_events(bus);
    AppStore store(bus);
    store.wire();
    const std::string sid = create_new_session("prov", "model", "ws");
    store.set_session_id(sid);

    store.enqueue_prompt("keep me");

    EXPECT_TRUE(store.take_queued_prompts("some-other-session").empty());
    EXPECT_EQ(store.queue_size(), 1u);
    EXPECT_EQ(queued_prompt_load(sid).size(), 1u);
}

TEST_F(QueueInjectionTest, InjectedEventSavesPendingReplyThenUserRow) {
    bus::BusRuntime bus;
    contract::register_all_events(bus);
    AppStore store(bus);
    store.wire();
    const std::string sid = create_new_session("prov", "model", "ws");
    store.set_session_id(sid);

    // Streamed reply that has not yet been flushed by a done event.
    bus.publish<contract::MessageDelta>({.session_id = sid, .text = "partial reply", .done = false});
    bus.drain();
    store.append_chat_message("User", "original prompt");

    bus.publish<contract::UserMessageInjected>({.session_id = sid, .text = "injected"});
    bus.drain();

    const auto rows = load_session_messages(sid);
    ASSERT_GE(rows.size(), 2u);
    const auto n = rows.size();
    EXPECT_EQ(rows[n - 2], (std::pair<std::string, std::string>{"Assistant", "partial reply"}));
    EXPECT_EQ(rows[n - 1], (std::pair<std::string, std::string>{"User", "injected"}));

    // Live session: the injected prompt is also appended to the chat view.
    ASSERT_FALSE(store.state().messages_history->empty());
    EXPECT_EQ(store.state().messages_history->back().role, qcode::kMessageRoleUser);
}

TEST_F(QueueInjectionTest, SecondTakeReturnsEmpty) {
    bus::BusRuntime bus;
    contract::register_all_events(bus);
    AppStore store(bus);
    store.wire();
    const std::string sid = create_new_session("prov", "model", "ws");
    store.set_session_id(sid);

    store.enqueue_prompt("only once");
    ASSERT_EQ(store.take_queued_prompts(sid).size(), 1u);

    EXPECT_TRUE(store.take_queued_prompts(sid).empty());
}

}  // namespace
}  // namespace qcode
