#include "generation/stream_step.h"

#include <qcode/core/errors.h>
#include <qcode/core/event.h>
#include <qcode/core/logger.h>

#include <chrono>
#include <utility>

namespace qcode {

using namespace contract;

namespace {
constexpr auto kFlushInterval = std::chrono::milliseconds(33);
constexpr auto kAbortSlice = std::chrono::milliseconds(250);
constexpr auto kHeartbeatInterval = std::chrono::seconds(15);

bool has_usage(const Usage& usage) {
  return usage.prompt_tokens > 0 || usage.completion_tokens > 0 ||
         usage.total_tokens > 0;
}
}  // namespace

void StreamAccumulator::add(const StreamEvent& event) {
  if (event.is_text_delta()) {
    text_ += event.text_delta;
  } else if (event.is_reasoning_delta()) {
    reasoning_ += event.text_delta;
    if (reasoning_signature_.empty() && event.metadata) {
      reasoning_signature_ = *event.metadata;
    }
  } else if (event.is_tool_call()) {
    // OpenAIResponseParser's rules: no arguments means {}, unparsable
    // arguments drop the call.
    JsonValue arguments = JsonValue::object();
    if (!event.tool_payload.empty() && event.tool_payload != "null") {
      try {
        arguments = JsonValue::parse(event.tool_payload);
      } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse tool call arguments: {}", e.what());
        return;
      }
    }
    tool_calls_.emplace_back(event.tool_call_id, event.tool_name,
                             std::move(arguments), event.metadata.value_or(""));
  } else if (event.is_finish()) {
    // Usage may come on its own after the finish event; keep the last one.
    if (event.usage && has_usage(*event.usage)) usage_ = *event.usage;
    if (event.finish_reason) finish_reason_ = *event.finish_reason;
  } else if (event.is_error() && !error_) {
    error_ = event.error.value_or("");
    if (error_->empty()) error_ = "Error during streaming";
  }
}

GenerateResult StreamAccumulator::take() {
  GenerateResult result;
  result.text = std::move(text_);
  result.reasoning = std::move(reasoning_);
  if (error_) {
    result.is_retryable = is_error_message_retryable(*error_);
    result.error = std::move(error_);
    result.finish_reason = kFinishReasonError;
    return result;
  }
  result.usage = usage_;
  result.finish_reason =
      tool_calls_.empty() ? finish_reason_ : kFinishReasonToolCalls;
  result.tool_calls = std::move(tool_calls_);

  if (result.has_tool_calls()) {
    std::vector<ToolCallContentPart> calls;
    calls.reserve(result.tool_calls.size());
    for (const auto& call : result.tool_calls) {
      calls.emplace_back(call.id, call.tool_name, call.arguments,
                         call.thought_signature);
    }
    auto assistant = Message::assistant_with_tools(result.text, calls);
    if (!result.reasoning.empty()) {
      assistant.content.emplace_back(
          ReasoningContentPart{result.reasoning, reasoning_signature_});
    }
    result.response_messages.push_back(std::move(assistant));
  } else if (!result.reasoning.empty()) {
    result.response_messages.push_back(Message::assistant_with_reasoning(
        result.text, result.reasoning, reasoning_signature_));
  } else if (!result.text.empty()) {
    result.response_messages.push_back(Message::assistant(result.text));
  }
  return result;
}

GenerateResult stream_step(Client& client, const StreamOptions& options,
                           bus::BusPort& bus, const GenerationContext& ctx) {
  // Like generate_text(), a failure to build the request is a step error.
  
  auto start_time = std::chrono::steady_clock::now();
  double ttft_ms = -1.0;
StreamResult stream;
  try {
    stream = client.stream_text(options);
  } catch (const std::exception& e) {
    return GenerateResult("Exception: " + std::string(e.what()));
  }
  StreamAccumulator step;

  std::string text_buffer;
  std::string reasoning_buffer;
  std::string reasoning_signature;
  std::string published_signature;
  auto last_flush = std::chrono::steady_clock::now();
  // Reasoning first, so the thinking block renders above the text it led to.
  // Anthropic signs a thinking block at its end, usually after its text was
  // flushed: a text-less delta then carries the signature, which the history
  // mirrors (TUI, server rows) need to replay the block like the next step.
  auto flush = [&]() {
    if (!reasoning_buffer.empty() || reasoning_signature != published_signature) {
      bus.publish<ReasoningDelta>({
          .session_id = ctx.session_id,
          .text = std::move(reasoning_buffer),
          .signature = reasoning_signature,
          .done = false,
      });
      reasoning_buffer.clear();
      published_signature = reasoning_signature;
    }
    if (!text_buffer.empty()) {
      bus.publish<MessageDelta>({
          .session_id = ctx.session_id,
          .text = std::move(text_buffer),
          .done = false,
      });
      text_buffer.clear();
    }
    last_flush = std::chrono::steady_clock::now();
  };

  auto last_event = std::chrono::steady_clock::now();
  auto next_heartbeat = last_event + kHeartbeatInterval;
  while (!stream.is_complete()) {
    // Wake in time for a pending flush; otherwise once per abort slice.
    const bool buffered = !text_buffer.empty() || !reasoning_buffer.empty();
    auto event = stream.poll(buffered ? kFlushInterval : kAbortSlice);
    const auto now = std::chrono::steady_clock::now();

    if (ctx.abort_flag && ctx.abort_flag->load()) {
      LOG_INFO("stream_step: abort requested");
      flush();
      stream.stop();
      return GenerateResult("Aborted by user");
    }

    if (!event) {
      if (buffered && now - last_flush >= kFlushInterval) flush();
      if (now >= next_heartbeat) {
        const auto waited =
            std::chrono::duration_cast<std::chrono::seconds>(now - last_event);
        bus.publish<ErrorOccurred>({
            .session_id = ctx.session_id,
            .message = "Still waiting for the model (" +
                       std::to_string(waited.count()) + "s)…",
            .severity = "info",
        });
        next_heartbeat = now + kHeartbeatInterval;
      }
      continue;
    }


    last_event = now;
    if (ttft_ms < 0.0 && (event->is_text_delta() || event->is_reasoning_delta() || event->is_tool_call())) {
      ttft_ms = std::chrono::duration<double, std::milli>(now - start_time).count();
    }
    next_heartbeat = now + kHeartbeatInterval;

    step.add(*event);
    if (event->is_text_delta()) {
      text_buffer += event->text_delta;
    } else if (event->is_reasoning_delta()) {
      // OpenRouter encrypts reasoning as [REDACTED]; the UI never shows it.
      if (event->text_delta.find("[REDACTED]") == std::string::npos) {
        reasoning_buffer += event->text_delta;
      }
      if (event->metadata && !event->metadata->empty()) {
        reasoning_signature = *event->metadata;
      }
    } else if (event->is_error()) {
      break;
    }
    if (now - last_flush >= kFlushInterval) flush();
  }


  flush();
  auto res = step.take();
  if (ttft_ms >= 0.0) res.ttft_ms = ttft_ms;
  return res;

}

}  // namespace qcode
