#include <gtest/gtest.h>
#include <qcode/session/study_store.h>
#include <qcode/session/session_store.h>

#include <atomic>
#include <filesystem>
#include <string>

namespace qcode {
namespace study {
namespace {

static std::atomic<int> s_db_counter{0};

class StudyStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        std::filesystem::create_directories("/tmp/qcode_study_test", ec);
        db_path_ = "/tmp/qcode_study_test/test_" + std::to_string(++s_db_counter) + ".db";
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

TEST_F(StudyStoreTest, CourseUpsertAndList) {
    auto c1 = upsert_course("Algorithms", "/workspace/algo");
    EXPECT_FALSE(c1.id.empty());
    EXPECT_EQ(c1.title, "Algorithms");
    EXPECT_EQ(c1.root_path, "/workspace/algo");

    // Upserting with same path updates title
    auto c2 = upsert_course("Data Structures & Algorithms", "/workspace/algo");
    EXPECT_EQ(c2.id, c1.id);
    EXPECT_EQ(c2.title, "Data Structures & Algorithms");

    auto courses = list_courses();
    ASSERT_EQ(courses.size(), 1u);
    EXPECT_EQ(courses[0].id, c1.id);

    auto fetched = get_course(c1.id);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->title, "Data Structures & Algorithms");
}

TEST_F(StudyStoreTest, ChapterUpsertAndList) {
    auto course = upsert_course("Physics", "/workspace/physics");
    auto ch1 = upsert_chapter(course.id, "kinematics", "Kinematics", "/workspace/physics/ch1", 0);
    auto ch2 = upsert_chapter(course.id, "dynamics", "Dynamics", "/workspace/physics/ch2", 1);

    auto chapters = list_chapters(course.id);
    ASSERT_EQ(chapters.size(), 2u);
    EXPECT_EQ(chapters[0].slug, "kinematics");
    EXPECT_EQ(chapters[1].slug, "dynamics");

    auto fetched_ch = get_chapter(ch1.id);
    ASSERT_TRUE(fetched_ch.has_value());
    EXPECT_EQ(fetched_ch->title, "Kinematics");

    clear_course_chapters(course.id);
    EXPECT_TRUE(list_chapters(course.id).empty());
}

TEST_F(StudyStoreTest, TopicAndQuestionFlow) {
    auto course = upsert_course("CS", "/workspace/cs");
    auto ch = upsert_chapter(course.id, "os", "Operating Systems", "/workspace/cs/os", 0);

    auto topic = upsert_topic(ch.id, "Deadlocks");
    EXPECT_FALSE(topic.id.empty());
    EXPECT_EQ(topic.name, "Deadlocks");

    auto topics = list_topics(ch.id);
    ASSERT_EQ(topics.size(), 1u);
    EXPECT_EQ(topics[0].name, "Deadlocks");

    nlohmann::json quiz = nlohmann::json::array({
        {
            {"topic", "Deadlocks"},
            {"type", "mcq"},
            {"prompt", "What is mutual exclusion?"},
            {"choices", {"A", "B", "C"}},
            {"answer_key", "A"},
            {"difficulty", 2}
        },
        {
            {"topic", "Virtual Memory"},
            {"type", "short"},
            {"prompt", "Explain paging."},
            {"answer_key", "Paging maps virtual to physical pages."},
            {"difficulty", 3}
        }
    });

    replace_chapter_questions(ch.id, quiz);
    auto questions = list_chapter_questions(ch.id);
    ASSERT_EQ(questions.size(), 2u);

    auto q1 = questions[0];
    EXPECT_EQ(q1.chapter_id, ch.id);
    auto fetched_q = get_question(q1.id);
    ASSERT_TRUE(fetched_q.has_value());
    EXPECT_EQ(fetched_q->id, q1.id);

    // Record attempt
    auto attempt = record_attempt(q1.id, "A", true, 1.0, "Correct!");
    EXPECT_EQ(attempt.question_id, q1.id);
    EXPECT_TRUE(attempt.correct);
    EXPECT_DOUBLE_EQ(attempt.score, 1.0);

    // Mastery calculation and weak topics
    update_topic_mastery(q1.topic_id, 1.0);
    auto plan = next_weak_topics(course.id, 5);
    EXPECT_LE(plan.suggested_count, 5);
}

TEST(StudyStoreHelpersTest, QuestionTypeConversion) {
    EXPECT_EQ(question_type_to_string(QuestionType::kMcq), "mcq");
    EXPECT_EQ(question_type_to_string(QuestionType::kShort), "short");
    EXPECT_EQ(question_type_to_string(QuestionType::kLong), "long");

    EXPECT_EQ(question_type_from_string("mcq"), QuestionType::kMcq);
    EXPECT_EQ(question_type_from_string("short"), QuestionType::kShort);
    EXPECT_EQ(question_type_from_string("long"), QuestionType::kLong);
    EXPECT_EQ(question_type_from_string("unknown"), QuestionType::kShort);
}

TEST(StudyStoreHelpersTest, MarkdownToHtml) {
    std::string md = "# Heading 1\n\nSome **bold** and *italic* text.\n\n- item 1\n- item 2";
    std::string html = markdown_to_html(md);
    EXPECT_NE(html.find("<h1>"), std::string::npos);
    EXPECT_NE(html.find("<strong>bold</strong>"), std::string::npos);
    EXPECT_NE(html.find("<em>italic</em>"), std::string::npos);
}

} // namespace
} // namespace study
} // namespace qcode
