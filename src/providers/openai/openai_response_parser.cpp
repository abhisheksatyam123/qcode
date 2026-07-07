#include "openai_response_parser.h"

#include "../../utils/response_utils.h"
#include "ai/logger.h"

namespace ai {
namespace openai {

GenerateResult OpenAIResponseParser::parse_success_completion_response(
    const nlohmann::json& response) {
  LOG_DEBUG("Parsing OpenAI chat completion response");

  nlohmann::json normalized_response = response;
  
  // Gemini/Antigravity response normalization
  nlohmann::json gemini_data = response;
  if (response.contains("response") && response["response"].is_object()) {
    gemini_data = response["response"];
  }
  
  if (gemini_data.contains("candidates")) {
    normalized_response = nlohmann::json::object();
    normalized_response["id"] = gemini_data.value("responseId", response.value("requestId", ""));
    normalized_response["model"] = gemini_data.value("modelVersion", response.value("model", ""));
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
      if (finish_reason_str == "STOP") choice["finish_reason"] = "stop";
      else if (finish_reason_str == "MAX_TOKENS") choice["finish_reason"] = "length";
      else choice["finish_reason"] = "stop";
      
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
              ToolCall tool_call(call_id, function_name, arguments);
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

      // Add assistant response to messages
      if (!result.text.empty()) {
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
  }

  // Extract usage
  if (normalized_response.contains("usage")) {
    auto& usage = normalized_response["usage"];
    result.usage.prompt_tokens = usage.value("prompt_tokens", 0);
    result.usage.completion_tokens = usage.value("completion_tokens", 0);
    result.usage.total_tokens = usage.value("total_tokens", 0);
    LOG_DEBUG("Token usage - prompt: {}, completion: {}, total: {}",
                          result.usage.prompt_tokens,
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
}  // namespace ai