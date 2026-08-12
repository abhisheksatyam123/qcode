#include <qcode/study/study_store.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <unordered_set>

#include <sqlite3.h>
#include <uuid.h>

#include <qcode/logger/logger.h>

namespace qcode {
namespace study {
namespace {

namespace fs = std::filesystem;

std::string get_db_path() {
  if (const char* p = std::getenv("QCODE_DB_PATH")) return p;
  if (const char* p = std::getenv("OPENCODE_DB_PATH")) return p;
  const char* home = std::getenv("HOME");
  return home ? std::string(home) + "/.qcode.db" : ".qcode.db";
}

sqlite3* open_db() {
  std::string path = get_db_path();
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    LOG_ERROR("study_store: open failed: {}",
              db ? sqlite3_errmsg(db) : "unknown");
    if (db) sqlite3_close(db);
    return nullptr;
  }
  sqlite3_busy_timeout(db, 5000);
  sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
  return db;
}

bool prepare_stmt(sqlite3* db, const char* sql, sqlite3_stmt** stmt) {
  if (sqlite3_prepare_v2(db, sql, -1, stmt, nullptr) != SQLITE_OK) {
    LOG_ERROR("study_store: prepare failed: {}", sqlite3_errmsg(db));
    return false;
  }
  return true;
}

std::string generate_uuid() {
  static thread_local std::mt19937 engine{std::random_device{}()};
  static thread_local uuids::uuid_random_generator gen{engine};
  return uuids::to_string(gen());
}

long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string column_text(sqlite3_stmt* stmt, int col) {
  const unsigned char* t = sqlite3_column_text(stmt, col);
  return t ? reinterpret_cast<const char*>(t) : "";
}

std::string slugify(std::string s) {
  std::string out;
  out.reserve(s.size());
  bool last_dash = false;
  for (char c : s) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc)) {
      out.push_back(static_cast<char>(std::tolower(uc)));
      last_dash = false;
    } else if (!out.empty() && !last_dash) {
      out.push_back('-');
      last_dash = true;
    }
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  if (out.empty()) out = "chapter";
  if (out.size() > 48) out.resize(48);
  return out;
}

std::string escape_html(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

std::string inline_md(std::string s) {
  // Very small subset: **bold**, *italic*, `code`
  std::string out;
  out.reserve(s.size() + 16);
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '`' && i + 1 < s.size()) {
      const auto end = s.find('`', i + 1);
      if (end != std::string::npos) {
        out += "<code>" + escape_html(s.substr(i + 1, end - i - 1)) + "</code>";
        i = end + 1;
        continue;
      }
    }
    if (s.compare(i, 2, "**") == 0) {
      const auto end = s.find("**", i + 2);
      if (end != std::string::npos) {
        out += "<strong>" + escape_html(s.substr(i + 2, end - i - 2)) +
               "</strong>";
        i = end + 2;
        continue;
      }
    }
    if (s[i] == '*' && (i + 1 >= s.size() || s[i + 1] != '*')) {
      const auto end = s.find('*', i + 1);
      if (end != std::string::npos) {
        out += "<em>" + escape_html(s.substr(i + 1, end - i - 1)) + "</em>";
        i = end + 1;
        continue;
      }
    }
    out += escape_html(std::string(1, s[i]));
    ++i;
  }
  return out;
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_file(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << content;
}

std::string extract_pdf_text(const fs::path& pdf_path) {
  // Best-effort via python3 + pypdf when available.
  const std::string py =
      "import sys\n"
      "try:\n"
      "  from pypdf import PdfReader\n"
      "except Exception:\n"
      "  try:\n"
      "    from PyPDF2 import PdfReader\n"
      "  except Exception:\n"
      "    sys.exit(2)\n"
      "r=PdfReader(sys.argv[1])\n"
      "print('\\n'.join((p.extract_text() or '') for p in r.pages))\n";
  fs::path scratch = fs::temp_directory_path();
  if (const char* t = std::getenv("TMPDIR")) {
    if (*t) scratch = t;
  } else if (const char* home = std::getenv("HOME")) {
    scratch = fs::path(home) / "tmp";
  }
  std::error_code ec;
  fs::create_directories(scratch, ec);
  const fs::path script = scratch / ("qcode_pdf_" + generate_uuid() + ".py");
  write_file(script, py);
  const std::string cmd = "python3 \"" + script.string() + "\" \"" +
                          pdf_path.string() + "\" 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");
  std::string out;
  if (pipe) {
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);
  }
  fs::remove(script, ec);
  return out;
}

struct SourceChapter {
  std::string title;
  std::string content;
  fs::path source_path;
  bool needs_review = false;
};

std::vector<SourceChapter> split_markdown_chapters(const std::string& text,
                                                   const fs::path& source) {
  std::vector<SourceChapter> chapters;
  std::istringstream in(text);
  std::string line;
  std::string current_title =
      source.stem().string().empty() ? "Untitled" : source.stem().string();
  std::ostringstream body;
  auto flush = [&]() {
    const std::string b = body.str();
    if (b.find_first_not_of(" \t\r\n") == std::string::npos) return;
    chapters.push_back({current_title, b, source, false});
    body.str("");
    body.clear();
  };
  while (std::getline(in, line)) {
    if (line.size() >= 3 && line[0] == '#' && line[1] == '#' &&
        (line[2] == ' ' || line[2] == '#')) {
      // ## or ### heading starts a chapter section when body already large
      if (body.tellp() > 800) {
        flush();
        size_t i = 0;
        while (i < line.size() && line[i] == '#') ++i;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
          ++i;
        current_title = line.substr(i);
        if (current_title.empty()) current_title = "Section";
        continue;
      }
    }
    if (line.size() >= 2 && line[0] == '#' && line[1] == ' ' &&
        body.tellp() > 200) {
      flush();
      current_title = line.substr(2);
      continue;
    }
    body << line << '\n';
  }
  flush();
  if (chapters.empty()) {
    chapters.push_back({source.stem().string(), text, source, false});
  }
  return chapters;
}

void copy_source(const fs::path& src, const fs::path& dest_dir) {
  fs::create_directories(dest_dir);
  std::error_code ec;
  fs::copy_file(src, dest_dir / src.filename(),
                fs::copy_options::overwrite_existing, ec);
}

}  // namespace

std::string question_type_to_string(QuestionType type) {
  switch (type) {
    case QuestionType::kMcq:
      return "mcq";
    case QuestionType::kLong:
      return "long";
    case QuestionType::kShort:
    default:
      return "short";
  }
}

QuestionType question_type_from_string(const std::string& s) {
  if (s == "mcq") return QuestionType::kMcq;
  if (s == "long") return QuestionType::kLong;
  return QuestionType::kShort;
}

Course upsert_course(const std::string& title, const std::string& root_path) {
  Course c;
  sqlite3* db = open_db();
  if (!db) return c;

  // Prefer existing course with same root_path.
  sqlite3_stmt* stmt = nullptr;
  if (prepare_stmt(db,
                   "SELECT id, title, root_path, created_at FROM study_courses "
                   "WHERE root_path = ? LIMIT 1;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, root_path.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      c.id = column_text(stmt, 0);
      c.title = column_text(stmt, 1);
      c.root_path = column_text(stmt, 2);
      c.created_at = sqlite3_column_int64(stmt, 3);
      sqlite3_finalize(stmt);
      if (!title.empty() && title != c.title) {
        sqlite3_stmt* upd = nullptr;
        if (prepare_stmt(db, "UPDATE study_courses SET title = ? WHERE id = ?;",
                         &upd)) {
          sqlite3_bind_text(upd, 1, title.c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_bind_text(upd, 2, c.id.c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_step(upd);
          sqlite3_finalize(upd);
          c.title = title;
        }
      }
      sqlite3_close(db);
      return c;
    }
    sqlite3_finalize(stmt);
  }

  c.id = generate_uuid();
  c.title = title.empty() ? "Study Course" : title;
  c.root_path = root_path;
  c.created_at = now_ms();
  if (prepare_stmt(db,
                   "INSERT INTO study_courses (id, title, root_path, created_at) "
                   "VALUES (?, ?, ?, ?);",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, c.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, c.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, c.root_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, c.created_at);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG_ERROR("study_store: insert course failed: {}", sqlite3_errmsg(db));
      c.id.clear();
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return c;
}

std::vector<Course> list_courses() {
  std::vector<Course> out;
  sqlite3* db = open_db();
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (prepare_stmt(db,
                   "SELECT id, title, root_path, created_at FROM study_courses "
                   "ORDER BY created_at DESC;",
                   &stmt)) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      Course c;
      c.id = column_text(stmt, 0);
      c.title = column_text(stmt, 1);
      c.root_path = column_text(stmt, 2);
      c.created_at = sqlite3_column_int64(stmt, 3);
      out.push_back(std::move(c));
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return out;
}

std::optional<Course> get_course(const std::string& course_id) {
  sqlite3* db = open_db();
  if (!db) return std::nullopt;
  sqlite3_stmt* stmt = nullptr;
  std::optional<Course> out;
  if (prepare_stmt(db,
                   "SELECT id, title, root_path, created_at FROM study_courses "
                   "WHERE id = ?;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, course_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      Course c;
      c.id = column_text(stmt, 0);
      c.title = column_text(stmt, 1);
      c.root_path = column_text(stmt, 2);
      c.created_at = sqlite3_column_int64(stmt, 3);
      out = std::move(c);
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return out;
}

Chapter upsert_chapter(const std::string& course_id, const std::string& slug,
                       const std::string& title, const std::string& path,
                       int order_index) {
  Chapter ch;
  sqlite3* db = open_db();
  if (!db) return ch;

  sqlite3_stmt* stmt = nullptr;
  if (prepare_stmt(db,
                   "SELECT id, course_id, slug, title, path, order_index FROM "
                   "study_chapters WHERE course_id = ? AND slug = ? LIMIT 1;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, course_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, slug.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      ch.id = column_text(stmt, 0);
      ch.course_id = column_text(stmt, 1);
      ch.slug = column_text(stmt, 2);
      ch.title = column_text(stmt, 3);
      ch.path = column_text(stmt, 4);
      ch.order_index = sqlite3_column_int(stmt, 5);
      sqlite3_finalize(stmt);
      sqlite3_stmt* upd = nullptr;
      if (prepare_stmt(db,
                       "UPDATE study_chapters SET title = ?, path = ?, "
                       "order_index = ? WHERE id = ?;",
                       &upd)) {
        sqlite3_bind_text(upd, 1, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 2, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(upd, 3, order_index);
        sqlite3_bind_text(upd, 4, ch.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(upd);
        sqlite3_finalize(upd);
      }
      ch.title = title;
      ch.path = path;
      ch.order_index = order_index;
      sqlite3_close(db);
      return ch;
    }
    sqlite3_finalize(stmt);
  }

  ch.id = generate_uuid();
  ch.course_id = course_id;
  ch.slug = slug;
  ch.title = title;
  ch.path = path;
  ch.order_index = order_index;
  if (prepare_stmt(db,
                   "INSERT INTO study_chapters "
                   "(id, course_id, slug, title, path, order_index) "
                   "VALUES (?, ?, ?, ?, ?, ?);",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, ch.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, course_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, slug.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, order_index);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG_ERROR("study_store: insert chapter failed: {}", sqlite3_errmsg(db));
      ch.id.clear();
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return ch;
}

void clear_course_chapters(const std::string& course_id) {
  sqlite3* db = open_db();
  if (!db) return;
  // Cascades remove topics/questions/attempts via FK when enabled.
  sqlite3_stmt* stmt = nullptr;
  if (prepare_stmt(db, "DELETE FROM study_chapters WHERE course_id = ?;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, course_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
}

std::vector<Chapter> list_chapters(const std::string& course_id) {
  std::vector<Chapter> out;
  sqlite3* db = open_db();
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (prepare_stmt(db,
                   "SELECT c.id, c.course_id, c.slug, c.title, c.path, "
                   "c.order_index, "
                   "COALESCE((SELECT AVG(t.mastery) FROM study_topics t "
                   "WHERE t.chapter_id = c.id), 0) "
                   "FROM study_chapters c WHERE c.course_id = ? "
                   "ORDER BY c.order_index ASC, c.title ASC;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, course_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      Chapter ch;
      ch.id = column_text(stmt, 0);
      ch.course_id = column_text(stmt, 1);
      ch.slug = column_text(stmt, 2);
      ch.title = column_text(stmt, 3);
      ch.path = column_text(stmt, 4);
      ch.order_index = sqlite3_column_int(stmt, 5);
      ch.mastery = sqlite3_column_double(stmt, 6);
      out.push_back(std::move(ch));
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return out;
}

std::optional<Chapter> get_chapter(const std::string& chapter_id) {
  sqlite3* db = open_db();
  if (!db) return std::nullopt;
  sqlite3_stmt* stmt = nullptr;
  std::optional<Chapter> out;
  if (prepare_stmt(db,
                   "SELECT id, course_id, slug, title, path, order_index FROM "
                   "study_chapters WHERE id = ?;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, chapter_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      Chapter ch;
      ch.id = column_text(stmt, 0);
      ch.course_id = column_text(stmt, 1);
      ch.slug = column_text(stmt, 2);
      ch.title = column_text(stmt, 3);
      ch.path = column_text(stmt, 4);
      ch.order_index = sqlite3_column_int(stmt, 5);
      out = std::move(ch);
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return out;
}

Topic upsert_topic(const std::string& chapter_id, const std::string& name) {
  Topic t;
  sqlite3* db = open_db();
  if (!db) return t;
  sqlite3_stmt* stmt = nullptr;
  if (prepare_stmt(db,
                   "SELECT id, chapter_id, name, mastery, last_seen_at FROM "
                   "study_topics WHERE chapter_id = ? AND name = ? LIMIT 1;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, chapter_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      t.id = column_text(stmt, 0);
      t.chapter_id = column_text(stmt, 1);
      t.name = column_text(stmt, 2);
      t.mastery = sqlite3_column_double(stmt, 3);
      t.last_seen_at = sqlite3_column_int64(stmt, 4);
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return t;
    }
    sqlite3_finalize(stmt);
  }
  t.id = generate_uuid();
  t.chapter_id = chapter_id;
  t.name = name;
  if (prepare_stmt(db,
                   "INSERT INTO study_topics "
                   "(id, chapter_id, name, mastery, last_seen_at) "
                   "VALUES (?, ?, ?, 0, 0);",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, t.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, chapter_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG_ERROR("study_store: insert topic failed: {}", sqlite3_errmsg(db));
      t.id.clear();
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return t;
}

std::vector<Topic> list_topics(const std::string& chapter_id) {
  std::vector<Topic> out;
  sqlite3* db = open_db();
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (prepare_stmt(db,
                   "SELECT id, chapter_id, name, mastery, last_seen_at FROM "
                   "study_topics WHERE chapter_id = ? ORDER BY mastery ASC, "
                   "name ASC;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, chapter_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      Topic t;
      t.id = column_text(stmt, 0);
      t.chapter_id = column_text(stmt, 1);
      t.name = column_text(stmt, 2);
      t.mastery = sqlite3_column_double(stmt, 3);
      t.last_seen_at = sqlite3_column_int64(stmt, 4);
      out.push_back(std::move(t));
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return out;
}

void replace_chapter_questions(const std::string& chapter_id,
                               const nlohmann::json& quiz_json) {
  sqlite3* db = open_db();
  if (!db) return;
  sqlite3_stmt* del = nullptr;
  if (prepare_stmt(db, "DELETE FROM study_questions WHERE chapter_id = ?;",
                   &del)) {
    sqlite3_bind_text(del, 1, chapter_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(del);
    sqlite3_finalize(del);
  }
  sqlite3_close(db);

  if (!quiz_json.is_array()) return;
  for (const auto& q : quiz_json) {
    if (!q.is_object()) continue;
    const std::string topic_name =
        q.value("topic", q.value("topic_name", "General"));
    Topic topic = upsert_topic(chapter_id, topic_name);
    const std::string type = q.value("type", "short");
    std::string prompt = q.value("prompt_html", "");
    if (prompt.empty()) {
      const std::string md = q.value("prompt", q.value("question", ""));
      prompt = markdown_to_html(md);
    }
    std::string choices = "[]";
    if (q.contains("choices") && q["choices"].is_array()) {
      choices = q["choices"].dump();
    }
    const std::string answer = q.value("answer_key", q.value("answer", ""));
    const int difficulty = q.value("difficulty", 1);

    sqlite3* db2 = open_db();
    if (!db2) continue;
    sqlite3_stmt* ins = nullptr;
    const std::string id = generate_uuid();
    if (prepare_stmt(db2,
                     "INSERT INTO study_questions "
                     "(id, chapter_id, topic_id, type, prompt_html, "
                     "choices_json, answer_key, difficulty) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
                     &ins)) {
      sqlite3_bind_text(ins, 1, id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins, 2, chapter_id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins, 3, topic.id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins, 4, type.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins, 5, prompt.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins, 6, choices.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins, 7, answer.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(ins, 8, difficulty);
      sqlite3_step(ins);
      sqlite3_finalize(ins);
    }
    sqlite3_close(db2);
  }
}

std::vector<Question> list_chapter_questions(const std::string& chapter_id) {
  std::vector<Question> out;
  sqlite3* db = open_db();
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (prepare_stmt(db,
                   "SELECT q.id, q.chapter_id, q.topic_id, q.type, "
                   "q.prompt_html, q.choices_json, q.answer_key, q.difficulty, "
                   "COALESCE(t.name, '') "
                   "FROM study_questions q "
                   "LEFT JOIN study_topics t ON t.id = q.topic_id "
                   "WHERE q.chapter_id = ? ORDER BY q.difficulty ASC;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, chapter_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      Question q;
      q.id = column_text(stmt, 0);
      q.chapter_id = column_text(stmt, 1);
      q.topic_id = column_text(stmt, 2);
      q.type = question_type_from_string(column_text(stmt, 3));
      q.prompt_html = column_text(stmt, 4);
      q.choices_json = column_text(stmt, 5);
      q.answer_key = column_text(stmt, 6);
      q.difficulty = sqlite3_column_int(stmt, 7);
      q.topic_name = column_text(stmt, 8);
      out.push_back(std::move(q));
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return out;
}

std::vector<Question> list_questions_for_topics(
    const std::string& chapter_id, const std::vector<std::string>& topic_ids,
    int limit) {
  auto all = list_chapter_questions(chapter_id);
  if (topic_ids.empty()) {
    if (static_cast<int>(all.size()) > limit) all.resize(limit);
    return all;
  }
  std::unordered_set<std::string> want(topic_ids.begin(), topic_ids.end());
  std::vector<Question> filtered;
  for (auto& q : all) {
    if (want.count(q.topic_id)) filtered.push_back(std::move(q));
    if (static_cast<int>(filtered.size()) >= limit) break;
  }
  return filtered;
}

std::optional<Question> get_question(const std::string& question_id) {
  sqlite3* db = open_db();
  if (!db) return std::nullopt;
  sqlite3_stmt* stmt = nullptr;
  std::optional<Question> out;
  if (prepare_stmt(db,
                   "SELECT q.id, q.chapter_id, q.topic_id, q.type, "
                   "q.prompt_html, q.choices_json, q.answer_key, q.difficulty, "
                   "COALESCE(t.name, '') "
                   "FROM study_questions q "
                   "LEFT JOIN study_topics t ON t.id = q.topic_id "
                   "WHERE q.id = ?;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, question_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      Question q;
      q.id = column_text(stmt, 0);
      q.chapter_id = column_text(stmt, 1);
      q.topic_id = column_text(stmt, 2);
      q.type = question_type_from_string(column_text(stmt, 3));
      q.prompt_html = column_text(stmt, 4);
      q.choices_json = column_text(stmt, 5);
      q.answer_key = column_text(stmt, 6);
      q.difficulty = sqlite3_column_int(stmt, 7);
      q.topic_name = column_text(stmt, 8);
      out = std::move(q);
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return out;
}

AttemptResult record_attempt(const std::string& question_id,
                             const std::string& student_answer, bool correct,
                             double score, const std::string& feedback) {
  AttemptResult r;
  r.question_id = question_id;
  r.student_answer = student_answer;
  r.correct = correct;
  r.score = score;
  r.feedback = feedback;

  sqlite3* db = open_db();
  if (!db) return r;
  const std::string id = generate_uuid();
  sqlite3_stmt* stmt = nullptr;
  if (prepare_stmt(db,
                   "INSERT INTO study_attempts "
                   "(id, question_id, student_answer, correct, score, feedback, "
                   "created_at) VALUES (?, ?, ?, ?, ?, ?, ?);",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, question_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, student_answer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, correct ? 1 : 0);
    sqlite3_bind_double(stmt, 5, score);
    sqlite3_bind_text(stmt, 6, feedback.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, now_ms());
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);

  if (auto q = get_question(question_id)) {
    if (!q->topic_id.empty()) update_topic_mastery(q->topic_id, score);
  }
  return r;
}

void update_topic_mastery(const std::string& topic_id, double attempt_score) {
  constexpr double kAlpha = 0.35;
  sqlite3* db = open_db();
  if (!db) return;
  sqlite3_stmt* sel = nullptr;
  double mastery = 0.0;
  if (prepare_stmt(db, "SELECT mastery FROM study_topics WHERE id = ?;",
                   &sel)) {
    sqlite3_bind_text(sel, 1, topic_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(sel) == SQLITE_ROW) {
      mastery = sqlite3_column_double(sel, 0);
    }
    sqlite3_finalize(sel);
  }
  const double next =
      (mastery <= 0.0 && attempt_score >= 0.0)
          ? attempt_score
          : (kAlpha * attempt_score + (1.0 - kAlpha) * mastery);
  sqlite3_stmt* upd = nullptr;
  if (prepare_stmt(db,
                   "UPDATE study_topics SET mastery = ?, last_seen_at = ? "
                   "WHERE id = ?;",
                   &upd)) {
    sqlite3_bind_double(upd, 1, next);
    sqlite3_bind_int64(upd, 2, now_ms());
    sqlite3_bind_text(upd, 3, topic_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(upd);
    sqlite3_finalize(upd);
  }
  sqlite3_close(db);
}

NextQuizPlan next_weak_topics(const std::string& course_id, int limit) {
  NextQuizPlan plan;
  plan.suggested_count = std::max(3, std::min(limit, 10));
  sqlite3* db = open_db();
  if (!db) return plan;
  sqlite3_stmt* stmt = nullptr;
  // Weak: mastery < 0.6 OR never seen (last_seen_at = 0), ordered weakest first.
  if (prepare_stmt(db,
                   "SELECT t.id, t.name FROM study_topics t "
                   "JOIN study_chapters c ON c.id = t.chapter_id "
                   "WHERE c.course_id = ? AND (t.mastery < 0.6 OR t.last_seen_at = 0) "
                   "ORDER BY t.last_seen_at ASC, t.mastery ASC LIMIT ?;",
                   &stmt)) {
    sqlite3_bind_text(stmt, 1, course_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      plan.weak_topic_ids.push_back(column_text(stmt, 0));
      plan.weak_topic_names.push_back(column_text(stmt, 1));
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return plan;
}

std::string markdown_to_html(const std::string& md) {
  std::istringstream in(md);
  std::ostringstream html;
  std::string line;
  bool in_ul = false;
  bool in_code = false;
  auto close_ul = [&]() {
    if (in_ul) {
      html << "</ul>\n";
      in_ul = false;
    }
  };
  while (std::getline(in, line)) {
    if (line.rfind("```", 0) == 0) {
      close_ul();
      if (!in_code) {
        html << "<pre><code>";
        in_code = true;
      } else {
        html << "</code></pre>\n";
        in_code = false;
      }
      continue;
    }
    if (in_code) {
      html << escape_html(line) << '\n';
      continue;
    }
    if (line.empty()) {
      close_ul();
      continue;
    }
    if (line.rfind("### ", 0) == 0) {
      close_ul();
      html << "<h3>" << inline_md(line.substr(4)) << "</h3>\n";
      continue;
    }
    if (line.rfind("## ", 0) == 0) {
      close_ul();
      html << "<h2>" << inline_md(line.substr(3)) << "</h2>\n";
      continue;
    }
    if (line.rfind("# ", 0) == 0) {
      close_ul();
      html << "<h1>" << inline_md(line.substr(2)) << "</h1>\n";
      continue;
    }
    if ((line.size() >= 2 && (line[0] == '-' || line[0] == '*') &&
         line[1] == ' ') ||
        (line.size() >= 3 && std::isdigit(static_cast<unsigned char>(line[0])) &&
         line[1] == '.' && line[2] == ' ')) {
      if (!in_ul) {
        html << "<ul>\n";
        in_ul = true;
      }
      const size_t start = (line[0] == '-' || line[0] == '*') ? 2 : 3;
      html << "<li>" << inline_md(line.substr(start)) << "</li>\n";
      continue;
    }
    close_ul();
    html << "<p>" << inline_md(line) << "</p>\n";
  }
  close_ul();
  if (in_code) html << "</code></pre>\n";
  return html.str();
}

std::string ingest_vault(const std::string& vault_root,
                         const std::string& course_title) {
  std::error_code ec;
  fs::path root = vault_root;
  if (!root.is_absolute()) root = fs::absolute(root, ec);
  // Create vault if missing — Study Buddy uses the existing notes layout.
  if (!fs::exists(root) || !fs::is_directory(root)) {
    fs::create_directories(root, ec);
    if (ec || !fs::is_directory(root)) {
      LOG_ERROR("study ingest: cannot create vault: {}", vault_root);
      return "";
    }
  }
  fs::create_directories(root / "tools", ec);

  Course course = upsert_course(
      course_title.empty() ? root.filename().string() : course_title,
      root.string());
  if (course.id.empty()) return "";

  // Refresh replaces the previous index so old study/ copies do not linger.
  clear_course_chapters(course.id);

  const std::unordered_set<std::string> skip_dirs = {
      ".git", ".obsidian", ".trash", "tools", "study", "__pycache__"};

  auto should_skip = [&](const fs::path& p) -> bool {
    const auto rel = fs::relative(p, root, ec);
    if (ec) return true;
    for (const auto& part : rel) {
      if (skip_dirs.count(part.string())) return true;
    }
    return false;
  };

  int order = 0;
  std::unordered_set<std::string> seen_slugs;

  auto register_note = [&](const fs::path& file) {
    if (!fs::is_regular_file(file, ec)) return;
    if (should_skip(file)) return;
    auto ext = file.extension().string();
    for (char& c : ext)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext != ".md" && ext != ".txt" && ext != ".markdown" && ext != ".pdf") {
      return;
    }
    // Skip generated study sidecars.
    const auto name = file.filename().string();
    if (name == "summary.md" || name == "quiz.json" || name == "quiz.html" ||
        name == "content.md") {
      return;
    }
    if (name.find(".extracted.") != std::string::npos) return;

    ++order;
    const std::string rel = fs::relative(file, root, ec).generic_string();
    std::string slug = slugify(rel);
    if (slug.empty()) slug = slugify(file.stem().string());
    // Keep slug unique.
    std::string unique = slug;
    int n = 2;
    while (!seen_slugs.insert(unique).second) {
      unique = slug + "-" + std::to_string(n++);
    }
    slug = unique;

    // Chapter path = the note file itself (existing vault layout; no copy).
    const fs::path chapter_path = file;
    const std::string title = file.stem().string();

    // For PDFs, best-effort extract beside the file via tools/extract_pdf.py.
    if (ext == ".pdf") {
      const fs::path tool = root / "tools" / "extract_pdf.py";
      const fs::path extracted =
          file.parent_path() / (file.stem().string() + ".extracted.md");
      if (fs::exists(tool) && !fs::exists(extracted)) {
        const std::string cmd = "python3 \"" + tool.string() + "\" \"" +
                                file.string() + "\" \"" + extracted.string() +
                                "\" 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) pclose(pipe);
      }
    }

    auto chapter =
        upsert_chapter(course.id, slug, title, chapter_path.string(), order);
    if (!chapter.id.empty()) {
      upsert_topic(chapter.id, "General");
    }
  };

  for (auto it = fs::recursive_directory_iterator(
           root, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    register_note(it->path());
  }

  // Lightweight index for the agent (optional, vault-native).
  nlohmann::json index = {
      {"id", course.id},
      {"title", course.title},
      {"root_path", course.root_path},
      {"created_at", course.created_at},
      {"chapter_count", order},
  };
  write_file(root / "tools" / "course_index.json", index.dump(2));

  LOG_INFO("study ingest: course {} indexed {} notes in-place under {}",
           course.id, order, root.string());
  return course.id;
}

}  // namespace study
}  // namespace qcode
