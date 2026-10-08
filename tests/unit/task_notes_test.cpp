#include <gtest/gtest.h>

#include <qcode/session/task_notes.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace qcode::task_notes;

namespace {

class TaskNotesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() /
            ("qcode_task_notes_" + std::to_string(::getpid()) + "_" +
             ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(root_);
    fs::create_directories(root_);
  }
  void TearDown() override { fs::remove_all(root_); }
  static std::string read(const std::string& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
  static void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x\n";
  }
  fs::path root_;
};

}  // namespace

TEST_F(TaskNotesTest, DefaultsToScratchpadTodoWhenNothingExists) {
  const auto loc = resolve(root_.string());
  EXPECT_FALSE(loc.exists);
  EXPECT_FALSE(loc.is_folder);
  EXPECT_EQ(loc.path, (root_ / "scratchpad" / "todo.md").string());
}

TEST_F(TaskNotesTest, ResolutionOrder) {
  touch(root_ / "scratchpad" / "todo.md");
  EXPECT_EQ(resolve(root_.string()).path, (root_ / "scratchpad" / "todo.md").string());

  fs::create_directories(root_ / "scratchpad" / "todo");
  auto loc = resolve(root_.string());
  EXPECT_TRUE(loc.is_folder);
  EXPECT_EQ(loc.path, (root_ / "scratchpad" / "todo" / "index.md").string());

  touch(root_ / "todo.md");
  EXPECT_EQ(resolve(root_.string()).path, (root_ / "todo.md").string());

  fs::create_directories(root_ / "todo");
  loc = resolve(root_.string());
  EXPECT_TRUE(loc.is_folder);
  EXPECT_EQ(loc.path, (root_ / "todo" / "index.md").string());
}

TEST_F(TaskNotesTest, EnsureCreatesTemplateWithThreeSections) {
  const auto loc = ensure(root_.string());
  ASSERT_TRUE(loc.exists);
  const auto text = read(loc.path);
  const auto t = text.find("## Tasks");
  const auto s = text.find("## Systems");
  const auto l = text.find("## Log");
  ASSERT_NE(t, std::string::npos);
  ASSERT_NE(s, std::string::npos);
  ASSERT_NE(l, std::string::npos);
  EXPECT_LT(t, s);
  EXPECT_LT(s, l);
}

TEST_F(TaskNotesTest, AppendLogAddsSectionAndSingleLines) {
  touch(root_ / "todo.md");  // existing file without a Log section
  ASSERT_TRUE(append_log(root_.string(), "sub:explore", "found\nthree refs"));
  ASSERT_TRUE(append_log(root_.string(), "orch", "T1 done"));
  const auto text = read((root_ / "todo.md").string());
  EXPECT_EQ(text.find("## Log"), text.rfind("## Log"));
  EXPECT_NE(text.find("[sub:explore] found three refs\n"), std::string::npos);
  EXPECT_NE(text.find("[orch] T1 done\n"), std::string::npos);
}

TEST_F(TaskNotesTest, ParallelAppendsDoNotInterleave) {
  ensure(root_.string());
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([this, i] {
      for (int k = 0; k < 10; ++k) {
        append_log(root_.string(), "sub:" + std::to_string(i), "event " + std::to_string(k));
      }
    });
  }
  for (auto& t : threads) t.join();
  std::ifstream in(resolve(root_.string()).path);
  std::string line;
  int entries = 0;
  while (std::getline(in, line)) {
    if (line.rfind("- ", 0) == 0) {
      ++entries;
      EXPECT_NE(line.find("] event "), std::string::npos) << line;
    }
  }
  EXPECT_EQ(entries, 80);
}
