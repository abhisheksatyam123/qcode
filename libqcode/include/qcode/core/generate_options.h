#pragma once

#include <qcode/core/enums.h>
#include <qcode/core/message.h>
#include <qcode/core/model.h>
#include <qcode/core/tool.h>
#include <qcode/core/usage.h>
#include <qcode/core/retry_policy.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qcode {

struct GenerateOptions {
  std::string model;
  std::string system;
  std::string prompt;
  Messages messages;
  std::optional<nlohmann::json> response_format{};
  std::optional<int> max_tokens;
  std::optional<double> temperature;
  std::optional<double> top_p;
  std::optional<int> seed;
  std::optional<double> frequency_penalty;
  std::optional<double> presence_penalty;

  // Reasoning / extended-thinking support (opt-in; provider-specific)
  std::optional<std::string> reasoning_effort;  // openai o-series: "low"/"medium"/"high"
  std::optional<int> budget_tokens;            // anthropic: thinking budget

  // Tool calling support
  ToolSet tools;
  std::string workspace;
  // Stable conversation key for providers that reuse a conversation id
  // across turns (e.g. Cursor). Usually the TUI/server session UUID.
  std::string session_id;
  ToolChoice tool_choice = ToolChoice::auto_choice();
  int max_steps = 1;
  std::vector<std::string> active_tools;

  // Multi-agent hook (opencode TaskTool parity): injected by the generation
  // layer so the task tool can run a real nested subagent turn. Returns JSON
  // with "output" (final subagent text) or "error".
  SubagentRunner subagent_runner;
  std::shared_ptr<std::atomic<bool>> abort_flag{nullptr};
  // When true, ServerSideDuplex streams (Cursor/Grok) yield so a queued
  // follow-up can start without the user having to Esc-abort the turn.
  std::function<bool()> has_queued_work;

  // Callbacks for tool calling and retries
  std::optional<std::function<void(const GenerateStep&)>> on_step_finish;
  std::optional<std::function<void(const ToolCall&)>> on_tool_call_start;
  std::optional<std::function<void(const ToolResult&)>> on_tool_call_finish;
  std::optional<std::function<bool(const ToolCall&)>> on_tool_call_confirm;
  std::optional<retry::RetryCallback> on_retry;

  GenerateOptions(std::string model_name, std::string user_prompt)
      : model(std::move(model_name)), prompt(std::move(user_prompt)) {}

  GenerateOptions(std::string model_name,
                  std::string system_prompt,
                  std::string user_prompt)
      : model(std::move(model_name)),
        system(std::move(system_prompt)),
        prompt(std::move(user_prompt)) {}

  GenerateOptions(std::string model_name,
                  std::string system_prompt,
                  std::string user_prompt,
                  std::optional<nlohmann::json> response_format_)
      : model(std::move(model_name)),
        system(std::move(system_prompt)),
        prompt(std::move(user_prompt)),
        response_format(std::move(response_format_)) {}

  GenerateOptions(std::string model_name, Messages conversation)
      : model(std::move(model_name)), messages(std::move(conversation)) {}

  GenerateOptions() = default;

  bool is_valid() const {
    return !model.empty() && (!prompt.empty() || !messages.empty());
  }

  bool has_messages() const { return !messages.empty(); }

  bool has_tools() const { return !tools.empty(); }

  bool is_multi_step() const { return max_steps > 1; }

  std::vector<std::string> get_active_tool_names() const {
    if (active_tools.empty()) {
      std::vector<std::string> all_names;
      for (const auto& [name, tool] : tools) {
        all_names.push_back(name);
      }
      return all_names;
    }
    return active_tools;
  }
};

struct GenerateResult {
  std::string text;
  // Thinking/reasoning text returned alongside this step (non-streaming
  // responses carry it on message.reasoning_content etc).
  std::string reasoning;
  FinishReason finish_reason = kFinishReasonError;
  Usage usage;

  /// Additional metadata (like TypeScript SDK)
  std::optional<std::string> id;
  std::optional<std::string> model;
  std::optional<int64_t> created;
  std::optional<std::string> system_fingerprint;

  /// Error handling
  std::optional<std::string> error;
  std::vector<std::string> warnings;
  std::optional<bool> is_retryable;
  /// Provider-provided Retry-After hint in milliseconds (from retry-after-ms
  /// or Retry-After headers). Honored verbatim by the retry policy, mirroring
  /// upstream opencode's SessionRetry.delay().
  std::optional<long long> retry_after_ms;

  /// Provider-specific metadata
  std::optional<std::string> provider_metadata;

  /// Response messages for multi-turn (includes the assistant's response)
  Messages response_messages;

  // Tool calling results
  std::vector<ToolCall> tool_calls;
  std::vector<ToolResult> tool_results;
  std::vector<GenerateStep> steps;

  GenerateResult() = default;

  GenerateResult(std::string generated_text,
                 FinishReason reason,
                 Usage token_usage)
      : text(std::move(generated_text)),
        finish_reason(reason),
        usage(token_usage) {}

  explicit GenerateResult(std::string error_message)
      : finish_reason(kFinishReasonError), error(std::move(error_message)) {}

  bool is_success() const {
    return finish_reason == kFinishReasonStop ||
           finish_reason == kFinishReasonLength ||
           finish_reason == kFinishReasonToolCalls;
  }

  explicit operator bool() const { return is_success(); }

  std::string error_message() const { return error.value_or(""); }

  bool has_tool_calls() const { return !tool_calls.empty(); }
  bool has_tool_results() const { return !tool_results.empty(); }

  std::string finishReasonToString() const {
    switch (finish_reason) {
      case kFinishReasonStop:
        return "stop";
      case kFinishReasonLength:
        return "length";
      case kFinishReasonToolCalls:
        return "tool_calls";
      case kFinishReasonContentFilter:
        return "content_filter";
      case kFinishReasonError:
        return "error";
    }
    return "unknown";
  }
};

}  // namespace qcode
