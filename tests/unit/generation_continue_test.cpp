#include <qcode/generation/generation_continue.h>

#include <gtest/gtest.h>

namespace qcode {
namespace {

TEST(GenerationContinueTest, DetectsMuseSparkStalls) {
  EXPECT_TRUE(looks_like_task_stall(
      "One more round — the exact render rules I need."));
  EXPECT_TRUE(looks_like_task_stall(
      "Now I need the CSS values + the marked parser rules."));
  EXPECT_TRUE(looks_like_task_stall(
      "Need your call on scope before I change it."));
  EXPECT_FALSE(looks_like_task_stall(
      "Moved model and context into the session header."));
}

TEST(GenerationContinueTest, AutoContinuesStallsButNotDone) {
  EXPECT_TRUE(should_auto_continue_build(0, "Now I need the CSS values + the marked parser rules."));
  EXPECT_FALSE(should_auto_continue_build(0, "Done. Header is updated."));
  EXPECT_FALSE(should_auto_continue_build(0, "Theme propagation done — 7 static tests green, new chrome now follows /theme."));
  EXPECT_FALSE(should_auto_continue_build(0, "Done this run:\n- updated styles\n- all tests green"));
  EXPECT_TRUE(should_auto_continue_build(0, ""));
  EXPECT_TRUE(should_auto_continue_build(3, ""));
  EXPECT_TRUE(should_auto_continue_build(6, "one more round"));
  EXPECT_TRUE(should_auto_continue_build(20, "one more round"));
  EXPECT_TRUE(should_auto_continue_build(100000, "one more round"));
  EXPECT_FALSE(should_auto_continue_build(0, "Moved model and context into the session header."));
  // Stall takes precedence over partial completion words
  EXPECT_TRUE(should_auto_continue_build(0, "I have updated the header. Next, I will run unit tests."));
}

TEST(GenerationContinueTest, DoesNotFalselyStallOnNormalExplanations) {
  EXPECT_FALSE(looks_like_task_stall(
      "Before I explain the implementation, here is what changed."));
  EXPECT_FALSE(looks_like_task_stall(
      "Tests confirm that all edge cases pass."));
  EXPECT_FALSE(looks_like_task_stall(
      "I added one more unit test for coverage."));
}

}  // namespace
}  // namespace qcode
