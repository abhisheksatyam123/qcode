#include "routes_internal.h"

#include <qcode/session/study_assistant.h>
#include <qcode/session/study_store.h>

#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace qcode {
namespace server {

void register_study_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers_list) {
  // ── Study Buddy routes ────────────────────────────────────────────
  svr.Get("/study/courses", [](const httplib::Request&, httplib::Response& res) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : qcode::study::list_courses()) {
      arr.push_back({{"id", c.id},
                     {"title", c.title},
                     {"root_path", c.root_path},
                     {"created_at", c.created_at}});
    }
    res.set_content(arr.dump(2), "application/json");
  });

  svr.Post("/study/courses/ingest", [](const httplib::Request& req,
                                       httplib::Response& res) {
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid json"})", "application/json");
      return;
    }
    const std::string root =
        body.value("root_path", "/sdcard/Documents/notes");
    const std::string title = body.value("course_title", "");
    const std::string course_id = qcode::study::ingest_vault(root, title);
    if (course_id.empty()) {
      res.status = 500;
      res.set_content(R"({"error":"ingest failed"})", "application/json");
      return;
    }
    auto course = qcode::study::get_course(course_id);
    nlohmann::json chapters = nlohmann::json::array();
    for (const auto& ch : qcode::study::list_chapters(course_id)) {
      chapters.push_back({{"id", ch.id},
                          {"slug", ch.slug},
                          {"title", ch.title},
                          {"path", ch.path},
                          {"order_index", ch.order_index},
                          {"mastery", ch.mastery}});
    }
    nlohmann::json out = {
        {"ok", true},
        {"course",
         {{"id", course_id},
          {"title", course ? course->title : title},
          {"root_path", course ? course->root_path : root}}},
        {"chapters", chapters},
    };
    res.set_content(out.dump(2), "application/json");
  });

  svr.Get("/study/courses/([^/]+)/chapters",
          [](const httplib::Request& req, httplib::Response& res) {
            const std::string course_id = req.matches[1];
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& ch : qcode::study::list_chapters(course_id)) {
              arr.push_back({{"id", ch.id},
                             {"course_id", ch.course_id},
                             {"slug", ch.slug},
                             {"title", ch.title},
                             {"path", ch.path},
                             {"order_index", ch.order_index},
                             {"mastery", ch.mastery}});
            }
            res.set_content(arr.dump(2), "application/json");
          });

  svr.Get("/study/courses/([^/]+)/next",
          [](const httplib::Request& req, httplib::Response& res) {
            const std::string course_id = req.matches[1];
            int limit = 8;
            if (req.has_param("limit")) {
              try {
                limit = std::stoi(req.get_param_value("limit"));
              } catch (...) {
              }
            }
            auto plan = qcode::study::next_weak_topics(course_id, limit);
            nlohmann::json topics = nlohmann::json::array();
            for (size_t i = 0; i < plan.weak_topic_ids.size(); ++i) {
              topics.push_back({{"id", plan.weak_topic_ids[i]},
                                {"name", plan.weak_topic_names[i]}});
            }
            nlohmann::json out = {{"topics", topics},
                                  {"suggested_count", plan.suggested_count}};
            res.set_content(out.dump(2), "application/json");
          });

  svr.Post("/study/chapters/([^/]+)/prepare",
           [providers_list](const httplib::Request& req,
                            httplib::Response& res) {
             const std::string chapter_id = req.matches[1];
             nlohmann::json body;
             try {
               body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
             } catch (...) {
               res.status = 400;
               res.set_content(R"({"error":"invalid json"})",
                               "application/json");
               return;
             }
             std::string provider = body.value("provider", "");
             std::string model = body.value("model", "");
             if ((provider.empty() || model.empty()) && providers_list &&
                 !providers_list->empty()) {
               const auto& p = (*providers_list)[0];
               if (provider.empty()) provider = p.id;
               if (model.empty() && !p.models.empty()) model = p.models[0].id;
             }
             if (provider.empty() || model.empty()) {
               res.status = 400;
               res.set_content(R"({"error":"provider/model required"})",
                               "application/json");
               return;
             }
             auto prepared = qcode::study::prepare_chapter_with_llm(
                 chapter_id, provider, model,
                 providers_list ? *providers_list
                                : std::vector<qcode::ProviderInfo>{});
             if (!prepared.ok) {
               res.status = 500;
               res.set_content(
                   nlohmann::json({{"error", prepared.error}}).dump(),
                   "application/json");
               return;
             }
             nlohmann::json out = {{"ok", true},
                                   {"summary_md", prepared.summary_md},
                                   {"quiz", prepared.quiz_json},
                                   {"question_count",
                                    prepared.quiz_json.is_array()
                                        ? prepared.quiz_json.size()
                                        : 0}};
             res.set_content(out.dump(2), "application/json");
           });

  svr.Get("/study/chapters/([^/]+)/quiz",
          [](const httplib::Request& req, httplib::Response& res) {
            const std::string chapter_id = req.matches[1];
            auto chapter = qcode::study::get_chapter(chapter_id);
            if (!chapter) {
              res.status = 404;
              res.set_content(R"({"error":"chapter not found"})",
                              "application/json");
              return;
            }
            std::vector<std::string> topic_filter;
            if (req.has_param("topics")) {
              std::stringstream ss(req.get_param_value("topics"));
              std::string part;
              while (std::getline(ss, part, ',')) {
                if (!part.empty()) topic_filter.push_back(part);
              }
            }
            int limit = 20;
            if (req.has_param("limit")) {
              try {
                limit = std::stoi(req.get_param_value("limit"));
              } catch (...) {
              }
            }
            auto questions = topic_filter.empty()
                                 ? qcode::study::list_chapter_questions(chapter_id)
                                 : qcode::study::list_questions_for_topics(
                                       chapter_id, topic_filter, limit);
            if (static_cast<int>(questions.size()) > limit) {
              questions.resize(limit);
            }
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& q : questions) {
              nlohmann::json choices = nlohmann::json::array();
              try {
                choices = nlohmann::json::parse(
                    q.choices_json.empty() ? "[]" : q.choices_json);
              } catch (...) {
              }
              arr.push_back(
                  {{"id", q.id},
                   {"type", qcode::study::question_type_to_string(q.type)},
                   {"prompt_html", q.prompt_html},
                   {"choices", choices},
                   {"topic", q.topic_name},
                   {"topic_id", q.topic_id},
                   {"difficulty", q.difficulty}});
            }
            nlohmann::json out = {{"chapter_id", chapter_id},
                                  {"title", chapter->title},
                                  {"path", chapter->path},
                                  {"questions", arr}};
            res.set_content(out.dump(2), "application/json");
          });

  svr.Post("/study/quiz/submit",
           [providers_list](const httplib::Request& req,
                            httplib::Response& res) {
             nlohmann::json body;
             try {
               body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
             } catch (...) {
               res.status = 400;
               res.set_content(R"({"error":"invalid json"})",
                               "application/json");
               return;
             }
             std::string provider = body.value("provider", "");
             std::string model = body.value("model", "");
             if ((provider.empty() || model.empty()) && providers_list &&
                 !providers_list->empty()) {
               const auto& p = (*providers_list)[0];
               if (provider.empty()) provider = p.id;
               if (model.empty() && !p.models.empty()) model = p.models[0].id;
             }
             nlohmann::json answers = body.contains("answers") &&
                                              body["answers"].is_array()
                                          ? body["answers"]
                                          : nlohmann::json::array();
             if (answers.empty() && body.contains("question_id")) {
               answers.push_back(
                   {{"question_id", body.value("question_id", "")},
                    {"answer", body.value("answer",
                                          body.value("student_answer", ""))}});
             }
             nlohmann::json results = nlohmann::json::array();
             for (const auto& a : answers) {
               const std::string qid = a.value("question_id", "");
               const std::string ans = a.value(
                   "answer", a.value("student_answer", std::string{}));
               auto q = qcode::study::get_question(qid);
               if (!q) {
                 results.push_back({{"question_id", qid},
                                    {"error", "question not found"}});
                 continue;
               }
               auto graded = qcode::study::grade_answer(
                   *q, ans, provider, model,
                   providers_list ? *providers_list
                                  : std::vector<qcode::ProviderInfo>{});
               results.push_back({{"question_id", graded.question_id},
                                  {"correct", graded.correct},
                                  {"score", graded.score},
                                  {"feedback", graded.feedback}});
             }
             res.set_content(
                 nlohmann::json({{"results", results}}).dump(2),
                 "application/json");
           });
}

}  // namespace server
}  // namespace qcode
