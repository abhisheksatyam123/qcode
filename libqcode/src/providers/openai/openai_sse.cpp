#include "openai_stream.h"

#include "openai_response_parser.h"
#include <qcode/core/logger.h>
#include "core/response_utils.h"

namespace qcode {
namespace openai {

void OpenAIStreamImpl::parse_sse_line(const std::string& line) {
  if (line.starts_with("data:")) {
    auto data = line.substr(5);
    if (!data.empty() && data.front() == ' ') data.erase(0, 1);

    LOG_DEBUG("Processing SSE line - data length: {}",
                          data.length());

    if (data == "[DONE]") {
      LOG_DEBUG("Received [DONE] signal, stream ending");
      push_finish_event_if_needed();
      mark_complete();
      return;
    }


    try {
      auto json = nlohmann::json::parse(data);

      const auto event_type = json.value("type", "");
      if (event_type == "response.output_text.delta") {
        push_event(StreamEvent(json.value("delta", "")));
        return;
      }
      if (event_type == "response.reasoning_text.delta") {
        push_event(StreamEvent::reasoning(json.value("delta", "")));
        return;
      }
      if (event_type == "response.completed" ||
          event_type == "response.incomplete") {
        Usage usage;
        const auto response = json.value("response", nlohmann::json::object());
        if (response.contains("usage")) {
          const auto& raw_usage = response["usage"];
          usage.prompt_tokens = raw_usage.value("input_tokens", 0);
          usage.completion_tokens = raw_usage.value("output_tokens", 0);
          usage.total_tokens = raw_usage.value("total_tokens", 0);
        }
        finish_event_pushed_ = true;
        push_event(StreamEvent(
            kStreamEventTypeFinish, usage,
            event_type == "response.incomplete" ? kFinishReasonLength
                                                  : kFinishReasonStop));
        return;
      }
      if (event_type == "error") {
        push_event(create_error_event(json.value("message", "Provider error")));
        return;
      }

      // Antigravity wraps every SSE chunk in { response: {...}, traceId,
      // metadata }. Unwrap so the Gemini/OpenAI branches below see the inner
      // payload (candidates / usageMetadata / choices / delta).
      if (protocol_ == StreamProtocol::kGeminiEnvelope) {
        if (json.contains("response") && json["response"].is_object()) {
          json = json["response"];
        }
      }

      // Surface provider errors (e.g. Antigravity quota/rate-limit) instead of
      // silently yielding an empty response.
      if (json.contains("error")) {
        const auto& err = json["error"];
        std::string msg;
        if (err.is_string()) {
          msg = err.get<std::string>();
        } else if (err.contains("message")) {
          msg = err["message"].get<std::string>();
        }
        if (!msg.empty()) {
          LOG_ERROR("Stream error from provider: {}", msg);
          if (msg.find("429") != std::string::npos ||
              msg.find("rate_limit") != std::string::npos ||
              msg.find("Rate limit") != std::string::npos ||
              msg.find("quota") != std::string::npos ||
              msg.find("RESOURCE_EXHAUSTED") != std::string::npos) {
            msg += "\n\n💡 Tip: Rate limit hit. Switch model via `/model laguna-s-2.1-free`, `/model mimo-v2.5-free`, or `/model nemotron-3.5-lightning-free`.";
          }
          push_event(create_error_event(msg));
          return;
        }
      }

      // Google Gemini / Antigravity SSE parsing
      if (json.contains("candidates")) {
        auto& candidates = json["candidates"];
        if (!candidates.empty()) {
          auto& cand = candidates[0];
          if (cand.contains("content") && cand["content"].contains("parts")) {
            auto& parts = cand["content"]["parts"];
            for (const auto& part : parts) {
              if (part.contains("text")) {
                const auto content = part["text"].get<std::string>();
                if (part.value("thought", false)) {
                  std::optional<std::string> signature;
                  if (part.contains("thoughtSignature")) {
                    signature = part["thoughtSignature"].get<std::string>();
                  }
                  push_event(StreamEvent::reasoning(content, signature));
                } else {
                  push_event(StreamEvent(content));
                }
              }
            }
          }
          
          if (cand.contains("finishReason")) {
            std::string reason_str = cand["finishReason"].get<std::string>();
            FinishReason finish_reason = kFinishReasonStop;
            if (reason_str == "STOP") finish_reason = kFinishReasonStop;
            else if (reason_str == "MAX_TOKENS") finish_reason = kFinishReasonLength;
            
            finish_event_pushed_ = true;
            
            if (json.contains("usageMetadata")) {
              auto& usage_meta = json["usageMetadata"];
              Usage usage;
              usage.prompt_tokens = usage_meta.value("promptTokenCount", 0);
              usage.completion_tokens = usage_meta.value("candidatesTokenCount", 0);
              usage.total_tokens = usage_meta.value("totalTokenCount", 0);
              usage.cached_prompt_tokens = usage_meta.value("cachedContentTokenCount", 0);
              usage.reasoning_completion_tokens =
                  usage_meta.value("thoughtsTokenCount", 0);
              push_event(StreamEvent(kStreamEventTypeFinish, usage, finish_reason));
            } else {
              push_event(StreamEvent(kStreamEventTypeFinish));
            }
          }
        }
        return;
      } else if (protocol_ == StreamProtocol::kGeminiEnvelope &&
                 json.contains("usageMetadata")) {
        auto& usage_meta = json["usageMetadata"];
        Usage usage;
        usage.prompt_tokens = usage_meta.value("promptTokenCount", 0);
        usage.completion_tokens = usage_meta.value("candidatesTokenCount", 0);
        usage.total_tokens = usage_meta.value("totalTokenCount", 0);
        usage.cached_prompt_tokens = usage_meta.value("cachedContentTokenCount", 0);
        usage.reasoning_completion_tokens =
            usage_meta.value("thoughtsTokenCount", 0);
        finish_event_pushed_ = true;
        push_event(StreamEvent(kStreamEventTypeFinish, usage, kFinishReasonStop));
        return;
      }

      if (!json.contains("choices") || !json["choices"].is_array()) return;
      auto& choices = json["choices"];

      if (!choices.empty() && choices[0].contains("delta")) {
        auto& delta = choices[0]["delta"];
        if (delta.contains("content") && !delta["content"].is_null()) {
          const auto& content = delta["content"];
          if (content.is_string()) {
            LOG_DEBUG("Received content chunk - length: {}",
                      content.get<std::string>().length());
            push_event(StreamEvent(content.get<std::string>()));
          } else if (content.is_array()) {
            // Muse Spark: mixed text + thought parts in one delta.
            std::string text;
            std::string thought;
            for (const auto& part : content) {
              if (!part.is_object()) {
                if (part.is_string()) text += part.get<std::string>();
                continue;
              }
              const auto type = part.value("type", "");
              std::string piece;
              if (part.contains("text") && part["text"].is_string()) {
                piece = part["text"].get<std::string>();
              } else if (part.contains("content") && part["content"].is_string()) {
                piece = part["content"].get<std::string>();
              } else if (part.contains("thought") && part["thought"].is_string()) {
                piece = part["thought"].get<std::string>();
              }
              const bool is_thought =
                  type == "thought" || type == "reasoning" ||
                  type == "thinking" ||
                  (part.contains("thought") && part["thought"].is_boolean() &&
                   part["thought"].get<bool>());
              if (is_thought) {
                thought += piece;
              } else {
                text += piece;
              }
            }
            if (!thought.empty()) {
              push_event(StreamEvent::reasoning(thought));
            }
            if (!text.empty()) push_event(StreamEvent(text));
          }
        }

        const auto reasoning = extract_openai_reasoning_text(delta);
        if (!reasoning.empty()) {
          LOG_DEBUG("Received reasoning chunk - length: {}", reasoning.length());
          push_event(StreamEvent::reasoning(reasoning));
        }
      }

      // Check for finish_reason
      if (!choices.empty() && choices[0].contains("finish_reason") &&
          !choices[0]["finish_reason"].is_null()) {
        auto finish_reason_str = choices[0]["finish_reason"].get<std::string>();
        auto finish_reason = parse_finish_reason(finish_reason_str);

        LOG_DEBUG("Stream finished with reason: {}",
                              finish_reason_str);

        finish_event_pushed_ = true;

        if (qcode::utils::is_empty_upstream_network_drop(json)) {
          push_event(create_error_event(
              "Upstream network error: empty completion"));
        } else if (json.contains("usage")) {
          auto usage = parse_usage(json["usage"]);
          LOG_INFO(
              "Stream completed - tokens used: {} prompt, {} completion, {} "
              "total",
              usage.prompt_tokens, usage.completion_tokens, usage.total_tokens);
          push_event(StreamEvent(kStreamEventTypeFinish, usage, finish_reason));
        } else {
          push_event(StreamEvent(kStreamEventTypeFinish));
        }
      }
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to parse SSE line: {} - Line content: {}",
                            e.what(), data);
    }
  } else if (!line.empty()) {
    LOG_DEBUG("Ignoring non-data SSE line: {}", line);
  }
}

StreamEvent OpenAIStreamImpl::create_error_event(const std::string& message) {
  LOG_DEBUG("Creating error event: {}", message);
  return StreamEvent(kStreamEventTypeError, message);
}

FinishReason OpenAIStreamImpl::parse_finish_reason(
    const std::string& reason_str) {
  if (reason_str == "stop") {
    return kFinishReasonStop;
  } else if (reason_str == "length") {
    return kFinishReasonLength;
  } else if (reason_str == "content_filter") {
    return kFinishReasonContentFilter;
  }
  return kFinishReasonStop;
}

Usage OpenAIStreamImpl::parse_usage(const nlohmann::json& usage_json) {
  Usage usage;
  usage.prompt_tokens = usage_json.value("prompt_tokens", usage_json.value("input_tokens", 0));
  usage.completion_tokens = usage_json.value("completion_tokens", usage_json.value("output_tokens", 0));
  usage.total_tokens = usage_json.value("total_tokens", usage.prompt_tokens + usage.completion_tokens);
  if (usage_json.contains("prompt_tokens_details") && usage_json["prompt_tokens_details"].is_object()) {
    usage.cached_prompt_tokens = usage_json["prompt_tokens_details"].value("cached_tokens", 0);
  } else if (usage_json.contains("cache_read_input_tokens")) {
    usage.cached_prompt_tokens = usage_json.value("cache_read_input_tokens", 0);
  }
  // Thinking-token accounting (upstream Usage.reasoningTokens).
  if (usage_json.contains("completion_tokens_details") &&
      usage_json["completion_tokens_details"].is_object()) {
    usage.reasoning_completion_tokens =
        usage_json["completion_tokens_details"].value("reasoning_tokens", 0);
  }
  if (usage.reasoning_completion_tokens == 0) {
    usage.reasoning_completion_tokens = usage_json.value("reasoning_tokens", 0);
  }
  return usage;
}

}  // namespace openai
}  // namespace qcode
