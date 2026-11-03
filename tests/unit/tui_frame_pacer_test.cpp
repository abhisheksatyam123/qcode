#include <frame_pacer.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace qcode {
namespace tui {
namespace {

using namespace std::chrono_literals;

TEST(TuiFramePacerTest, BurstOfRequestsCoalescesIntoFewPosts) {
    std::atomic<int> posts{0};
    {
        FramePacer pacer([&] { posts++; }, 50ms);
        for (int i = 0; i < 10000; ++i) pacer.request();
        std::this_thread::sleep_for(20ms);
        // A burst inside one interval: one post now, at most one more due.
        EXPECT_GE(posts.load(), 1);
        EXPECT_LE(posts.load(), 2);
    }
}

TEST(TuiFramePacerTest, SteadyStreamIsCappedAtFrameRate) {
    std::atomic<int> posts{0};
    FramePacer pacer([&] { posts++; }, 20ms);
    const auto end = std::chrono::steady_clock::now() + 200ms;
    while (std::chrono::steady_clock::now() < end) {
        pacer.request();
        std::this_thread::sleep_for(1ms);  // ~1000 requests per second
    }
    pacer.stop();
    // 200ms at one post per 20ms is ~10; allow scheduler slack.
    EXPECT_GE(posts.load(), 5);
    EXPECT_LE(posts.load(), 12);
}

TEST(TuiFramePacerTest, RequestAfterIdlePostsWithoutWaitingForInterval) {
    std::atomic<int> posts{0};
    FramePacer pacer([&] { posts++; }, 500ms);
    std::this_thread::sleep_for(10ms);
    const auto t0 = std::chrono::steady_clock::now();
    pacer.request();
    while (posts.load() == 0 &&
           std::chrono::steady_clock::now() - t0 < 400ms) {
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_EQ(posts.load(), 1);
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 100ms);
}

TEST(TuiFramePacerTest, StopDropsLaterRequests) {
    std::atomic<int> posts{0};
    FramePacer pacer([&] { posts++; }, 1ms);
    pacer.stop();
    pacer.request();
    std::this_thread::sleep_for(10ms);
    EXPECT_EQ(posts.load(), 0);
    pacer.stop();  // idempotent
}

TEST(TuiFramePacerTest, FrameIntervalFromEnv) {
    EXPECT_EQ(frame_interval_from_env(nullptr), 16ms);
    EXPECT_EQ(frame_interval_from_env("30"), 33ms);
    EXPECT_EQ(frame_interval_from_env("1"), 200ms);
    EXPECT_EQ(frame_interval_from_env("100000"), 4ms);
    EXPECT_EQ(frame_interval_from_env("garbage"), 16ms);
}

}  // namespace
}  // namespace tui
}  // namespace qcode
