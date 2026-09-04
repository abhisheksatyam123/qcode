// Duplicate-user-prompt guard: after an aborted/retried turn the trailing
// history already holds the user prompt — re-sending must reuse it, not
// append a second visible copy. Also verifies that enqueued prompts remain
// in the queue section until execution starts.

#include <qcode/generation/generation_controller.h>
#include <qcode/ui/app_store.h>

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

TEST(ShouldAppendUserMessage, SystemErrorAfterPromptDoesNotDuplicate) {
    Messages hist;
    hist.push_back(Message::user("continue"));
    hist.push_back(Message::system("Error: Authentication failed"));
    EXPECT_FALSE(should_append_user_message(hist, "continue"));
}

TEST(ShouldAppendUserMessage, AssistantReplyMeansFreshSend) {
    Messages hist;
    hist.push_back(Message::user("hello"));
    hist.push_back(Message::assistant("hi there"));
    EXPECT_TRUE(should_append_user_message(hist, "hello"));
}

TEST(EnqueuePromptQueuing, StaysInQueueAndNotInHistoryUntilExecution) {
    bus::BusRuntime bus;
    AppStore store(bus);
    store.wire();
    
    // First enqueue adds prompt to queue, NOT history
    store.enqueue_prompt("hello world");
    EXPECT_TRUE(store.state().messages_history->empty());
    EXPECT_EQ(store.queue_size(), 1u);
    ASSERT_TRUE(store.state().queued_prompt_texts != nullptr);
    EXPECT_EQ(store.state().queued_prompt_texts->size(), 1u);
    EXPECT_EQ(store.state().queued_prompt_texts->front(), "hello world");

    // Second enqueue with prompt
    store.enqueue_prompt("second prompt");
    EXPECT_TRUE(store.state().messages_history->empty());
    EXPECT_EQ(store.queue_size(), 2u);
    EXPECT_EQ(store.state().queued_prompt_texts->size(), 2u);
    EXPECT_EQ(store.state().queued_prompt_texts->back(), "second prompt");
}

}  // namespace
}  // namespace qcode
