#include "openai_responses.h"

#include <optional>
#include <string>

namespace qcode {
namespace openai {

nlohmann::json to_responses_request(const nlohmann::json& request) {
  const auto json_string_or_dump = [](const nlohmann::json& value,
                                      const std::string& fallback) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_null() || value.is_discarded()) return fallback;
    return value.dump();
  };

  // OpenCode openai-responses.ts: user = [{input_text}], assistant =
  // [{output_text}]. A bare string or null is rejected as
  // `input[n].content` "did not match any supported type".
  const auto responses_content =
      [](const std::string& role,
         const nlohmann::json& content) -> std::optional<nlohmann::json> {
    const char* part_type =
        role == "assistant" ? "output_text" : "input_text";
    if (content.is_null()) return std::nullopt;
    if (content.is_string()) {
      const auto text = content.get<std::string>();
      if (text.empty()) return std::nullopt;
      // OpenCode keeps system as a plain string (joinText).
      if (role == "system") return std::optional<nlohmann::json>{content};
      return nlohmann::json::array({{{"type", part_type}, {"text", text}}});
    }
    if (content.is_array()) {
      nlohmann::json parts = nlohmann::json::array();
      for (const auto& part : content) {
        if (part.is_string()) {
          const auto text = part.get<std::string>();
          if (!text.empty()) {
            parts.push_back({{"type", part_type}, {"text", text}});
          }
          continue;
        }
        if (!part.is_object()) continue;
        std::string text;
        if (part.contains("text") && part["text"].is_string()) {
          text = part["text"].get<std::string>();
        } else if (part.contains("content") && part["content"].is_string()) {
          text = part["content"].get<std::string>();
        }
        if (text.empty()) continue;
        const auto type = part.value("type", "");
        if (type == "input_text" || type == "output_text") {
          parts.push_back({{"type", type}, {"text", text}});
        } else {
          parts.push_back({{"type", part_type}, {"text", text}});
        }
      }
      if (parts.empty()) return std::nullopt;
      return parts;
    }
    return nlohmann::json::array(
        {{{"type", part_type}, {"text", content.dump()}}});
  };

  nlohmann::json responses{{"model", request["model"]},
                           {"input", nlohmann::json::array()}};
  for (const auto& message : request["messages"]) {
    const auto role = message.value("role", "");
    if (role == "tool") {
      responses["input"].push_back(
          {{"type", "function_call_output"},
           {"call_id", message.value("tool_call_id", "")},
           {"output", message.contains("content")
                          ? json_string_or_dump(message["content"], "")
                          : ""}});
      continue;
    }
    if (message.contains("content")) {
      if (auto parts = responses_content(role, message["content"])) {
        responses["input"].push_back(
            {{"role", role}, {"content", std::move(*parts)}});
      }
    }
    if (message.contains("tool_calls")) {
      for (const auto& call : message["tool_calls"]) {
        const auto& function = call.contains("function")
                                   ? call["function"]
                                   : nlohmann::json::object();
        const auto arguments =
            function.contains("arguments")
                ? json_string_or_dump(function["arguments"], "{}")
                : "{}";
        responses["input"].push_back({{"type", "function_call"},
                                      {"call_id", call.value("id", "")},
                                      {"name", function.value("name", "")},
                                      {"arguments", arguments}});
      }
    }
  }
  if (request.contains("max_completion_tokens")) {
    responses["max_output_tokens"] = request["max_completion_tokens"];
  }
  if (request.contains("temperature")) {
    responses["temperature"] = request["temperature"];
  }
  if (request.contains("top_p")) responses["top_p"] = request["top_p"];
  if (request.contains("reasoning_effort")) {
    responses["reasoning"] = {{"effort", request["reasoning_effort"]}};
  }
  if (request.contains("tools")) {
    responses["tools"] = nlohmann::json::array();
    for (const auto& tool : request["tools"]) {
      const auto& function = tool["function"];
      responses["tools"].push_back(
          {{"type", "function"},
           {"name", function.value("name", "")},
           {"description", function.value("description", "")},
           {"parameters",
            function.value("parameters", nlohmann::json::object())}});
    }
  }
  return responses;
}

}  // namespace openai
}  // namespace qcode
