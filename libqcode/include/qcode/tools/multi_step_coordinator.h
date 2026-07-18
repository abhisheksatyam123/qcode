#pragma once

#include <qcode/types/generate_options.h>
#include <qcode/types/message.h>
#include <qcode/types/tool.h>

#include <functional>
#include <vector>

namespace ai {

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

 private:
  static Messages tool_results_to_messages(
      const std::vector<ToolCall>& tool_calls,
      const std::vector<ToolResult>& tool_results);
};

}  // namespace ai
