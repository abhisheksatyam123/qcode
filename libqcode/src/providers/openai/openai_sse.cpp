#include "openai_stream.h"

#include <array>
#include <optional>
#include <string_view>

#include "openai_response_parser.h"
#include <qcode/core/logger.h>
#include <qcode/transform/gemini_transform.h>
#include "core/response_utils.h"

namespace qcode {
namespace openai {

namespace {

// First reasoning_details signature in a delta (OpenRouter); the
// non-streaming parser keeps the same one.
std::optional<std::string> reasoning_signature(const nlohmann::json& delta) {
  const auto details = delta.find("reasoning_details");
  if (details == delta.end() || !details->is_array()) return std::nullopt;
  for (const auto& detail : *details) {
    if (!detail.is_object()) continue;
    const auto sig = detail.find("signature");
    if (sig != detail.end() && sig->is_string() &&
        !sig->get_ref<const std::string&>().empty()) {
      return sig->get<std::string>();
    }
  }
  return std::nullopt;
}

// Fast path for the commonest SSE event: a chat-completions chunk whose only
// payload is one text delta, `choices[0].delta.content`, with no finish_reason.
// Every other shape returns nullopt and goes through the full DOM parse. The
// accepted shapes are checked strictly, so an unexpected field, escape or
// layout falls back rather than being guessed at.
std::optional<std::string> plain_content_delta(std::string_view data) {
  // Any of these anywhere in the line means the event may carry more than a
  // text delta, so leave it to the full parser.
  static constexpr std::array<std::string_view, 12> kSlowMarkers = {
      "tool_calls", "function", "reasoning", "usage", "error", "candidates",
      "response", "refusal", "\"type\"", "thought", "signature", "audio"};
  for (const auto marker : kSlowMarkers) {
    if (data.find(marker) != std::string_view::npos) return std::nullopt;
  }

  // Compact (OpenAI) and spaced (Python json.dumps) layouts.
  static constexpr std::string_view kCompactHead =
      R"("choices":[{"index":0,"delta":{"content":")";
  static constexpr std::string_view kSpacedHead =
      R"("choices": [{"index": 0, "delta": {"content": ")";
  size_t start = data.find(kCompactHead);
  if (start != std::string_view::npos) {
    start += kCompactHead.size();
  } else {
    start = data.find(kSpacedHead);
    if (start == std::string_view::npos) return std::nullopt;
    start += kSpacedHead.size();
  }

  // Decode the JSON string literal. \u escapes are left to the full parser.
  std::string text;
  size_t i = start;
  for (;;) {
    if (i >= data.size()) return std::nullopt;
    const char c = data[i];
    if (c == '"') break;
    if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
    if (c != '\\') {
      text += c;
      ++i;
      continue;
    }
    if (i + 1 >= data.size()) return std::nullopt;
    switch (data[i + 1]) {
      case '"': text += '"'; break;
      case '\\': text += '\\'; break;
      case '/': text += '/'; break;
      case 'n': text += '\n'; break;
      case 't': text += '\t'; break;
      case 'r': text += '\r'; break;
      case 'b': text += '\b'; break;
      case 'f': text += '\f'; break;
      default: return std::nullopt;
    }
    i += 2;
  }

  // Everything after the closing quote must close the delta and the chunk
  // with finish_reason null, and nothing else. Whitespace is ignored.
  std::string tail;
  for (size_t j = i + 1; j < data.size(); ++j) {
    if (data[j] != ' ') tail += data[j];
  }
  if (tail == R"(},"finish_reason":null}]})" ||
      tail == R"(},"logprobs":null,"finish_reason":null}]})") {
    return text;
  }
  return std::nullopt;
}

}  // namespace

void OpenAIStreamImpl::parse_sse_line(const std::string& line) {
  if (line.starts_with("data:")) {
    auto data = line.substr(5);
    if (!data.empty() && data.front() == ' ') data.erase(0, 1);

    if (data == "[DONE]") {
      LOG_DEBUG("Received [DONE] signal, stream ending");
      push_finish_event_if_needed();
      mark_complete();
      return;
    }


    if (fast_delta_path_) {
      if (auto text = plain_content_delta(data)) {
        push_event(StreamEvent(std::move(*text)));
        return;
      }
    }

    try {
      auto json = nlohmann::json::parse(data);

      const auto event_type = json.value("type", "");
      if (event_type == "response.output_item.done") {
        // Providers that return the summary only on the finished item (no
        // deltas) still surface their thinking this way.
        const auto item = json.value("item", nlohmann::json::object());
        if (item.value("type", "") == "reasoning" && !reasoning_seen_) {
          std::string text;
          if (item.contains("summary") && item["summary"].is_array()) {
            for (const auto& part : item["summary"]) {
              text += part.value("text", "");
            }
          }
          if (text.empty()) text = extract_openai_reasoning_text(item);
          if (!text.empty()) {
            reasoning_seen_ = true;
            push_event(StreamEvent::reasoning(text));
          }
        }
        return;
      }
      if (event_type == "response.output_item.added") {
        const auto item = json.value("item", nlohmann::json::object());
        if (item.value("type", "") == "function_call") {
          PendingToolCall tc;
          tc.id = item.value("call_id", item.value("id", ""));
          tc.name = item.value("name", "");
          pending_tool_calls_.push_back(std::move(tc));
        }
        return;
      }
      if (event_type == "response.function_call_arguments.delta") {
        if (!pending_tool_calls_.empty()) {
          pending_tool_calls_.back().arguments += json.value("delta", "");
        }
        return;
      }
      if (event_type == "response.function_call_arguments.done") {
        if (!pending_tool_calls_.empty()) {
          if (json.contains("arguments") && json["arguments"].is_string()) {
            pending_tool_calls_.back().arguments = json["arguments"].get<std::string>();
          }
        }
        return;
      }
      if (event_type == "response.output_text.delta") {
        push_event(StreamEvent(json.value("delta", "")));
        return;
      }
      // The Responses API streams thinking as a *summary*: these are the
      // event names it actually emits. response.reasoning_text.delta is kept
      // for providers that use it.
      if (event_type == "response.reasoning_summary_text.delta" ||
          event_type == "response.reasoning_text.delta") {
        const auto delta = json.value("delta", "");
        if (!delta.empty()) reasoning_seen_ = true;
        push_event(StreamEvent::reasoning(delta));
        return;
      }
      if (event_type == "response.reasoning_summary_part.added" ||
          event_type == "response.reasoning_summary_part.done") {
        // Boundary marker only; the text arrives as deltas.
        return;
      }
      if (event_type == "response.completed" ||
          event_type == "response.incomplete") {
        push_pending_tool_calls();
        Usage usage;
        const auto response = json.value("response", nlohmann::json::object());
        if (response.contains("usage") && response["usage"].is_object()) {
          // input_tokens / output_tokens_details.reasoning_tokens /
          // input_tokens_details.cached_tokens all land here now.
          usage = parse_usage(response["usage"]);
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
            msg += "\n\n💡 Tip: Rate limit hit. Switch model via `/model minimax-m3.1-flash-free`, `/model nemotron-3.5-lightning-free`, or `/model big-pickle`.";
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
              // Gemini sends each function call whole in one chunk, with the
              // thought signature Gemini 3 requires back on replay. Same
              // shape as the blocking parser (normalize_gemini_response).
              if (part.contains("functionCall") && part["functionCall"].is_object()) {
                const auto& function = part["functionCall"];
                std::optional<std::string> signature;
                if (part.contains("thoughtSignature") &&
                    part["thoughtSignature"].is_string()) {
                  signature = part["thoughtSignature"].get<std::string>();
                }
                const auto id = function.contains("id") && function["id"].is_string()
                                    ? function["id"].get<std::string>()
                                    : gemini::new_uuid();
                push_event(StreamEvent::tool_call(
                    id, function.value("name", ""),
                    function.value("args", nlohmann::json::object()).dump(),
                    std::move(signature)));
              }
            }
          }
          
          if (cand.contains("finishReason")) {
            std::string reason_str = cand["finishReason"].get<std::string>();
            FinishReason finish_reason = kFinishReasonStop;
            if (reason_str == "STOP") finish_reason = kFinishReasonStop;
            else if (reason_str == "MAX_TOKENS") finish_reason = kFinishReasonLength;
            else if (reason_str == "SAFETY" || reason_str == "RECITATION" ||
                     reason_str == "PROHIBITED_CONTENT" || reason_str == "BLOCKLIST" ||
                     reason_str == "SPII") finish_reason = kFinishReasonContentFilter;
            
            finish_event_pushed_ = true;
            
            if (json.contains("usageMetadata")) {
              auto& usage_meta = json["usageMetadata"];
              Usage usage;
              usage.prompt_tokens = usage_meta.value("promptTokenCount", 0);
              // Thoughts are billed as output: completion includes them.
              usage.completion_tokens = usage_meta.value("candidatesTokenCount", 0) +
                                        usage_meta.value("thoughtsTokenCount", 0);
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
        // Thoughts are billed as output: completion includes them.
        usage.completion_tokens = usage_meta.value("candidatesTokenCount", 0) +
                                  usage_meta.value("thoughtsTokenCount", 0);
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
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
          for (const auto& tc : delta["tool_calls"]) {
            size_t idx = tc.value("index", 0);
            const std::string call_id =
                tc.contains("id") && tc["id"].is_string()
                    ? tc["id"].get<std::string>()
                    : std::string();
            // Some OpenAI-compatible models (notably big-pickle) emit each
            // complete tool call with a fresh id but reuse index 0. Treat a
            // new id at an occupied index as a separate call instead of
            // concatenating both argument objects into invalid JSON.
            if (!call_id.empty() && pending_tool_calls_.size() > idx &&
                !pending_tool_calls_[idx].id.empty() &&
                pending_tool_calls_[idx].id != call_id) {
              pending_tool_calls_.push_back(PendingToolCall{});
              idx = pending_tool_calls_.size() - 1;
            }
            if (pending_tool_calls_.size() <= idx) {
              pending_tool_calls_.resize(idx + 1);
            }
            if (!call_id.empty()) {
              pending_tool_calls_[idx].id = call_id;
            }
            if (tc.contains("thought_signature") &&
                tc["thought_signature"].is_string()) {
              pending_tool_calls_[idx].thought_signature =
                  tc["thought_signature"].get<std::string>();
            }
            if (tc.contains("function") && tc["function"].is_object()) {
              const auto& fn = tc["function"];
              if (fn.contains("name") && fn["name"].is_string()) {
                pending_tool_calls_[idx].name += fn["name"].get<std::string>();
              }
              if (fn.contains("arguments") && fn["arguments"].is_string()) {
                pending_tool_calls_[idx].arguments += fn["arguments"].get<std::string>();
              }
            }
          }
        }
        if (delta.contains("content") && !delta["content"].is_null()) {
          const auto& content = delta["content"];
          if (content.is_string()) {
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
        auto signature = reasoning_signature(delta);
        if (!reasoning.empty() || signature) {
          push_event(StreamEvent::reasoning(reasoning, std::move(signature)));
        }
      }

      // Check for finish_reason
      if (!choices.empty() && choices[0].contains("finish_reason") &&
          !choices[0]["finish_reason"].is_null()) {
        auto finish_reason_str = choices[0]["finish_reason"].get<std::string>();
        auto finish_reason = parse_finish_reason(finish_reason_str);

        LOG_DEBUG("Stream finished with reason: {}",
                              finish_reason_str);

        push_pending_tool_calls();

        finish_event_pushed_ = true;

        if (qcode::utils::is_empty_upstream_network_drop(json)) {
          push_event(create_error_event(
              "Upstream network error: empty completion"));
        } else if (json.contains("usage") && json["usage"].is_object()) {
          // Zen free models emit "usage":null on the finish_reason chunk;
          // parse_usage() would throw on null and the finish event (with its
          // cache/token accounting) would be lost.
          auto usage = parse_usage(json["usage"]);
          LOG_INFO(
              "Stream completed - tokens used: {} prompt, {} completion, {} "
              "total",
              usage.prompt_tokens, usage.completion_tokens, usage.total_tokens);
          usage_reported_ = true;
          push_event(StreamEvent(kStreamEventTypeFinish, usage, finish_reason));
        } else {
          StreamEvent finish(kStreamEventTypeFinish);
          finish.finish_reason = finish_reason;
          push_event(std::move(finish));
        }
      } else if (finish_event_pushed_ && !usage_reported_ &&
                 json.contains("usage") && json["usage"].is_object()) {
        // stream_options.include_usage: the counts come after the finish
        // chunk, in a chunk of their own with no choices. Report them once.
        usage_reported_ = true;
        StreamEvent usage_event(kStreamEventTypeFinish);
        usage_event.usage = parse_usage(json["usage"]);
        push_event(std::move(usage_event));
      }
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to parse SSE line: {} - Line content: {}",
                            e.what(), data);
    }
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
  } else if (reason_str == "tool_calls") {
    return kFinishReasonToolCalls;
  } else if (reason_str == "length") {
    return kFinishReasonLength;
  } else if (reason_str == "content_filter") {
    return kFinishReasonContentFilter;
  }
  return kFinishReasonStop;
}

// Thinking-token accounting lives in parse_openai_usage (shared with the
// non-streaming parser) so chat, Responses and Gemini streams agree.
Usage OpenAIStreamImpl::parse_usage(const nlohmann::json& usage_json) {
  return parse_openai_usage(usage_json);
}

}  // namespace openai
}  // namespace qcode
