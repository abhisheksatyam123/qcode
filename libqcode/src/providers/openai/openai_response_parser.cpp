#include "openai_response_parser.h"

#include "../../utils/response_utils.h"
#include <qcode/core/logger.h>

namespace qcode {
namespace openai {
namespace {

nlohmann::json normalize_responses_api(const nlohmann::json& response) {
  if (!response.contains("output") || !response["output"].is_array()) {
    return response;
  }
  nlohmann::json message{{"role", "assistant"}, {"content", ""}};
  nlohmann::json tool_calls = nlohmann::json::array();
  std::string text;
  for (const auto& item : response["output"]) {
    if (item.value("type", "") == "message" && item.contains("content")) {
      for (const auto& part : item["content"]) {
        if (part.value("type", "") == "output_text") {
          text += part.value("text", "");
        }
      }
    } else if (item.value("type", "") == "function_call") {
      tool_calls.push_back(
          {{"id", item.value("call_id", item.value("id", ""))},
           {"type", "function"},
           {"function",
            {{"name", item.value("name", "")},
             {"arguments", item.value("arguments", "{}")}}}});
    }
  }
  message["content"] = text;
  if (!tool_calls.empty()) message["tool_calls"] = std::move(tool_calls);
  const auto finish_reason =
      !message.contains("tool_calls")
          ? (response.value("status", "") == "incomplete" ? "length" : "stop")
          : "tool_calls";
  nlohmann::json normalized{
      {"id", response.value("id", "")},
      {"model", response.value("model", "")},
      {"created", response.value("created_at", 0)},
      {"choices",
       {{{"index", 0},
         {"message", std::move(message)},
         {"finish_reason", finish_reason}}}}};
  if (response.contains("usage")) {
    const auto& usage = response["usage"];
    normalized["usage"] = {
        {"prompt_tokens", usage.value("input_tokens", 0)},
        {"completion_tokens", usage.value("output_tokens", 0)},
        {"total_tokens", usage.value("total_tokens", 0)}};
  }
  return normalized;
}

}  // namespace

GenerateResult OpenAIResponseParser::parse_success_completion_response(
    const nlohmann::json& response) {
  LOG_DEBUG("Parsing OpenAI chat completion response");

  // Gemini/Antigravity response normalization: unwrap the envelope and
  // translate a generateContent payload into OpenAI-shaped JSON.
  nlohmann::json normalized_response = normalize_responses_api(response);

  GenerateResult result;

  // Extract basic fields
  result.id = normalized_response.value("id", "");
  result.model = normalized_response.value("model", "");
  result.created = normalized_response.value("created", 0);

  // Handle system_fingerprint which can be null or string
  if (auto it = normalized_response.find("system_fingerprint");
      it != normalized_response.end() && !it->is_null()) {
    result.system_fingerprint = it->get<std::string>();
  }

  LOG_DEBUG("Response ID: {}, Model: {}",
                        result.id.value_or("none"),
                        result.model.value_or("unknown"));

  // Extract choices
  if (normalized_response.contains("choices") && !normalized_response["choices"].empty()) {
    auto& choice = normalized_response["choices"][0];

    // Extract message content
    if (choice.contains("message")) {
      auto& message = choice["message"];
      // Handle null content (happens when model makes tool calls)
      if (message.contains("content") && !message["content"].is_null()) {
        result.text = message["content"].get<std::string>();
      } else {
        result.text = "";
      }
      LOG_DEBUG("Extracted message content - length: {}",
                            result.text.length());

      // Parse tool calls if present
      if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
        LOG_DEBUG("Found {} tool calls in response",
                              message["tool_calls"].size());

        for (const auto& tool_call_json : message["tool_calls"]) {
          if (tool_call_json.contains("id") &&
              !tool_call_json["id"].is_null() &&
              tool_call_json.contains("function") &&
              tool_call_json["function"].contains("name") &&
              !tool_call_json["function"]["name"].is_null() &&
              tool_call_json["function"].contains("arguments")) {
            std::string call_id = tool_call_json["id"].get<std::string>();
            std::string function_name =
                tool_call_json["function"]["name"].get<std::string>();

            // Handle arguments - they might be null, string, or object
            std::string arguments_str;
            if (tool_call_json["function"]["arguments"].is_null()) {
              arguments_str = "{}";
            } else if (tool_call_json["function"]["arguments"].is_string()) {
              arguments_str =
                  tool_call_json["function"]["arguments"].get<std::string>();
            } else {
              arguments_str = tool_call_json["function"]["arguments"].dump();
            }

            try {
              JsonValue arguments;
              if (arguments_str.empty() || arguments_str == "null") {
                arguments = JsonValue::object();
              } else {
                arguments = JsonValue::parse(arguments_str);
              }
              ToolCall tool_call(
                  call_id, function_name, arguments,
                  tool_call_json.value("thought_signature", ""));
              result.tool_calls.push_back(tool_call);

              LOG_DEBUG("Parsed tool call: {} with args: {}",
                                    function_name, arguments_str);
            } catch (const std::exception& e) {
              LOG_ERROR("Failed to parse tool call arguments: {}",
                                    e.what());
            }
          }
        }
      }

      std::string reasoning;
      std::string reasoning_signature;
      if (message.contains("reasoning") && message["reasoning"].is_string()) {
        reasoning = message["reasoning"].get<std::string>();
      }
      if (message.contains("reasoning_details") &&
          message["reasoning_details"].is_array()) {
        for (const auto& detail : message["reasoning_details"]) {
          if (reasoning.empty()) reasoning += detail.value("text", "");
          if (reasoning_signature.empty()) {
            reasoning_signature = detail.value("signature", "");
          }
        }
      }

      if (!result.tool_calls.empty()) {
        std::vector<ToolCallContentPart> calls;
        calls.reserve(result.tool_calls.size());
        for (const auto& call : result.tool_calls) {
          calls.emplace_back(call.id, call.tool_name, call.arguments,
                             call.thought_signature);
        }
        result.response_messages.push_back(
            Message::assistant_with_tools(result.text, calls));
      } else if (!reasoning.empty()) {
        result.response_messages.push_back(Message::assistant_with_reasoning(
            result.text, reasoning, reasoning_signature));
      } else if (!result.text.empty()) {
        result.response_messages.push_back(Message::assistant(result.text));
      }
    }

    // Extract finish reason
    if (choice.contains("finish_reason") &&
        !choice["finish_reason"].is_null()) {
      auto finish_reason_str = choice["finish_reason"].get<std::string>();
      result.finish_reason = parse_finish_reason(finish_reason_str);
      LOG_DEBUG("Finish reason: {}", finish_reason_str);
    } else {
      result.finish_reason =
          kFinishReasonStop;  // Default to stop if null or missing
      LOG_DEBUG(
          "Finish reason was null or missing, defaulting to stop");
    }
  } else {
    std::string err = "Response has no valid choices";
    if (normalized_response.contains("error")) {
      auto& err_obj = normalized_response["error"];
      if (err_obj.is_string()) {
        err += ": " + err_obj.get<std::string>();
      } else if (err_obj.is_object() && err_obj.contains("message")) {
        err += ": " + err_obj["message"].get<std::string>();
      }
    }
    LOG_ERROR("Response missing choices: {}", normalized_response.dump());
    result.error = err;
    result.finish_reason = kFinishReasonError;
  }

  // Extract usage
  if (normalized_response.contains("usage")) {
    auto& usage = normalized_response["usage"];
    result.usage.prompt_tokens = usage.value("prompt_tokens", 0);
    result.usage.completion_tokens = usage.value("completion_tokens", 0);
    result.usage.total_tokens = usage.value("total_tokens", 0);
    if (usage.contains("prompt_tokens_details") && usage["prompt_tokens_details"].is_object()) {
      result.usage.cached_prompt_tokens = usage["prompt_tokens_details"].value("cached_tokens", 0);
    }
    LOG_DEBUG("Token usage - prompt: {}, cached: {}, completion: {}, total: {}",
                          result.usage.prompt_tokens,
                          result.usage.cached_prompt_tokens,
                          result.usage.completion_tokens,
                          result.usage.total_tokens);
  }

  // Store full metadata
  result.provider_metadata = normalized_response.dump();

  return result;
}

GenerateResult OpenAIResponseParser::parse_error_completion_response(
    int status_code,
    const std::string& body) {
  return utils::parse_standard_error_response("OpenAI", status_code, body);
}

EmbeddingResult OpenAIResponseParser::parse_success_embedding_response(
    const nlohmann::json& response) {
  LOG_DEBUG("Parsing OpenAI embeddings response");

  EmbeddingResult result;

  // Extract basic fields
  result.model = response.value("model", "");

  // Extract choices
  if (response.contains("data") && !response["data"].empty()) {
    result.data = std::move(response["data"]);
  }

  // Extract usage
  if (response.contains("usage")) {
    auto& usage = response["usage"];
    result.usage.prompt_tokens = usage.value("prompt_tokens", 0);
    result.usage.completion_tokens = usage.value("completion_tokens", 0);
    result.usage.total_tokens = usage.value("total_tokens", 0);
    LOG_DEBUG("Token usage - prompt: {}, completion: {}, total: {}",
                          result.usage.prompt_tokens,
                          result.usage.completion_tokens,
                          result.usage.total_tokens);
  }

  // Store full metadata
  result.provider_metadata = response.dump();

  return result;
}

EmbeddingResult OpenAIResponseParser::parse_error_embedding_response(
    int status_code,
    const std::string& body) {
  auto generate_result =
      utils::parse_standard_error_response("OpenAI", status_code, body);
  return EmbeddingResult(generate_result.error);
}

FinishReason OpenAIResponseParser::parse_finish_reason(
    const std::string& reason) {
  if (reason == "stop") {
    return kFinishReasonStop;
  }
  if (reason == "length") {
    return kFinishReasonLength;
  }
  if (reason == "content_filter") {
    return kFinishReasonContentFilter;
  }
  if (reason == "tool_calls") {
    return kFinishReasonToolCalls;
  }
  return kFinishReasonError;
}

}  // namespace openai
}  // namespace qcode