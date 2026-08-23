// Duplicate-user-prompt guard: after an aborted/retried turn the trailing
// history already holds the user prompt — re-sending must reuse it, not
// append a second visible copy.

#include <qcode/session/generation_controller.h>

#include <gtest/gtest.h>

namespace qcode {
namespace {

TEST(ShouldAppendUserMessage, FreshHistoryAppends) {
    Messages hist;
    EXPECT_TRUE(should_append_user_message(hist, "hello"));
}

TEST(ShouldAppendUserMessage, TrailingSamePromptDoesNotDuplicate) {
    Messages hist;
    hist.push_back(Message::user("fix the bug"));
    EXPECT_FALSE(should_append_user_message(hist, "fix the bug"));
    // Different text still appends.
    EXPECT_TRUE(should_append_user_message(hist, "other prompt"));
}

TEST(ShouldAppendUserMessage, AbortArtifactsStillDedupe) {
    // Aborted turn may leave tool cards after the prompt.
    Messages hist;
    hist.push_back(Message::user("run tests"));
    hist.push_back(Message::assistant(""));
    hist.back().content.emplace_back(
        qcode::ToolCallContentPart("t1", "bash", JsonValue{}));
    EXPECT_FALSE(should_append_user_message(hist, "run tests"));
}

TEST(ShouldAppendUserMessage, AssistantReplyMeansFreshSend) {
    Messages hist;
    hist.push_back(Message::user("hello"));
    hist.push_back(Message::assistant("hi there"));
    EXPECT_TRUE(should_append_user_message(hist, "hello"));
}

}  // namespace
}  // namespace qcode
