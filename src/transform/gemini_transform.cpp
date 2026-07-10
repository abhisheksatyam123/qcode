#include "ai/gemini_transform.h"

#include <chrono>
#include <random>
#include <unordered_set>

namespace ai {
namespace gemini {
namespace {

std::string new_uuid_impl() {
  static const char hex_chars[] = "0123456789abcdef";
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 15);
  static std::uniform_int_distribution<> dis2(8, 11);

  std::string uuid = "";
  for (int i = 0; i < 8; ++i) uuid += hex_chars[dis(gen)];
  uuid += "-";
  for (int i = 0; i < 4; ++i) uuid += hex_chars[dis(gen)];
  uuid += "-4";
  for (int i = 0; i < 3; ++i) uuid += hex_chars[dis(gen)];
  uuid += "-";
  uuid += hex_chars[dis2(gen)];
  for (int i = 0; i < 3; ++i) uuid += hex_chars[dis(gen)];
  uuid += "-";
  for (int i = 0; i < 12; ++i) uuid += hex_chars[dis(gen)];

  return uuid;
}

std::string random_hex_impl(size_t len) {
  static const char hex_chars[] = "0123456789abcdef";
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 15);
  std::string res = "";
  for (size_t i = 0; i < len; ++i) {
    res += hex_chars[dis(gen)];
  }
  return res;
}

nlohmann::json convert_openai_to_gemini_impl(const nlohmann::json& openai_req) {
  nlohmann::json gemini_req = nlohmann::json::object();

  nlohmann::json contents = nlohmann::json::array();
  std::string system_instruction = "";

  if (openai_req.contains("messages") && openai_req["messages"].is_array()) {
    for (const auto& msg : openai_req["messages"]) {
      std::string role = msg.value("role", "");
      std::string text_content = msg.value("content", "");

      if (role == "system") {
        system_instruction = text_content;
      } else {
        nlohmann::json content_obj = nlohmann::json::object();
        content_obj["role"] = (role == "assistant") ? "model" : "user";

        nlohmann::json parts = nlohmann::json::array();
        parts.push_back({{"text", text_content}});
        content_obj["parts"] = parts;

        contents.push_back(content_obj);
      }
    }
  }

  gemini_req["contents"] = contents;

  if (!system_instruction.empty()) {
    gemini_req["systemInstruction"] = {
        {"parts", {{{"text", system_instruction}}}}};
  }

  nlohmann::json gen_config = nlohmann::json::object();
  if (openai_req.contains("temperature")) {
    gen_config["temperature"] = openai_req["temperature"];
  }
  if (openai_req.contains("max_completion_tokens")) {
    gen_config["maxOutputTokens"] = openai_req["max_completion_tokens"];
  }
  gemini_req["generationConfig"] = gen_config;

  return gemini_req;
}

nlohmann::json wrap_antigravity_envelope_impl(const nlohmann::json& gemini_req,
                                              const std::string& model) {
  nlohmann::json env = nlohmann::json::object();
  env["project"] = "tuned-keel-d72qv";

  std::string req_uuid = new_uuid_impl();
  unsigned long long timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  env["requestId"] =
      "agent/" + req_uuid + "/" + std::to_string(timestamp) + "/" + req_uuid + "/0";
  env["request"] = gemini_req;

  std::string mapped_model = model;
  if (model == "gemini-3-flash") {
    mapped_model = "gemini-3-flash-agent";
  }
  env["model"] = mapped_model;
  env["userAgent"] = "antigravity";
  env["requestType"] = "agent";

  return env;
}

nlohmann::json normalize_gemini_response_impl(const nlohmann::json& response) {
  nlohmann::json normalized_response = response;

  nlohmann::json gemini_data = response;
  if (response.contains("response") && response["response"].is_object()) {
    gemini_data = response["response"];
  }

  if (gemini_data.contains("candidates")) {
    normalized_response = nlohmann::json::object();
    normalized_response["id"] =
        gemini_data.value("responseId", response.value("requestId", ""));
    normalized_response["model"] =
        gemini_data.value("modelVersion", response.value("model", ""));
    normalized_response["created"] = 0;

    nlohmann::json choices = nlohmann::json::array();
    auto& candidates = gemini_data["candidates"];
    if (!candidates.empty()) {
      auto& cand = candidates[0];
      nlohmann::json choice = nlohmann::json::object();
      choice["index"] = 0;

      nlohmann::json message = nlohmann::json::object();
      message["role"] = "assistant";

      std::string text_content = "";
      if (cand.contains("content") && cand["content"].contains("parts")) {
        auto& parts = cand["content"]["parts"];
        if (!parts.empty() && parts[0].contains("text")) {
          text_content = parts[0]["text"].get<std::string>();
        }
      }
      message["content"] = text_content;

      choice["message"] = message;

      std::string finish_reason_str = cand.value("finishReason", "stop");
      if (finish_reason_str == "STOP")
        choice["finish_reason"] = "stop";
      else if (finish_reason_str == "MAX_TOKENS")
        choice["finish_reason"] = "length";
      else
        choice["finish_reason"] = "stop";

      choices.push_back(choice);
    }
    normalized_response["choices"] = choices;

    if (gemini_data.contains("usageMetadata")) {
      auto& usage_meta = gemini_data["usageMetadata"];
      nlohmann::json usage = nlohmann::json::object();
      usage["prompt_tokens"] = usage_meta.value("promptTokenCount", 0);
      usage["completion_tokens"] = usage_meta.value("candidatesTokenCount", 0);
      usage["total_tokens"] = usage_meta.value("totalTokenCount", 0);
      normalized_response["usage"] = usage;
    }
  }

  return normalized_response;
}

}  // namespace

std::string new_uuid() { return new_uuid_impl(); }
std::string random_hex(size_t len) { return random_hex_impl(len); }

nlohmann::json convert_openai_to_gemini(const nlohmann::json& openai_req) {
  return convert_openai_to_gemini_impl(openai_req);
}

nlohmann::json wrap_antigravity_envelope(const nlohmann::json& gemini_req,
                                         const std::string& model) {
  return wrap_antigravity_envelope_impl(gemini_req, model);
}

nlohmann::json unwrap_envelope(const nlohmann::json& json) {
  if (json.contains("response") && json["response"].is_object()) {
    return json["response"];
  }
  return json;
}

nlohmann::json normalize_gemini_response(const nlohmann::json& response) {
  return normalize_gemini_response_impl(response);
}

}  // namespace gemini
}  // namespace ai
