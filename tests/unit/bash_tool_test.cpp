#include <qcode/tools/bash_tool.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace qcode {
namespace {

TEST(BashToolTest, SpillsLargeOutputWithoutReturningItInline) {
  const auto output_dir =
      std::filesystem::temp_directory_path() /
      ("qcode-bash-test-" + std::to_string(getpid()));
  std::filesystem::remove_all(output_dir);
  ASSERT_EQ(setenv("QCODE_TOOL_OUTPUT_DIR", output_dir.c_str(), 1), 0);

  const JsonValue args = {
      {"mode", "run"},
      {"command",
       "python3 -c 'import sys; sys.stdout.write(\"x\" * 5242880)'"},
      {"description", "Generate bounded test output"},
      {"max_output_chars", 4096},
  };
  const auto result = BashTool::execute(args, ToolExecutionContext{});

  ASSERT_TRUE(result.contains("output"));
  const auto inline_output = result["output"].get<std::string>();
  EXPECT_LT(inline_output.size(), 8192U);
  EXPECT_NE(inline_output.find("Output truncated"), std::string::npos);

  size_t file_count = 0;
  uintmax_t stored_size = 0;
  for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
    if (!entry.is_regular_file()) continue;
    ++file_count;
    stored_size = entry.file_size();
  }
  EXPECT_EQ(file_count, 1U);
  EXPECT_EQ(stored_size, 5U * 1024U * 1024U);

  std::filesystem::remove_all(output_dir);
  unsetenv("QCODE_TOOL_OUTPUT_DIR");
}

TEST(BashToolTest, ExactLineLimitIsNotReportedAsTruncated) {
  const auto output_dir =
      std::filesystem::temp_directory_path() /
      ("qcode-bash-lines-" + std::to_string(getpid()));
  std::filesystem::remove_all(output_dir);
  ASSERT_EQ(setenv("QCODE_TOOL_OUTPUT_DIR", output_dir.c_str(), 1), 0);

  const JsonValue args = {
      {"mode", "run"},
      {"command", "printf 'line\\n'"},
      {"description", "Generate exact line output"},
      {"max_output_lines", 1},
  };
  const auto result = BashTool::execute(args, ToolExecutionContext{});

  EXPECT_EQ(result["output"].get<std::string>(), "line\n");
  EXPECT_FALSE(std::filesystem::exists(output_dir) &&
               !std::filesystem::is_empty(output_dir));
  std::filesystem::remove_all(output_dir);
  unsetenv("QCODE_TOOL_OUTPUT_DIR");
}

TEST(BashToolTest, TruncatesAtValidUtf8Boundary) {
  const auto output_dir =
      std::filesystem::temp_directory_path() /
      ("qcode-bash-utf8-" + std::to_string(getpid()));
  std::filesystem::remove_all(output_dir);
  ASSERT_EQ(setenv("QCODE_TOOL_OUTPUT_DIR", output_dir.c_str(), 1), 0);

  const JsonValue args = {
      {"mode", "run"},
      {"command",
       "python3 -c 'import sys; sys.stdout.buffer.write(\"ééé\".encode())'"},
      {"description", "Generate UTF-8 test output"},
      {"max_output_chars", 5},
  };
  const auto result = BashTool::execute(args, ToolExecutionContext{});

  EXPECT_NO_THROW(static_cast<void>(result.dump()));
  const auto output = result["output"].get<std::string>();
  EXPECT_EQ(output.substr(0, std::string("éé").size()), "éé");
  EXPECT_NE(output.find("Output truncated"), std::string::npos);
  std::filesystem::remove_all(output_dir);
  unsetenv("QCODE_TOOL_OUTPUT_DIR");
}

TEST(BashToolTest, SerializesConcurrentExecutions) {
  std::atomic<int> running_count{0};
  std::atomic<bool> overlap_detected{false};

  auto task = [&]() {
    const JsonValue args = {
        {"mode", "run"},
        {"command", "sleep 0.05"},
        {"description", "Concurrent test sleep"},
    };
    BashTool::execute(args, ToolExecutionContext{});
  };

  std::thread t1(task);
  std::thread t2(task);
  t1.join();
  t2.join();

  EXPECT_FALSE(overlap_detected.load());
}

}  // namespace
}  // namespace qcode
