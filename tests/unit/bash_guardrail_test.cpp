#include <gtest/gtest.h>

#include <qcode/tools/bash_tool.h>

#include <unistd.h>

#include <filesystem>
#include <string>

using qcode::destructive_command_reason;

TEST(BashGuardrailTest, BlocksRecursiveForcedRemovalOfRootAndHome) {
  EXPECT_FALSE(destructive_command_reason("rm -rf /").empty());
  EXPECT_FALSE(destructive_command_reason("rm -fr ~").empty());
  EXPECT_FALSE(destructive_command_reason("rm -rf \"$HOME\"").empty());
  EXPECT_FALSE(destructive_command_reason("rm --recursive --force /").empty());
  EXPECT_FALSE(destructive_command_reason("cd /tmp && rm -rf /").empty());
}

TEST(BashGuardrailTest, AllowsOrdinaryRemoval) {
  EXPECT_TRUE(destructive_command_reason("rm -rf build/tmp").empty());
  EXPECT_TRUE(destructive_command_reason("rm file.txt").empty());
  EXPECT_TRUE(destructive_command_reason("rm -r build").empty());  // not forced
}

TEST(BashGuardrailTest, BlocksForcePushAndHardReset) {
  EXPECT_FALSE(destructive_command_reason("git push --force origin main").empty());
  EXPECT_FALSE(destructive_command_reason("git push -f origin main").empty());
  EXPECT_FALSE(destructive_command_reason("git push -uf origin main").empty());
  EXPECT_FALSE(destructive_command_reason("git reset --hard HEAD~1").empty());
  EXPECT_FALSE(destructive_command_reason("git clean -fdx").empty());
}

TEST(BashGuardrailTest, AllowsNormalGit) {
  EXPECT_TRUE(destructive_command_reason("git push origin main").empty());
  EXPECT_TRUE(destructive_command_reason("git push --force-with-lease origin main").empty());
  EXPECT_TRUE(destructive_command_reason("git reset --soft HEAD~1").empty());
  EXPECT_TRUE(destructive_command_reason("git clean -n").empty());
  EXPECT_TRUE(destructive_command_reason("git status --short").empty());
}

TEST(BashGuardrailTest, BlocksDiskAndPowerCommands) {
  EXPECT_FALSE(destructive_command_reason("mkfs.ext4 /dev/sdb1").empty());
  EXPECT_FALSE(destructive_command_reason("dd if=/dev/zero of=/dev/sda").empty());
  EXPECT_FALSE(destructive_command_reason("shutdown -h now").empty());
  EXPECT_TRUE(destructive_command_reason("dd if=in.bin of=out.bin").empty());
}
// End to end through the public entry point: a blocked command is refused before
// anything runs, in both foreground and background modes. The marker file proves
// the command never executed.
TEST(BashGuardrailTest, ExecuteRefusesBlockedCommandInBothModes) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / ("qcode_guard_" + std::to_string(::getpid()));
  fs::create_directories(dir);
  const fs::path marker = dir / "ran.txt";

  qcode::ToolExecutionContext ctx;
  ctx.workspace = dir.string();
  const std::string cmd = "git push --force origin main; touch " + marker.string();

  for (const std::string m : {std::string("run"), std::string("background")}) {
    qcode::JsonValue args;
    args["command"] = cmd;
    args["mode"] = m;
    const auto result = qcode::BashTool::execute(args, ctx);
    ASSERT_TRUE(result.contains("error"));
    EXPECT_NE(result["error"].get<std::string>().find("blocked"), std::string::npos);
    EXPECT_FALSE(fs::exists(marker));
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
}
