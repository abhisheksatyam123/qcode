#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <qcode/core/state.h>
#include <qcode/session/study_store.h>

namespace qcode {
namespace study {

struct PrepareResult {
  bool ok = false;
  std::string error;
  std::string summary_md;
  nlohmann::json quiz_json = nlohmann::json::array();
};

struct GradeLongResult {
  double score = 0.0;
  std::string feedback;
  bool ok = false;
  std::string error;
};

// Teaching-assistant identity for Study mode chats / prepare calls.
std::string study_identity();

// LLM: write summary.md + quiz.json for a chapter (tools disabled).
PrepareResult prepare_chapter_with_llm(
    const std::string& chapter_id, const std::string& provider_id,
    const std::string& model_id, const std::vector<ProviderInfo>& providers);

// Deterministic grade for mcq/short; LLM rubric for long.
AttemptResult grade_answer(const Question& question,
                           const std::string& student_answer,
                           const std::string& provider_id,
                           const std::string& model_id,
                           const std::vector<ProviderInfo>& providers);

GradeLongResult grade_long_with_llm(
    const Question& question, const std::string& student_answer,
    const std::string& provider_id, const std::string& model_id,
    const std::vector<ProviderInfo>& providers);

std::string normalize_answer(std::string s);

}  // namespace study
}  // namespace qcode
