#include "anthropic_response_parser.h"

#include "../../utils/response_utils.h"
#include <qcode/logger/logger.h>

namespace qcode {
namespace anthropic {

GenerateResult AnthropicResponseParser::parse_success_completion_response(
    const nlohmann::json& response) {
  LOG_DEBUG("Parsing Anthropic messages response");

  GenerateResult result;

  // Extract basic fields
  result.id = response.value("id", "");
  result.model = response.value("model", "");

  LOG_DEBUG("Response ID: {}, Model: {}",
                        result.id.value_or("none"),
                        result.model.value_or("unknown"));

  // Extract content from the response
  if (response.contains("content") && response["content"].is_array()) {
    std::string full_text;

    for (const auto& content_block : response["content"]) {
      if (content_block.contains("type")) {
        std::string type = content_block["type"];

        if (type == "text" && content_block.contains("text")) {
          full_text += content_block["text"].get<std::string>();
        } else if (type == "tool_use") {
          // Parse Anthropic tool use
          if (content_block.contains("id") && content_block.contains("name") &&
              content_block.contains("input")) {
            std::string call_id = content_block["id"];
            std::string function_name = content_block["name"];
            const JsonValue& arguments = content_block["input"];

            ToolCall tool_call(call_id, function_name, arguments);
            result.tool_calls.push_back(tool_call);

            LOG_DEBUG(
                "Parsed Anthropic tool call: {} with args: {}", function_name,
                arguments.dump());
          }
        }
      }
    }

    result.text = full_text;
    LOG_DEBUG(
        "Extracted message content - length: {}, tool calls: {}",
        result.text.length(), result.tool_calls.size());

    // Add assistant response to messages
    if (!result.text.empty()) {
      result.response_messages.push_back(Message::assistant(result.text));
    }
  } else {
    LOG_DEBUG("Response has no content array");
  }

  // Extract stop reason
  if (response.contains("stop_reason")) {
    std::string stop_reason = response["stop_reason"];
    result.finish_reason = parse_stop_reason(stop_reason);
    LOG_DEBUG("Stop reason: {}", stop_reason);
  }

  // Extract usage
  if (response.contains("usage")) {
    auto& usage = response["usage"];
    result.usage.prompt_tokens = usage.value("input_tokens", 0);
    result.usage.completion_tokens = usage.value("output_tokens", 0);
    result.usage.total_tokens =
        result.usage.prompt_tokens + result.usage.completion_tokens;
    LOG_DEBUG("Token usage - input: {}, output: {}, total: {}",
                          result.usage.prompt_tokens,
                          result.usage.completion_tokens,
                          result.usage.total_tokens);
  }

  // Store full metadata
  result.provider_metadata = response.dump();

  return result;
}

GenerateResult AnthropicResponseParser::parse_error_completion_response(
    int status_code,
    const std::string& body) {
  return utils::parse_standard_error_response("Anthropic", status_code, body);
}

EmbeddingResult AnthropicResponseParser::parse_success_embedding_response(
    const nlohmann::json& response) {
  LOG_DEBUG("Parsing Anthropic embeddings response");

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

EmbeddingResult AnthropicResponseParser::parse_error_embedding_response(
    int status_code,
    const std::string& body) {
  auto generate_result =
      utils::parse_standard_error_response("Anthropic", status_code, body);
  return EmbeddingResult(generate_result.error);
}

FinishReason AnthropicResponseParser::parse_stop_reason(
    const std::string& reason) {
  if (reason == "end_turn") {
    return kFinishReasonStop;
  }
  if (reason == "max_tokens") {
    return kFinishReasonLength;
  }
  if (reason == "stop_sequence") {
    return kFinishReasonStop;
  }
  if (reason == "tool_use") {
    return kFinishReasonToolCalls;
  }
  return kFinishReasonStop;
}

}  // namespace anthropic
}  // namespace qcode