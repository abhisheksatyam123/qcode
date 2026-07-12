#pragma once

#include "enums.h"
#include "usage.h"

#include <optional>
#include <string>

namespace ai {

struct StreamEvent {
  explicit StreamEvent(std::string text)
      : type(kStreamEventTypeTextDelta), text_delta(std::move(text)) {}

  StreamEvent(StreamEventType event_type, std::string error_msg)
      : type(event_type), error(std::move(error_msg)) {}

  StreamEvent(StreamEventType event_type,
              Usage usage_stats,
              FinishReason reason)
      : type(event_type), usage(usage_stats), finish_reason(reason) {}

  explicit StreamEvent(StreamEventType event_type) : type(event_type) {}

  bool is_text_delta() const { return type == kStreamEventTypeTextDelta; }

  // Reasoning/thinking delta: text in text_delta; signature (if any) in metadata.
  static StreamEvent reasoning(std::string text,
                               std::optional<std::string> signature = std::nullopt) {
    StreamEvent e(kStreamEventTypeReasoningDelta);
    e.text_delta = std::move(text);
    e.metadata = std::move(signature);
    return e;
  }
  bool is_reasoning_delta() const { return type == kStreamEventTypeReasoningDelta; }

  bool is_error() const { return type == kStreamEventTypeError; }

  bool is_finish() const { return type == kStreamEventTypeFinish; }

  StreamEventType type;
  std::string text_delta;
  std::optional<std::string> error;
  std::optional<Usage> usage;
  std::optional<FinishReason> finish_reason;
  std::optional<std::string> metadata;
};

}  // namespace ai