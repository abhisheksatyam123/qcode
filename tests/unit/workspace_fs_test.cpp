#include "workspace_fs.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>

namespace qcode {
namespace server {
namespace {

TEST(WorkspaceFsTest, ShellQuoteWrapsSingleQuotes) {
    EXPECT_EQ(shell_quote("abc"), "'abc'");
    EXPECT_NE(shell_quote("a'b").find("'"), std::string::npos);
}

TEST(WorkspaceFsTest, ExpandTildeUsesHome) {
    const char* old = std::getenv("HOME");
    std::string saved = old ? old : "";
    setenv("HOME", "/home/tester", 1);
    EXPECT_EQ(expand_tilde("~"), "/home/tester");
    EXPECT_EQ(expand_tilde("~/proj"), "/home/tester/proj");
    EXPECT_EQ(expand_tilde("/abs"), "/abs");
    if (old) setenv("HOME", saved.c_str(), 1);
    else unsetenv("HOME");
}

TEST(WorkspaceFsTest, SplitLinesDropsCR) {
    auto lines = split_lines("a\r\nb\nc");
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "a");
    EXPECT_EQ(lines[1], "b");
    EXPECT_EQ(lines[2], "c");
}

TEST(WorkspaceFsTest, LooksBinaryDetectsNul) {
    EXPECT_FALSE(looks_binary("hello text"));
    std::string bin("abc");
    bin.push_back('\0');
    bin += "def";
    EXPECT_TRUE(looks_binary(bin));
}

TEST(WorkspaceFsTest, ResolveWorkspacePathRejectsEscape) {
    auto root = std::filesystem::temp_directory_path() / "qcode_ws_test";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    std::string abs, rel;
    EXPECT_EQ(resolve_workspace_path(root.string(), "ok.txt", abs, rel), "");
    EXPECT_FALSE(abs.empty());
    EXPECT_FALSE(resolve_workspace_path(root.string(), "/etc/passwd", abs, rel).empty());
    EXPECT_FALSE(resolve_workspace_path(root.string(), "../outside", abs, rel).empty());
    std::filesystem::remove_all(root, ec);
}

}  // namespace
}  // namespace server
}  // namespace qcode
