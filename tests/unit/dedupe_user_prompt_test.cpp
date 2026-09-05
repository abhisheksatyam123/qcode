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

TEST(BusRuntimeWake, ExplicitWakeTriggersCallback) {
    bus::BusRuntime bus;
    bool woken = false;
    bus.set_wake_callback([&woken]() { woken = true; });
    EXPECT_FALSE(woken);
    bus.wake();
    EXPECT_TRUE(woken);
}

TEST(AppStoreRetry, ErrorMarksRetryAvailableAndPreservesErrorStatus) {
    bus::BusRuntime bus;
    contract::register_all_events(bus);
    AppStore store(bus);
    store.wire();
    store.set_session_id("test-session");

    *store.state().last_user_prompt = "build feature";
    EXPECT_FALSE(*store.state().retry_available);

    // Error event marks retry available and sets status to error
    bus.publish<contract::ErrorOccurred>({
        .session_id = "test-session",
        .message = "Rate limit exceeded",
        .severity = "error"
    });
    bus.drain();

    EXPECT_TRUE(*store.state().retry_available);
    EXPECT_EQ(store.status(), "error");

    // Idle notification must NOT overwrite active error status
    bus.publish<contract::SessionStatusChanged>({
        .session_id = "test-session",
        .status = "idle"
    });
    bus.drain();

    EXPECT_EQ(store.status(), "error");
    EXPECT_TRUE(*store.state().retry_available);

    // Explicit clear_error resets status to idle
    store.clear_error();
    EXPECT_EQ(store.status(), "idle");
}

TEST(AppStoreRetry, SuccessfulTurnClearsRetryAvailable) {
    bus::BusRuntime bus;
    contract::register_all_events(bus);
    AppStore store(bus);
    store.wire();
    store.set_session_id("test-session");

    store.mark_retry_available("failing prompt");
    EXPECT_TRUE(*store.state().retry_available);

    // Finished message delta marks completion and clears retry availability
    bus.publish<contract::MessageDelta>({
        .session_id = "test-session",
        .text = "All done!",
        .done = true
    });
    bus.drain();

    EXPECT_FALSE(*store.state().retry_available);
}

TEST(AppStoreRetry, AbortMarksRetryAvailable) {
    bus::BusRuntime bus;
    contract::register_all_events(bus);
    AppStore store(bus);
    store.wire();
    store.set_session_id("test-session");

    *store.state().last_user_prompt = "long running prompt";
    EXPECT_FALSE(*store.state().retry_available);

    bus.publish<contract::ErrorOccurred>({
        .session_id = "test-session",
        .message = "Generation stopped",
        .severity = "warning"
    });
    bus.drain();

    EXPECT_TRUE(*store.state().retry_available);
}

TEST(PromptQueue, FifoOrderAndIndependentItems) {
    bus::BusRuntime bus;
    AppStore store(bus);
    store.wire();

    store.enqueue_prompt("item 1");
    store.enqueue_prompt("item 2");
    store.enqueue_prompt("item 3");

    EXPECT_EQ(store.queue_size(), 3u);
    auto snapshot = store.queued_prompts_snapshot();
    ASSERT_EQ(snapshot.size(), 3u);
    EXPECT_EQ(snapshot[0], "item 1");
    EXPECT_EQ(snapshot[1], "item 2");
    EXPECT_EQ(snapshot[2], "item 3");

    EXPECT_EQ(store.dequeue_prompt(), "item 1");
    EXPECT_EQ(store.queue_size(), 2u);

    EXPECT_EQ(store.dequeue_prompt(), "item 2");
    EXPECT_EQ(store.queue_size(), 1u);

    EXPECT_EQ(store.dequeue_prompt(), "item 3");
    EXPECT_EQ(store.queue_size(), 0u);
    EXPECT_FALSE(store.has_queued_prompt());
}

TEST(RetryRollback, CleansTrailingArtifactsAndAvoidsDuplicatePrompt) {
    Messages hist;
    hist.push_back(Message::user("prior turn"));
    hist.push_back(Message::assistant("prior reply"));

    // Failed turn
    hist.push_back(Message::user("fix memory leak"));
    hist.push_back(Message::assistant("partial code snippet"));
    hist.push_back(Message::system("Error: Network connection lost"));

    const std::string retry_prompt = "fix memory leak";

    // Rollback logic matching trigger_retry
    for (int i = static_cast<int>(hist.size()) - 1; i >= 0; --i) {
        if (hist[i].role == kMessageRoleUser && !hist[i].has_tool_results()) {
            if (hist[i].get_text() == retry_prompt) {
                hist.erase(hist.begin() + i + 1, hist.end());
                break;
            }
        }
    }

    // Now history ends with the user prompt
    ASSERT_EQ(hist.size(), 3u);
    EXPECT_EQ(hist.back().role, kMessageRoleUser);
    EXPECT_EQ(hist.back().get_text(), "fix memory leak");

    // Re-sending does not duplicate user message
    EXPECT_FALSE(should_append_user_message(hist, retry_prompt));
}
}  // namespace qcode
