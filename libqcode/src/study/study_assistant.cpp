#include <qcode/study/study_assistant.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <qcode/logger/logger.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/providers/registry.h>
#include <qcode/types/client.h>
#include <qcode/types/generate_options.h>
#include <qcode/types/message.h>

namespace qcode {
namespace study {
namespace {

namespace fs = std::filesystem;

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

std::optional<Client> make_client(const std::string& provider_id,
                                  const std::vector<ProviderInfo>& providers,
                                  std::string& error) {
  const ProviderInfo* sel = nullptr;
  for (const auto& p : providers) {
    if (p.id == provider_id) {
      sel = &p;
      break;
    }
  }
  if (!sel) {
    error = "unknown provider: " + provider_id;
    return std::nullopt;
  }
  providers::register_authenticated_providers();
  providers::ProviderOptions opts;
  opts.base_url = sel->api_url;
  opts.api_key = sel->api_key;
  opts.headers = sel->headers;
  opts.protocol = sel->protocol;
  opts.project_id = sel->project_id;
  auto resolution = providers::ProviderRegistry::instance().resolve(sel->id, opts);
  if (!resolution.ok()) {
    error = resolution.error;
    return std::nullopt;
  }
  return std::move(resolution.client);
}

nlohmann::json extract_json_array(const std::string& text) {
  const auto start = text.find('[');
  const auto end = text.rfind(']');
  if (start == std::string::npos || end == std::string::npos || end <= start) {
    return nlohmann::json();
  }
  try {
    return nlohmann::json::parse(text.substr(start, end - start + 1));
  } catch (...) {
    return nlohmann::json();
  }
}

nlohmann::json extract_json_object(const std::string& text) {
  const auto start = text.find('{');
  const auto end = text.rfind('}');
  if (start == std::string::npos || end == std::string::npos || end <= start) {
    return nlohmann::json();
  }
  try {
    return nlohmann::json::parse(text.substr(start, end - start + 1));
  } catch (...) {
    return nlohmann::json();
  }
}

}  // namespace

std::string study_identity() {
  return R"STUDY(### Identity

You are a patient Study Buddy for the student's notes vault. Help them learn
class-wise subjects and chapter material, quiz them, and close coverage gaps.
Do not act like a software-engineering agent unless they explicitly ask to code.

### Curriculum layout (source of truth)

Study content lives under `classes/` (workspace = notes vault root):

```text
classes/<class>/<subject>/<chapter>/
  content.md   quiz.json   quiz.html   _meta.json   _progress.json
classes/<class>/<subject>/quizzes/<overall-quiz>/
  quiz.json   _meta.json   _progress.json
classes/_curriculum.json
```

Example: `classes/class-9/mathematics/ch01-coordinates/`.

Personal folders (`atomic/`, `journal/`, `scratchpad/`, …) are not curriculum.

### Progress & gaps

Before planning study work, run:
`python3 tools/curriculum_status.py .`
(Use `--json` for machine-readable output; `--write` refreshes rollups.)

Use that report to see:
- how many classes / subjects / chapters exist
- chapter status: `not_started` | `in_progress` | `mastered`
- attempts / correct / mastery and remaining chapters
- weak chapters (not started or mastery < 0.6)

Record graded attempts with:
`python3 tools/record_attempt.py --path classes/.../<chapter> --question-id q1 --correct 1 --topic <topic> --refresh`

Scaffold new chapters with:
`python3 tools/scaffold_chapter.py --class 9 --subject mathematics --slug ch02-... --title "..." --order 2`

### Study rules

- Prefer reading `content.md` (and PDF / extracted text) in the chapter folder.
- Prefer chapter `quiz.json` / `quiz.html`; use subject `quizzes/` for overall tests.
- Explain clearly with short examples; check understanding with questions.
- Do not destroy original vault sources; update `_progress.json` / quiz outputs when asked.
- On Android prefer `python3` vault tools (`tools/websearch.py`, `tools/webfetch.py`, curriculum tools).
)STUDY";
}

std::string normalize_answer(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  std::string out;
  out.reserve(s.size());
  bool prev_space = false;
  for (char c : s) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isspace(uc)) {
      if (!prev_space) out.push_back(' ');
      prev_space = true;
    } else {
      out.push_back(static_cast<char>(std::tolower(uc)));
      prev_space = false;
    }
  }
  return out;
}

PrepareResult prepare_chapter_with_llm(
    const std::string& chapter_id, const std::string& provider_id,
    const std::string& model_id, const std::vector<ProviderInfo>& providers) {
  PrepareResult result;
  auto chapter = get_chapter(chapter_id);
  if (!chapter) {
    result.error = "chapter not found";
    return result;
  }
  const fs::path note_path = chapter->path;
  fs::path out_dir = note_path;
  std::string content;
  if (fs::is_regular_file(note_path)) {
    out_dir = note_path.parent_path();
    content = read_file(note_path);
    // Prefer extracted PDF text when present.
    if (note_path.extension() == ".pdf") {
      const fs::path extracted =
          out_dir / (note_path.stem().string() + ".extracted.md");
      const std::string extracted_text = read_file(extracted);
      if (!extracted_text.empty()) content = extracted_text;
    }
  } else if (fs::is_directory(note_path)) {
    content = read_file(note_path / "content.md");
    if (content.empty()) {
      // Fall back to first markdown in the folder.
      std::error_code ec;
      for (auto it = fs::directory_iterator(note_path, ec);
           !ec && it != fs::directory_iterator(); ++it) {
        const auto ext = it->path().extension().string();
        if (ext == ".md" || ext == ".txt") {
          content = read_file(it->path());
          if (!content.empty()) break;
        }
      }
    }
  }
  if (content.empty()) {
    result.error = "note content missing or empty";
    return result;
  }

  std::string err;
  auto client = make_client(provider_id, providers, err);
  if (!client) {
    result.error = err;
    return result;
  }

  const std::string system = study_identity() + R"(

You are preparing one chapter for study. Output TWO sections exactly:

## SUMMARY
(markdown teaching summary, concise)

## QUIZ_JSON
(a raw JSON array only — no fences — following the quiz schema)
)";

  std::ostringstream user;
  user << "Chapter title: " << chapter->title << "\n\n";
  user << "Source material:\n\n" << content.substr(0, 48000);

  GenerateOptions opts;
  opts.model = model_id;
  opts.system = system;
  opts.messages = {Message::user(user.str())};

  GenerateResult res = client->generate_text(opts);
  if (!res.is_success() || (res.error && !res.error->empty())) {
    result.error = res.error && !res.error->empty() ? *res.error
                                                    : res.error_message();
    return result;
  }

  const std::string& text = res.text;
  std::string summary;
  const auto sum_pos = text.find("## SUMMARY");
  const auto quiz_pos = text.find("## QUIZ_JSON");
  if (sum_pos != std::string::npos && quiz_pos != std::string::npos &&
      quiz_pos > sum_pos) {
    summary = text.substr(sum_pos + 10, quiz_pos - (sum_pos + 10));
    while (!summary.empty() &&
           (summary.front() == '\n' || summary.front() == '\r' ||
            summary.front() == ' '))
      summary.erase(summary.begin());
  } else {
    summary = text;
  }

  nlohmann::json quiz = extract_json_array(text);
  if (!quiz.is_array() || quiz.empty()) {
    // Fallback: synthesize a few short questions from headings.
    quiz = nlohmann::json::array();
    quiz.push_back({{"type", "short"},
                    {"topic", "General"},
                    {"prompt", "Summarize the main idea of this chapter in one sentence."},
                    {"answer", ""},
                    {"difficulty", 1}});
    quiz.push_back({{"type", "long"},
                    {"topic", "General"},
                    {"prompt", "Explain two key concepts from this chapter and how they relate."},
                    {"answer", ""},
                    {"difficulty", 2}});
  }

  // Sidecar outputs live beside the note (existing vault structure).
  const std::string stem = fs::is_regular_file(note_path)
                               ? note_path.stem().string()
                               : chapter->slug;
  const fs::path summary_path = out_dir / (stem + ".summary.md");
  const fs::path quiz_json_path = out_dir / (stem + ".quiz.json");
  const fs::path quiz_html_path = out_dir / (stem + ".quiz.html");
  // Also write generic names for older callers.
  write_file(out_dir / "summary.md", summary);
  write_file(out_dir / "quiz.json", quiz.dump(2));
  write_file(summary_path, summary);
  write_file(quiz_json_path, quiz.dump(2));
  replace_chapter_questions(chapter_id, quiz);

  // Prefer vault-local HTML renderer tool when present.
  fs::path vault = out_dir;
  while (!vault.empty() && vault.filename() != "notes" &&
         vault.has_parent_path() && vault != vault.root_path()) {
    if (fs::exists(vault / "tools" / "render_quiz_html.py")) break;
    vault = vault.parent_path();
  }
  const fs::path renderer = vault / "tools" / "render_quiz_html.py";
  if (fs::exists(renderer)) {
    const std::string cmd = "python3 \"" + renderer.string() + "\" \"" +
                            quiz_json_path.string() + "\" \"" +
                            quiz_html_path.string() + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) pclose(pipe);
  }

  result.ok = true;
  result.summary_md = summary;
  result.quiz_json = std::move(quiz);
  return result;
}

GradeLongResult grade_long_with_llm(
    const Question& question, const std::string& student_answer,
    const std::string& provider_id, const std::string& model_id,
    const std::vector<ProviderInfo>& providers) {
  GradeLongResult out;
  std::string err;
  auto client = make_client(provider_id, providers, err);
  if (!client) {
    out.error = err;
    return out;
  }

  GenerateOptions opts;
  opts.model = model_id;
  opts.system =
      "You grade student long-form answers. Reply with ONLY a JSON object:\n"
      "{\"score\":0.0-1.0,\"feedback\":\"short constructive feedback\"}\n"
      "Be fair; partial credit is fine.";
  std::ostringstream user;
  user << "Question:\n" << question.prompt_html << "\n\n";
  if (!question.answer_key.empty()) {
    user << "Rubric / expected points:\n" << question.answer_key << "\n\n";
  }
  user << "Student answer:\n" << student_answer;

  opts.messages = {Message::user(user.str())};
  GenerateResult res = client->generate_text(opts);
  if (!res.is_success() || (res.error && !res.error->empty())) {
    out.error = res.error && !res.error->empty() ? *res.error
                                                 : res.error_message();
    return out;
  }
  auto obj = extract_json_object(res.text);
  if (!obj.is_object()) {
    out.score = 0.5;
    out.feedback = "Could not parse grade; awarded partial credit.";
    out.ok = true;
    return out;
  }
  out.score = std::clamp(obj.value("score", 0.0), 0.0, 1.0);
  out.feedback = obj.value("feedback", "");
  out.ok = true;
  return out;
}

AttemptResult grade_answer(const Question& question,
                           const std::string& student_answer,
                           const std::string& provider_id,
                           const std::string& model_id,
                           const std::vector<ProviderInfo>& providers) {
  if (question.type == QuestionType::kLong) {
    auto graded = grade_long_with_llm(question, student_answer, provider_id,
                                      model_id, providers);
    if (!graded.ok) {
      return record_attempt(question.id, student_answer, false, 0.0,
                            graded.error.empty() ? "grading failed"
                                                 : graded.error);
    }
    const bool correct = graded.score >= 0.7;
    return record_attempt(question.id, student_answer, correct, graded.score,
                          graded.feedback);
  }

  const std::string expected = normalize_answer(question.answer_key);
  const std::string got = normalize_answer(student_answer);
  bool correct = false;
  double score = 0.0;
  std::string feedback;

  if (question.type == QuestionType::kMcq) {
    correct = !expected.empty() && got == expected;
    if (!correct && !question.choices_json.empty()) {
      try {
        auto choices = nlohmann::json::parse(question.choices_json);
        if (choices.is_array()) {
          for (const auto& c : choices) {
            const std::string choice = normalize_answer(c.get<std::string>());
            if (got == choice && choice == expected) {
              correct = true;
              break;
            }
          }
        }
      } catch (...) {
      }
    }
    score = correct ? 1.0 : 0.0;
    feedback = correct ? "Correct." : ("Expected: " + question.answer_key);
  } else {
    // short
    if (expected.empty()) {
      score = got.empty() ? 0.0 : 0.6;
      correct = score >= 0.6;
      feedback = correct ? "Answer recorded." : "Empty answer.";
    } else {
      correct = (got == expected) ||
                (got.find(expected) != std::string::npos) ||
                (expected.find(got) != std::string::npos && got.size() >= 3);
      score = correct ? 1.0 : 0.0;
      feedback = correct ? "Correct." : ("Expected something like: " + question.answer_key);
    }
  }

  return record_attempt(question.id, student_answer, correct, score, feedback);
}

}  // namespace study
}  // namespace qcode
