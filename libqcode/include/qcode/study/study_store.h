#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace qcode {
namespace study {

struct Course {
  std::string id;
  std::string title;
  std::string root_path;
  long long created_at = 0;
};

struct Chapter {
  std::string id;
  std::string course_id;
  std::string slug;
  std::string title;
  std::string path;
  int order_index = 0;
  double mastery = 0.0;  // average of topic masteries
};

struct Topic {
  std::string id;
  std::string chapter_id;
  std::string name;
  double mastery = 0.0;
  long long last_seen_at = 0;
};

enum class QuestionType { kMcq, kShort, kLong };

struct Question {
  std::string id;
  std::string chapter_id;
  std::string topic_id;
  QuestionType type = QuestionType::kShort;
  std::string prompt_html;
  std::string choices_json;  // JSON array for MCQ
  std::string answer_key;
  int difficulty = 1;
  std::string topic_name;
};

struct AttemptResult {
  std::string question_id;
  std::string student_answer;
  bool correct = false;
  double score = 0.0;
  std::string feedback;
};

struct NextQuizPlan {
  std::vector<std::string> weak_topic_ids;
  std::vector<std::string> weak_topic_names;
  int suggested_count = 5;
};

std::string question_type_to_string(QuestionType type);
QuestionType question_type_from_string(const std::string& s);

// ── Courses / chapters ──────────────────────────────────────────────
Course upsert_course(const std::string& title, const std::string& root_path);
std::vector<Course> list_courses();
std::optional<Course> get_course(const std::string& course_id);

Chapter upsert_chapter(const std::string& course_id, const std::string& slug,
                       const std::string& title, const std::string& path,
                       int order_index);
void clear_course_chapters(const std::string& course_id);
std::vector<Chapter> list_chapters(const std::string& course_id);
std::optional<Chapter> get_chapter(const std::string& chapter_id);

Topic upsert_topic(const std::string& chapter_id, const std::string& name);
std::vector<Topic> list_topics(const std::string& chapter_id);

// Replace all questions for a chapter from quiz.json payload.
void replace_chapter_questions(const std::string& chapter_id,
                               const nlohmann::json& quiz_json);

std::vector<Question> list_chapter_questions(const std::string& chapter_id);
std::vector<Question> list_questions_for_topics(
    const std::string& chapter_id, const std::vector<std::string>& topic_ids,
    int limit);

std::optional<Question> get_question(const std::string& question_id);

AttemptResult record_attempt(const std::string& question_id,
                             const std::string& student_answer, bool correct,
                             double score, const std::string& feedback);

void update_topic_mastery(const std::string& topic_id, double attempt_score);

NextQuizPlan next_weak_topics(const std::string& course_id, int limit = 8);

// Ingest MD/TXT/PDF under vault_root into study/<course>/chapters/...
// Returns the course id or empty on failure.
std::string ingest_vault(const std::string& vault_root,
                         const std::string& course_title);

// Lightweight markdown → HTML (headings, paragraphs, lists, code, bold/italic).
std::string markdown_to_html(const std::string& md);

}  // namespace study
}  // namespace qcode
