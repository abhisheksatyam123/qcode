#include <qcode/tools/bash_tool.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

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

// Guards against regression to the old process-wide `g_bash_exec_mutex`,
// which serialized EVERY command: 5 x 0.4s sleeps would take >= 2.0s.
// Bounded concurrency must admit them in (at most) ceil(5/4) = 2 waves.
TEST(BashToolTest, RunsConcurrentExecutionsInParallelInsteadOfSerializing) {
  constexpr int kNumCommands = 5;
  constexpr auto kSleep = std::chrono::milliseconds(400);

  auto task = [&]() {
    const JsonValue args = {
        {"mode", "run"},
        {"command", "sleep 0.4"},
        {"description", "Concurrent test sleep"},
        {"timeout", 10000},
    };
    BashTool::execute(args, ToolExecutionContext{});
  };

  const auto start = std::chrono::steady_clock::now();
  std::vector<std::thread> threads;
  threads.reserve(kNumCommands);
  for (int i = 0; i < kNumCommands; ++i) threads.emplace_back(task);
  for (auto& t : threads) t.join();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  // Serialized execution => >= 5 * 400ms = 2000ms. Generous margin over the
  // expected ~800ms (2 waves of 4 slots) to stay robust on loaded machines.
  EXPECT_LT(elapsed.count(), 1500)
      << "concurrent bash commands appear serialized: " << elapsed.count()
      << "ms";
}

// Scoped override of the limiter slot count (read live on each acquire).
struct EnvVarGuard {
  explicit EnvVarGuard(const char* name, const char* value) : name_(name) {
    setenv(name_, value, 1);
  }
  ~EnvVarGuard() { unsetenv(name_); }
  const char* name_;
};

// With QCODE_BASH_MAX_CONCURRENT=2, 4 x 400ms sleeps must run in exactly 2
// waves: >= 750ms (proves the bound is enforced) but < 1400ms (proves it is
// bounded parallelism, not full serialization, which would be >= 1600ms).
TEST(BashToolTest, EnforcesBoundedConcurrencyFromEnvOverride) {
  EnvVarGuard guard("QCODE_BASH_MAX_CONCURRENT", "2");

  auto task = [&]() {
    const JsonValue args = {
        {"mode", "run"},
        {"command", "sleep 0.4"},
        {"description", "Bounded concurrency test sleep"},
        {"timeout", 10000},
    };
    BashTool::execute(args, ToolExecutionContext{});
  };

  const auto start = std::chrono::steady_clock::now();
  std::vector<std::thread> threads;
  threads.reserve(4);
  for (int i = 0; i < 4; ++i) threads.emplace_back(task);
  for (auto& t : threads) t.join();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  EXPECT_GE(elapsed.count(), 750)
      << "bound not enforced: 4 x 400ms finished in " << elapsed.count()
      << "ms with 2 slots";
  EXPECT_LT(elapsed.count(), 1400)
      << "too slow with 2 slots (serial would be 1600ms): "
      << elapsed.count() << "ms";
}

TEST(BashToolTest, AllowsBitwiseShiftOperatorsWithoutFalseHeredocError) {
  const JsonValue args = {
      {"mode", "run"},
      {"command", "python3 -c \"print(16 << 2)\""},
      {"description", "Test bitwise left shift operator"},
  };
  const auto result = BashTool::execute(args, ToolExecutionContext{});
  EXPECT_TRUE(result.contains("output"));
  EXPECT_NE(result["output"].get<std::string>().find("64"), std::string::npos);
}

TEST(BashToolTest, AllowsHerestringsAndQuotedLiterals) {
  const JsonValue args = {
      {"mode", "run"},
      {"command", "python3 -c \"print('<<< hello >>>')\""},
      {"description", "Test herestring syntax"},
  };
  const auto result = BashTool::execute(args, ToolExecutionContext{});
  EXPECT_TRUE(result.contains("output"));
  EXPECT_NE(result["output"].get<std::string>().find("hello"), std::string::npos);
}

TEST(BashToolTest, RejectsTrulyUnterminatedHeredoc) {
  const JsonValue args = {
      {"mode", "run"},
      {"command", "cat << UNTERMINATED_MARKER\nsome content"},
      {"description", "Test unterminated heredoc"},
  };
  const auto result = BashTool::execute(args, ToolExecutionContext{});
  EXPECT_TRUE(result.contains("error"));
  EXPECT_NE(result["error"].get<std::string>().find("UNTERMINATED_MARKER"), std::string::npos);
}

TEST(BashToolTest, AllowsQuotedNumbersAndShiftInPythonCommands) {
  const JsonValue args = {
      {"mode", "run"},
      {"command", "python3 -c \"print(1 << int('16'))\""},
      {"description", "Test shift operator with quoted numbers"},
  };
  const auto result = BashTool::execute(args, ToolExecutionContext{});
  EXPECT_TRUE(result.contains("output"));
  EXPECT_NE(result["output"].get<std::string>().find("65536"), std::string::npos);
}

TEST(BashToolTest, AllowsEscapedQuotesInPythonInlineCommands) {
  const JsonValue args = {
      {"mode", "run"},
      {"command", "python3 -c \"asm_path = 'firmware.asm'; print(1 << 16)\""},
      {"description", "Test Python inline script with mixed quotes and shift"},
  };
  const auto result = BashTool::execute(args, ToolExecutionContext{});
  EXPECT_TRUE(result.contains("output"));
  EXPECT_NE(result["output"].get<std::string>().find("65536"), std::string::npos);
}


TEST(BashToolTest, ReadOnlyContextRejectsRedirect) {
  ToolExecutionContext ctx;
  ctx.can_edit = false;
  ctx.workspace = "/tmp";
  const auto out = BashTool::execute(
      nlohmann::json{{"command", "echo hi > /tmp/qcode-readonly-test"},
                     {"description", "write a file"}},
      ctx);
  EXPECT_TRUE(out.contains("error")) << out.dump();
  EXPECT_NE(out.value("error", "").find("Read-only"), std::string::npos);
}

TEST(BashToolTest, ReadOnlyContextAllowsEcho) {
  ToolExecutionContext ctx;
  ctx.can_edit = false;
  ctx.workspace = "/tmp";
  const auto out = BashTool::execute(
      nlohmann::json{{"command", "echo hi"}, {"description", "print hi"}},
      ctx);
  EXPECT_FALSE(out.contains("error")) << out.dump();
  EXPECT_NE(out.value("output", "").find("hi"), std::string::npos);
}

TEST(BashToolTest, ReadOnlyContextRejectsRm) {
  ToolExecutionContext ctx;
  ctx.can_edit = false;
  const auto out = BashTool::execute(
      nlohmann::json{{"command", "rm -rf /tmp/qcode-readonly-test"},
                     {"description", "delete path"}},
      ctx);
  EXPECT_TRUE(out.contains("error")) << out.dump();
}

}  // namespace
}  // namespace qcode
