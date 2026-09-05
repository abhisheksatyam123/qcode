#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>

#include <gtest/gtest.h>

#include <string>

namespace qcode::bus {
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
}  // namespace qcode::bus

#include <qcode/ui/app_store.h>
#include <qcode/session/session_store.h>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>
#include <cstdio>

namespace qcode {
namespace {

TEST(TuiStoreTest, FormatsErrorWithoutDuplicatePrefix) {
  // Use a temporary database for the test
  const char* old_db_path = std::getenv("QCODE_DB_PATH");
  const std::string db_path = std::string("/tmp/qcode_tui_bus_test/test_") +
      std::to_string(::getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
      ".db";
  std::error_code ec;
  std::filesystem::create_directories("/tmp/qcode_tui_bus_test", ec);
  setenv("QCODE_DB_PATH", db_path.c_str(), 1);
  
  // Initialize database
  session::init_database();

  bus::BusRuntime bus;
  bus.register_event<contract::ErrorOccurred>();

  AppStore store(bus);
  store.wire();

  std::string test_session_id = session::create_new_session("openai", "gpt-4o");
  store.set_session_id(test_session_id);

  // Publish ErrorOccurred with an error that already has "Error: " prefix
  bus.publish<contract::ErrorOccurred>({
      .session_id = test_session_id,
      .message = "Error: Upstream request failed",
      .severity = "error",
  });

  bus.drain();

  // Load messages from DB to check formatting
  auto messages = session::load_session_messages(test_session_id);
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages[0].second, "Error: Upstream request failed");

  // Publish ErrorOccurred with an error that does NOT have "Error: " prefix
  bus.publish<contract::ErrorOccurred>({
      .session_id = test_session_id,
      .message = "Upstream request failed",
      .severity = "error",
  });

  bus.drain();

  messages = session::load_session_messages(test_session_id);
  ASSERT_EQ(messages.size(), 2U);
  EXPECT_EQ(messages[1].second, "Error: Upstream request failed");

  const size_t before_info = messages.size();
  bus.publish<contract::ErrorOccurred>({
      .session_id = test_session_id,
      .message =
          "Retrying 1/6 in 2.5s — {\"error\":{\"message\":\"Endpoint is "
          "unavailable\"}}",
      .severity = "info",
  });
  bus.drain();
  messages = session::load_session_messages(test_session_id);
  EXPECT_EQ(messages.size(), before_info);

  // Clean up
  std::filesystem::remove(db_path, ec);
  std::filesystem::remove(db_path + "-wal", ec);
  std::filesystem::remove(db_path + "-shm", ec);
  if (old_db_path) {
    setenv("QCODE_DB_PATH", old_db_path, 1);
  } else {
    unsetenv("QCODE_DB_PATH");
  }
}

TEST(TuiStoreTest, IgnoresStaleSessionEventsAfterSwitch) {
  const char* old_db_path = std::getenv("QCODE_DB_PATH");
  const std::string db_path = std::string("/tmp/qcode_tui_bus_test/stale_") +
      std::to_string(::getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
      ".db";
  std::error_code ec;
  std::filesystem::create_directories("/tmp/qcode_tui_bus_test", ec);
  setenv("QCODE_DB_PATH", db_path.c_str(), 1);
  session::init_database();

  bus::BusRuntime bus;
  contract::register_all_events(bus);
  AppStore store(bus);
  store.wire();

  const auto old_id = session::create_new_session("cursor", "cursor-grok-4.6");
  store.set_session_id(old_id);
  bus.publish<contract::MessageDelta>({
      .session_id = old_id,
      .text = "hello from old",
      .done = false,
  });
  bus.drain();
  ASSERT_FALSE(store.state().messages_history->empty());
  EXPECT_EQ(store.state().messages_history->back().get_text(),
            "hello from old");

  const auto new_id = session::create_new_session("cursor", "cursor-grok-4.6");
  store.set_session_id(new_id);
  store.state().messages_history->clear();
  store.set_status("idle");

  bus.publish<contract::MessageDelta>({
      .session_id = old_id,
      .text = " leftover from old turn",
      .done = false,
  });
  bus.publish<contract::SessionStatusChanged>({
      .session_id = old_id,
      .status = "agent",
  });
  bus.drain();

  EXPECT_TRUE(store.state().messages_history->empty());
  EXPECT_EQ(store.status(), "idle");

  bus.publish<contract::MessageDelta>({
      .session_id = new_id,
      .text = "fresh",
      .done = false,
  });
  bus.drain();
  ASSERT_FALSE(store.state().messages_history->empty());
  EXPECT_EQ(store.state().messages_history->back().get_text(), "fresh");

  std::filesystem::remove(db_path, ec);
  std::filesystem::remove(db_path + "-wal", ec);
  std::filesystem::remove(db_path + "-shm", ec);
  if (old_db_path) {
    setenv("QCODE_DB_PATH", old_db_path, 1);
  } else {
    unsetenv("QCODE_DB_PATH");
  }
}

}  // namespace
}  // namespace qcode
