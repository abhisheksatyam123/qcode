#pragma once

#include <qcode/core/bus_port.h>
#include <qcode/core/client.h>
#include <qcode/core/generate_options.h>
#include <qcode/core/stream_event.h>
#include <qcode/core/stream_options.h>
#include <qcode/generation/generation_service.h>

#include <optional>
#include <string>
#include <vector>

namespace qcode {

// Folds one model step's stream events into the GenerateResult that
// generate_text() gives for the same response (OpenAIResponseParser's shape):
// text, reasoning and its signature, tool calls, the last reported usage, the
// finish reason (ToolCalls when there are calls) and one assistant message.
// An error result keeps the text and reasoning streamed before the error.
class StreamAccumulator {
 public:
  void add(const StreamEvent& event);
  GenerateResult take();

 private:
  std::string text_;
  std::string reasoning_;
  std::string reasoning_signature_;
  std::vector<ToolCall> tool_calls_;
  Usage usage_;
  FinishReason finish_reason_ = kFinishReasonStop;
  std::optional<std::string> error_;
};

// One tool-loop step over client.stream_text(). Publishes ReasoningDelta and
// MessageDelta while the model writes (33 ms batches, reasoning first, all
// flushed before returning) and returns the accumulated step. Checks
// ctx.abort_flag every 250 ms and then returns "Aborted by user"; posts a
// "Still waiting for the model" notice after each 15 s without events.
GenerateResult stream_step(Client& client, const StreamOptions& options,
                           bus::BusPort& bus, const GenerationContext& ctx);

}  // namespace qcode
