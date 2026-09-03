#include <qcode/session/generation_continue.h>

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

TEST(GenerationContinueTest, AutoContinuesBuildStallsButNotPlanOrDone) {
  EXPECT_TRUE(should_auto_continue_build(
      false, 0, "Now I need the CSS values + the marked parser rules."));
  EXPECT_FALSE(should_auto_continue_build(
      true, 0, "Now I need the CSS values + the marked parser rules."));
  EXPECT_FALSE(should_auto_continue_build(false, 0, "Done. Header is updated."));
  EXPECT_TRUE(should_auto_continue_build(false, 0, ""));
  EXPECT_FALSE(should_auto_continue_build(false, 3, ""));
  EXPECT_FALSE(should_auto_continue_build(false, 6, "one more round"));
}

}  // namespace
}  // namespace qcode
