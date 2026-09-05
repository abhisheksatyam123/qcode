#include <qcode/session/study_assistant.h>
#include <qcode/session/study_store.h>
#include <qcode/session/session_store.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <unistd.h>
#include <string>
#include <vector>

namespace qcode {
namespace study {
namespace {

class StudyAssistantTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        std::filesystem::create_directories("/tmp/qcode_study_asst_test", ec);
        db_path_ = "/tmp/qcode_study_asst_test/test_" +
                   std::to_string(::getpid()) + "_" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                   ".db";
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(db_path_ + "-wal", ec);
        std::filesystem::remove(db_path_ + "-shm", ec);
        setenv("QCODE_DB_PATH", db_path_.c_str(), 1);
        qcode::session::init_database();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(db_path_ + "-wal", ec);
        std::filesystem::remove(db_path_ + "-shm", ec);
        unsetenv("QCODE_DB_PATH");
    }

    std::string db_path_;
};

TEST(StudyNormalizeTest, TrimsAndLowercases) {
    EXPECT_EQ(normalize_answer("  Hello   World  "), "hello world");
    EXPECT_EQ(normalize_answer("ABC"), "abc");
}

TEST_F(StudyAssistantTest, GradesMcqDeterministically) {
    auto course = upsert_course("C", "/c");
    auto ch = upsert_chapter(course.id, "ch1", "Ch1", "/c/ch1", 0);
    nlohmann::json quiz = nlohmann::json::array({
        {
            {"topic", "arith"},
            {"type", "mcq"},
            {"prompt", "2+2?"},
            {"choices", {"3", "4", "5"}},
            {"answer_key", "4"},
            {"difficulty", 1}
        }
    });
    replace_chapter_questions(ch.id, quiz);
    auto questions = list_chapter_questions(ch.id);
    ASSERT_EQ(questions.size(), 1u);

    std::vector<ProviderInfo> providers;
    auto ok = grade_answer(questions[0], "4", "", "", providers);
    EXPECT_TRUE(ok.correct);
    EXPECT_DOUBLE_EQ(ok.score, 1.0);

    auto bad = grade_answer(questions[0], "3", "", "", providers);
    EXPECT_FALSE(bad.correct);
    EXPECT_DOUBLE_EQ(bad.score, 0.0);
}

TEST_F(StudyAssistantTest, GradesShortSubstringMatch) {
    auto course = upsert_course("C2", "/c2");
    auto ch = upsert_chapter(course.id, "ch1", "Ch1", "/c2/ch1", 0);
    nlohmann::json quiz = nlohmann::json::array({
        {
            {"topic", "geo"},
            {"type", "short"},
            {"prompt", "capital?"},
            {"answer_key", "Paris"},
            {"difficulty", 1}
        }
    });
    replace_chapter_questions(ch.id, quiz);
    auto questions = list_chapter_questions(ch.id);
    ASSERT_EQ(questions.size(), 1u);

    std::vector<ProviderInfo> providers;
    auto ok = grade_answer(questions[0], "paris", "", "", providers);
    EXPECT_TRUE(ok.correct);
}

}  // namespace
}  // namespace study
}  // namespace qcode
