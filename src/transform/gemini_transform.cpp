#include "ai/gemini_transform.h"
#include "ai/utils/random.h"

#include <chrono>
#include <unordered_map>

namespace ai {
namespace gemini {
namespace {

nlohmann::json convert_openai_to_gemini_impl(const nlohmann::json& openai_req) {
  nlohmann::json gemini_req = nlohmann::json::object();
  nlohmann::json contents = nlohmann::json::array();
  std::string system_instruction;
  std::unordered_map<std::string, std::string> tool_names;

  if (openai_req.contains("messages") && openai_req["messages"].is_array()) {
    for (const auto& msg : openai_req["messages"]) {
      const auto role = msg.value("role", "");
      const auto text_content =
          msg.contains("content") && msg["content"].is_string()
              ? msg["content"].get<std::string>()
              : std::string{};
      if (role == "system") {
        if (!system_instruction.empty()) system_instruction += "\n\n";
        system_instruction += text_content;
        continue;
      }

      nlohmann::json parts = nlohmann::json::array();
      if (role != "tool" && !text_content.empty()) {
        parts.push_back({{"text", text_content}});
      }
      if (msg.contains("reasoning") && !msg["reasoning"].is_null()) {
        nlohmann::json part{{"text", msg["reasoning"]}, {"thought", true}};
        if (msg.contains("reasoning_signature")) {
          part["thoughtSignature"] = msg["reasoning_signature"];
        }
        parts.push_back(std::move(part));
      }
      if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
        for (const auto& call : msg["tool_calls"]) {
          const auto& function = call["function"];
          auto args = function.value("arguments", nlohmann::json{});
          if (args.is_string()) {
            try {
              args = nlohmann::json::parse(args.get<std::string>());
            } catch (...) {
              args = nlohmann::json::object();
            }
          }
          const auto call_id = call.value("id", "");
          const auto name = function.value("name", "");
          tool_names[call_id] = name;
          nlohmann::json function_part{
              {"functionCall",
               {{"id", call_id}, {"name", name}, {"args", std::move(args)}}}};
          if (call.contains("thought_signature")) {
            function_part["thoughtSignature"] = call["thought_signature"];
          }
          parts.push_back(std::move(function_part));
        }
      }
      if (role == "tool") {
        auto response = msg.value("content", nlohmann::json{});
        if (response.is_string()) {
          try {
            response = nlohmann::json::parse(response.get<std::string>());
          } catch (...) {
            response = {{"result", response}};
          }
        }
        const auto call_id = msg.value("tool_call_id", "");
        parts.push_back(
            {{"functionResponse",
              {{"id", call_id},
               {"name", tool_names.contains(call_id) ? tool_names[call_id] : ""},
               {"response", std::move(response)}}}});
      }
      if (!parts.empty()) {
        contents.push_back(
            {{"role", role == "assistant" ? "model" : "user"},
             {"parts", std::move(parts)}});
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
  if (openai_req.contains("top_p")) gen_config["topP"] = openai_req["top_p"];
  if (openai_req.contains("seed")) gen_config["seed"] = openai_req["seed"];
  if (openai_req.contains("reasoning_effort")) {
    gen_config["thinkingConfig"] = {
        {"thinkingLevel", openai_req["reasoning_effort"]}};
  }
  gemini_req["generationConfig"] = gen_config;

  if (openai_req.contains("tools") && openai_req["tools"].is_array()) {
    nlohmann::json declarations = nlohmann::json::array();
    for (const auto& tool : openai_req["tools"]) {
      if (!tool.contains("function")) continue;
      const auto& function = tool["function"];
      declarations.push_back(
          {{"name", function.value("name", "")},
           {"description", function.value("description", "")},
           {"parameters", function.value("parameters",
                                          nlohmann::json::object())}});
    }
    if (!declarations.empty()) {
      gemini_req["tools"] = {{{"functionDeclarations",
                               std::move(declarations)}}};
    }
  }
  if (openai_req.contains("tool_choice")) {
    auto mode = "AUTO";
    nlohmann::json allowed = nlohmann::json::array();
    if (openai_req["tool_choice"].is_string()) {
      const auto choice = openai_req["tool_choice"].get<std::string>();
      if (choice == "none") mode = "NONE";
      if (choice == "required") mode = "ANY";
    } else if (openai_req["tool_choice"].contains("function")) {
      mode = "ANY";
      allowed.push_back(
          openai_req["tool_choice"]["function"].value("name", ""));
    }
    gemini_req["toolConfig"]["functionCallingConfig"]["mode"] = mode;
    if (!allowed.empty()) {
      gemini_req["toolConfig"]["functionCallingConfig"]
                ["allowedFunctionNames"] = std::move(allowed);
    }
  }
  return gemini_req;
}

nlohmann::json wrap_antigravity_envelope_impl(const nlohmann::json& gemini_req,
                                              const std::string& model,
                                              const std::string& project_id) {
  nlohmann::json env = nlohmann::json::object();
  env["project"] = project_id.empty() ? "rising-fact-p41fc" : project_id;

  std::string req_uuid = utils::new_uuid();
  unsigned long long timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  env["requestId"] =
      "agent/" + req_uuid + "/" + std::to_string(timestamp) + "/" + req_uuid + "/0";
  env["request"] = gemini_req;

  std::string mapped_model = model;
  constexpr std::string_view antigravity_prefix = "antigravity-";
  if (mapped_model.starts_with(antigravity_prefix)) {
    mapped_model.erase(0, antigravity_prefix.size());
  }
  if (mapped_model == "gemini-3-flash") {
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

      std::string text_content;
      std::string reasoning_content;
      nlohmann::json reasoning_details = nlohmann::json::array();
      nlohmann::json tool_calls = nlohmann::json::array();
      if (cand.contains("content") && cand["content"].contains("parts")) {
        auto& parts = cand["content"]["parts"];
        for (const auto& part : parts) {
          if (part.contains("text")) {
            const auto text = part["text"].get<std::string>();
            if (part.value("thought", false)) {
              reasoning_content += text;
              nlohmann::json detail{{"type", "text"}, {"text", text}};
              if (part.contains("thoughtSignature")) {
                detail["signature"] = part["thoughtSignature"];
              }
              reasoning_details.push_back(std::move(detail));
            } else {
              text_content += text;
            }
          }
          if (part.contains("functionCall")) {
            const auto& function = part["functionCall"];
            nlohmann::json tool_call{
                {"id", function.value("id", utils::new_uuid())},
                {"type", "function"},
                {"function",
                 {{"name", function.value("name", "")},
                  {"arguments",
                   function.value("args", nlohmann::json::object()).dump()}}}};
            if (part.contains("thoughtSignature")) {
              tool_call["thought_signature"] = part["thoughtSignature"];
            }
            tool_calls.push_back(std::move(tool_call));
          }
        }
      }
      message["content"] = text_content;
      if (!reasoning_content.empty()) message["reasoning"] = reasoning_content;
      if (!reasoning_details.empty()) {
        message["reasoning_details"] = std::move(reasoning_details);
      }
      if (!tool_calls.empty()) message["tool_calls"] = std::move(tool_calls);

      choice["message"] = message;

      std::string finish_reason_str = cand.value("finishReason", "STOP");
      if (finish_reason_str == "STOP")
        choice["finish_reason"] =
            message.contains("tool_calls") ? "tool_calls" : "stop";
      else if (finish_reason_str == "MAX_TOKENS")
        choice["finish_reason"] = "length";
      else
        choice["finish_reason"] = "content_filter";

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

std::string new_uuid() { return utils::new_uuid(); }
std::string random_hex(size_t len) { return utils::random_hex(len); }

nlohmann::json convert_openai_to_gemini(const nlohmann::json& openai_req) {
  return convert_openai_to_gemini_impl(openai_req);
}

nlohmann::json wrap_antigravity_envelope(const nlohmann::json& gemini_req,
                                         const std::string& model,
                                         const std::string& project_id) {
  return wrap_antigravity_envelope_impl(gemini_req, model, project_id);
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
