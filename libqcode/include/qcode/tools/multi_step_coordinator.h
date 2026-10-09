#pragma once

#include <qcode/core/generate_options.h>
#include <qcode/core/message.h>
#include <qcode/core/tool.h>

#include <functional>
#include <vector>

namespace qcode {

/// Coordinates multi-step tool-calling loops with the model.
class MultiStepCoordinator {
 public:
  /// Execute a multi-step tool calling workflow.
  /// @param initial_options The initial generation options
  /// @param generate_func Function to call the model (provided by client)
  /// @return Final generation result with all steps
  static GenerateResult execute_multi_step(
      const GenerateOptions& initial_options,
      const std::function<GenerateResult(const GenerateOptions&)>&
          generate_func);

  /// Auto-compaction for the loop (subagent threads). When a step's request
  /// reaches `threshold` tokens of context, the next request is first sent
  /// with the compaction directive appended (compaction::build_in_loop_request)
  /// and the conversation is replaced by compaction::continuation_message.
  /// `on_compacted(message)` lets the caller persist it. threshold 0 = off.
  struct AutoCompact {
    size_t threshold = 0;
    std::function<void(const std::string&)> on_compacted;
  };
  static GenerateResult execute_multi_step(
      const GenerateOptions& initial_options,
      const std::function<GenerateResult(const GenerateOptions&)>&
          generate_func,
      const AutoCompact& auto_compact);

 private:
  static Messages tool_results_to_messages(
      const std::vector<ToolCall>& tool_calls,
      const std::vector<ToolResult>& tool_results);
};

}  // namespace qcode
