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

std::string explain_stream_root_cause(
    const StreamDiagnostics& diag,
    double elapsed_sec,
    double ttft_ms,
    size_t reasoning_chars,
    size_t text_chars,
    size_t tool_call_count) {
  if (diag.in_backoff) {
    return "Upstream rate-limit or overload backoff: provider returned HTTP " +
           std::to_string(diag.last_http_status) + ". Backoff in progress (" +
           std::to_string(std::max(0LL, diag.backoff_remaining_ms / 1000)) + "s remaining, attempt " +
           std::to_string(diag.attempt) + "/" + std::to_string(diag.max_attempts) + ").";
  }
  if (ttft_ms < 0.0) {
    if (!diag.headers_received) {
      return "Upstream prompt ingestion / queue delay: request payload (" +
             std::to_string(diag.request_bytes) + " bytes) sent to " +
             (diag.endpoint.empty() ? "provider" : diag.endpoint) +
             ", but no response headers or tokens received after " +
             std::to_string(static_cast<int>(elapsed_sec)) +
             "s (TTFT not reached). Provider may be under high load or processing large context.";
    }
    return "Upstream generation delay: response headers received (HTTP " +
           std::to_string(diag.last_http_status) +
           "), but no stream content/tokens delivered after " +
           std::to_string(static_cast<int>(elapsed_sec)) + "s.";
  }
  // TTFT has arrived
  if (diag.idle_ms > 30000.0) {
    return "Stream stalled mid-generation: no new SSE chunks received for " +
           std::to_string(static_cast<int>(diag.idle_ms / 1000.0)) +
           "s after initial tokens (TTFT was " +
           std::to_string(static_cast<int>(ttft_ms)) +
           "ms). Upstream model may be stuck in reasoning loop or network connection stalled.";
  }
  return "Long active generation: model is actively producing tokens (TTFT " +
         std::to_string(static_cast<int>(ttft_ms)) + "ms, received " +
         std::to_string(diag.chunks_received) + " chunks / " +
         std::to_string(diag.bytes_received) + " bytes, reasoning=" +
         std::to_string(reasoning_chars) + " chars, text=" +
         std::to_string(text_chars) + " chars, tool_calls=" +
         std::to_string(tool_call_count) + ").";
}

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
  auto last_slow_log = start_time;
  size_t accumulated_text_len = 0;
  size_t accumulated_reasoning_len = 0;
  size_t accumulated_tool_calls = 0;

  while (!stream.is_complete()) {
    // Wake in time for a pending flush; otherwise once per abort slice.
    const bool buffered = !text_buffer.empty() || !reasoning_buffer.empty();
    auto event = stream.poll(buffered ? kFlushInterval : kAbortSlice);
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - start_time).count();

    // Monitor for requests taking more than a minute (60s)
    if (elapsed >= 60.0 && (now - last_slow_log >= std::chrono::seconds(30))) {
      last_slow_log = now;
      const auto diag = stream.get_diagnostics();
      const std::string root_cause = explain_stream_root_cause(
          diag, elapsed, ttft_ms,
          accumulated_reasoning_len + reasoning_buffer.size(),
          accumulated_text_len + text_buffer.size(),
          accumulated_tool_calls);

      LOG_WARN("[SLOW LLM REQUEST] Model '{}' request taking {:.1f}s (> 1 min). "
               "Session: {}. Root cause: {}. Diagnostics: endpoint='{}', attempt={}/{}, "
               "status={}, sent_bytes={}, chunks={}, recv_bytes={}, ttft={:.1f}ms, idle={:.1f}s",
               options.model, elapsed, ctx.session_id, root_cause, diag.endpoint,
               diag.attempt, diag.max_attempts, diag.last_http_status,
               diag.request_bytes, diag.chunks_received, diag.bytes_received,
               diag.ttft_ms, diag.idle_ms / 1000.0);

      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = "Model '" + options.model + "' taking " +
                     std::to_string(static_cast<int>(elapsed)) + "s: " + root_cause,
          .severity = "warn",
      });
    }

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
      accumulated_text_len += event->text_delta.size();
      text_buffer += event->text_delta;
    } else if (event->is_reasoning_delta()) {
      // OpenRouter encrypts reasoning as [REDACTED]; the UI never shows it.
      if (event->text_delta.find("[REDACTED]") == std::string::npos) {
        accumulated_reasoning_len += event->text_delta.size();
        reasoning_buffer += event->text_delta;
      }
      if (event->metadata && !event->metadata->empty()) {
        reasoning_signature = *event->metadata;
      }
    } else if (event->is_tool_call()) {
      accumulated_tool_calls++;
    } else if (event->is_error()) {
      break;
    }
    if (now - last_flush >= kFlushInterval) flush();
  }


  flush();
  auto res = step.take();
  if (ttft_ms >= 0.0) res.ttft_ms = ttft_ms;

  const auto total_duration_s =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
  if (total_duration_s >= 60.0) {
    LOG_INFO("[SLOW LLM REQUEST COMPLETED] Model '{}' finished after {:.1f}s (> 1 min). "
             "Finish reason: {}. Total text: {} chars, reasoning: {} chars, tool calls: {}.",
             options.model, total_duration_s, res.finishReasonToString(),
             res.text.size(), res.reasoning.size(), res.tool_calls.size());
  }

  return res;

}

}  // namespace qcode
