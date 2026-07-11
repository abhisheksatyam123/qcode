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
