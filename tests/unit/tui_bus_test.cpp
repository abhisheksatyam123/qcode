#include <ai/tui/bus/impl.h>
#include <ai/tui/contract/event.h>

#include <gtest/gtest.h>

#include <string>

namespace ai::tui::bus {
namespace {

using contract::MessageDelta;
using contract::ErrorOccurred;

TEST(TuiBusTest, CoalescesAdjacentMessageChunks) {
  BusRuntime bus;
  bus.register_event<MessageDelta>();

  int wake_count = 0;
  bus.set_wake_callback([&wake_count]() { ++wake_count; });

  std::string received;
  auto subscription = bus.subscribe<MessageDelta>(
      [&received](const MessageDelta::Payload& payload) {
        received += payload.text;
      });

  for (int i = 0; i < 100; ++i) {
    bus.publish<MessageDelta>({
        .session_id = "session",
        .text = "x",
        .done = false,
    });
  }

  EXPECT_EQ(bus.pending(), 1U);
  EXPECT_EQ(wake_count, 1);
  EXPECT_EQ(bus.drain(), 1U);
  EXPECT_EQ(received, std::string(100, 'x'));
}

TEST(TuiBusTest, PreservesFinalEventBoundaryAndRewakes) {
  BusRuntime bus;
  bus.register_event<MessageDelta>();

  int wake_count = 0;
  bus.set_wake_callback([&wake_count]() { ++wake_count; });

  int event_count = 0;
  bool saw_done = false;
  auto subscription = bus.subscribe<MessageDelta>(
      [&event_count, &saw_done](const MessageDelta::Payload& payload) {
        ++event_count;
        saw_done = saw_done || payload.done;
      });

  bus.publish<MessageDelta>({
      .session_id = "session",
      .text = "partial",
      .done = false,
  });
  bus.publish<MessageDelta>({
      .session_id = "session",
      .text = "",
      .done = true,
  });

  EXPECT_EQ(bus.pending(), 2U);
  EXPECT_EQ(bus.drain(), 2U);
  EXPECT_EQ(event_count, 2);
  EXPECT_TRUE(saw_done);

  bus.publish<MessageDelta>({
      .session_id = "session",
      .text = "next",
      .done = false,
  });
  EXPECT_EQ(wake_count, 2);
}

TEST(TuiBusTest, RewakesWhenDrainBatchLimitLeavesEvents) {
  BusRuntime bus;
  bus.register_event<ErrorOccurred>();

  int wake_count = 0;
  bus.set_wake_callback([&wake_count]() { ++wake_count; });
  auto subscription =
      bus.subscribe<ErrorOccurred>([](const ErrorOccurred::Payload&) {});

  for (int i = 0; i < 1025; ++i) {
    bus.publish<ErrorOccurred>({
        .session_id = "session",
        .message = "error",
        .severity = "error",
    });
  }

  EXPECT_EQ(wake_count, 1);
  EXPECT_EQ(bus.drain(), 1024U);
  EXPECT_EQ(bus.pending(), 1U);
  EXPECT_EQ(wake_count, 2);
  EXPECT_EQ(bus.drain(), 1U);
  EXPECT_EQ(bus.pending(), 0U);
}


}  // namespace
}  // namespace ai::tui::bus

#include <ai/tui/store.h>
#include <ai/tui/db.h>
#include <cstdlib>
#include <cstdio>

namespace ai::tui {
namespace {

TEST(TuiStoreTest, FormatsErrorWithoutDuplicatePrefix) {
  // Use a temporary database for the test
  const char* old_db_path = std::getenv("QCODE_DB_PATH");
  setenv("QCODE_DB_PATH", "tui_store_test_temp.db", 1);
  
  // Initialize database
  db::init_database();

  bus::BusRuntime bus;
  bus.register_event<contract::ErrorOccurred>();

  AppStore store(bus);
  store.wire();

  std::string test_session_id = db::create_new_session("openai", "gpt-4o");
  store.set_session_id(test_session_id);

  // Publish ErrorOccurred with an error that already has "Error: " prefix
  bus.publish<contract::ErrorOccurred>({
      .session_id = test_session_id,
      .message = "Error: Upstream request failed",
      .severity = "error",
  });

  bus.drain();

  // Load messages from DB to check formatting
  auto messages = db::load_session_messages(test_session_id);
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages[0].second, "Error: Upstream request failed");

  // Publish ErrorOccurred with an error that does NOT have "Error: " prefix
  bus.publish<contract::ErrorOccurred>({
      .session_id = test_session_id,
      .message = "Upstream request failed",
      .severity = "error",
  });

  bus.drain();

  messages = db::load_session_messages(test_session_id);
  ASSERT_EQ(messages.size(), 2U);
  EXPECT_EQ(messages[1].second, "Error: Upstream request failed");

  // Clean up
  std::remove("tui_store_test_temp.db");
  std::remove("tui_store_test_temp.db-wal");
  std::remove("tui_store_test_temp.db-shm");
  if (old_db_path) {
    setenv("QCODE_DB_PATH", old_db_path, 1);
  } else {
    unsetenv("QCODE_DB_PATH");
  }
}

}  // namespace
}  // namespace ai::tui
