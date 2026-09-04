#include <qcode/core/stream_result.h>

#include <gtest/gtest.h>

#include <queue>
#include <string>
#include <utility>

namespace qcode {
namespace {

class ScriptedStream : public internal::StreamResultImpl {
 public:
  void push(StreamEvent event) { events_.push(std::move(event)); }

  StreamEvent get_next_event() override {
    if (events_.empty()) return StreamEvent(kStreamEventTypeFinish);
    StreamEvent event = std::move(events_.front());
    events_.pop();
    return event;
  }

  bool has_more_events() const override { return !events_.empty(); }
  void stop_stream() override {}

 private:
  std::queue<StreamEvent> events_;
};

TEST(StreamResultTest, HasErrorConsumesAllEvents) {
  auto impl = std::make_unique<ScriptedStream>();
  impl->push(StreamEvent("hello"));
  impl->push(StreamEvent(" world"));
  impl->push(StreamEvent(kStreamEventTypeFinish));
  StreamResult stream(std::move(impl));

  EXPECT_FALSE(stream.has_error());
  std::string leftover;
  for (const auto& event : stream) {
    if (event.is_text_delta()) leftover += event.text_delta;
  }
  EXPECT_TRUE(leftover.empty());
}

TEST(StreamResultTest, SinglePassCollectsTextDeltas) {
  auto impl = std::make_unique<ScriptedStream>();
  impl->push(StreamEvent("hello"));
  impl->push(StreamEvent(" world"));
  impl->push(StreamEvent(kStreamEventTypeFinish));
  StreamResult stream(std::move(impl));

  std::string text;
  for (const auto& event : stream) {
    if (event.is_text_delta()) text += event.text_delta;
  }
  EXPECT_EQ(text, "hello world");
}

}  // namespace
}  // namespace qcode
