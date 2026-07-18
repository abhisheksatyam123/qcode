#pragma once

namespace qcode {

enum MessageRole {
  kMessageRoleSystem,
  kMessageRoleUser,
  kMessageRoleAssistant
};

enum FinishReason {
  kFinishReasonStop,
  kFinishReasonLength,
  kFinishReasonContentFilter,
  kFinishReasonToolCalls,
  kFinishReasonError
};

enum StreamEventType {
  kStreamEventTypeTextDelta,
  kStreamEventTypeReasoningDelta,
  kStreamEventTypeToolCall,
  kStreamEventTypeToolResult,
  kStreamEventTypeStepStart,
  kStreamEventTypeStepFinish,
  kStreamEventTypeFinish,
  kStreamEventTypeError
};

}  // namespace qcode