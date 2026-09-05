#include <gtest/gtest.h>
#include <qcode/session/git_workspace.h>
#include <qcode/ui/chat_state.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace qcode {
namespace {

class GitWorkspaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        original_cwd_ = std::filesystem::current_path();
        test_dir_ = "/tmp/qcode_git_workspace_test_" + std::to_string(getpid());
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
        std::filesystem::create_directories(test_dir_, ec);
        std::filesystem::current_path(test_dir_, ec);

        // Init git repo
        system("git init -q");
        system("git config user.email 'test@example.com'");
        system("git config user.name 'Test User'");
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::current_path(original_cwd_, ec);
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path original_cwd_;
    std::filesystem::path test_dir_;
};

TEST_F(GitWorkspaceTest, DetectsUntrackedAndModifiedFiles) {
    // 1. Initial commit
    {
        std::ofstream f("initial.txt");
        f << "line 1\nline 2\n";
    }
    system("git add initial.txt && git commit -q -m 'initial commit'");

    // 2. Modify initial.txt
    {
        std::ofstream f("initial.txt", std::ios::app);
        f << "line 3\n";
    }

    // 3. Create untracked file
    {
        std::ofstream f("untracked.txt");
        f << "foo\nbar\n";
    }

    auto entries = fetch_modified_files();
    EXPECT_GE(entries.size(), 2u);

    bool found_modified = false;
    bool found_untracked = false;

    for (const auto& e : entries) {
        if (e.path == "initial.txt") {
            found_modified = true;
            EXPECT_FALSE(e.untracked);
            EXPECT_EQ(e.additions, 1);
            EXPECT_EQ(e.deletions, 0);
        } else if (e.path == "untracked.txt") {
            found_untracked = true;
            EXPECT_TRUE(e.untracked);
            EXPECT_EQ(e.additions, 2);
        }
    }

    EXPECT_TRUE(found_modified);
    EXPECT_TRUE(found_untracked);

    // Test update_modified_files on ChatState
    ChatState state;
    update_modified_files(state);
    ASSERT_TRUE(state.file_changes != nullptr);
    EXPECT_EQ(state.file_changes->size(), entries.size());
    EXPECT_GT(*state.files_revision, 0);
}

} // namespace
} // namespace qcode
