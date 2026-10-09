#include <filesystem>
#include <fstream>
#include <algorithm>
#include <qcode/core/logger.h>
#include <chrono>
#include <future>
#include <optional>
#include <thread>

#include <qcode/tools/tool_executor.h>
#include <qcode/tools/tool_factory.h>
#include <qcode/core/tool.h>
#include <qcode/core/utf8.h>

namespace qcode {

ToolResult ToolExecutor::execute_tool(const ToolCall& tool_call,
                                      const ToolSet& tools,
                                      const GenerateOptions* options) {
  LOG_DEBUG("ToolExecutor: execute_tool tool={}", tool_call.tool_name);
  // Validate tool call
  if (!tool_call.is_valid()) {
    return ToolResult(
        tool_call.id, tool_call.tool_name, tool_call.arguments,
        std::string("Invalid tool call: missing required fields"));
  }

  // Check if tool exists
  auto tool_it = tools.find(tool_call.tool_name);
  if (tool_it == tools.end()) {
    if (tool_call.tool_name == "read" && tool_call.arguments.contains("path") &&
        tool_call.arguments["path"].is_string()) {
      std::string path_str = tool_call.arguments["path"].get<std::string>();
      std::filesystem::path p(path_str);
      if (options && !options->workspace.empty() && p.is_relative()) {
        p = std::filesystem::path(options->workspace) / p;
      }
      std::ifstream f(p);
      if (f.is_open()) {
        std::string text_content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
        return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments,
                          nlohmann::json{{"content", text_content}});
      }
      return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments,
                        "Failed to open file: " + path_str);
    }
    LOG_WARN("ToolExecutor: tool '{}' not found", tool_call.tool_name);
    return ToolResult(
        tool_call.id, tool_call.tool_name, tool_call.arguments,
        std::string("Tool not found: '" + tool_call.tool_name + "'"));
  }

  const Tool& tool = tool_it->second;

  // Validate arguments against schema
  if (!validate_tool_call(tool_call, tool)) {
    return ToolResult(
        tool_call.id, tool_call.tool_name, tool_call.arguments,
        std::string("Invalid arguments for tool '" + tool_call.tool_name +
                    "': " + tool_call.arguments.dump()));
  }

  // Check if tool has execution function
  if (!tool.has_execute()) {
    // Tool has no execution function - return the call for forwarding to client
    return ToolResult(
        tool_call.id, tool_call.tool_name, tool_call.arguments,
        JsonValue(std::string(
            "Tool call forwarded to client (no execute function)")));
  }

  // Create execution context
  ToolExecutionContext context;
  context.tool_call_id = tool_call.id;
  if (options) {
    context.workspace = options->workspace;
    context.session_id = options->session_id;
  }
  if (options && options->subagent_runner) {
    context.subagent_runner = options->subagent_runner;
  }
  if (options && options->abort_flag) {
    context.abort_flag = options->abort_flag;
  } else {
    context.abort_flag = std::make_shared<std::atomic<bool>>(false);
  }

  try {
    if (tool.is_async()) {
      return execute_async_tool(tool_call, tool, context, options);
    } else {
      return execute_sync_tool(tool_call, tool, context, options);
    }
  } catch (const std::exception& e) {
    // Return error result instead of throwing to allow graceful handling
    return ToolResult(
        tool_call.id, tool_call.tool_name, tool_call.arguments,
        std::string("Tool execution failed: " + std::string(e.what())));
  }
}

std::vector<ToolResult> ToolExecutor::execute_tools(
    const std::vector<ToolCall>& tool_calls,
    const ToolSet& tools,
    bool parallel,
    const GenerateOptions* options) {
  std::vector<ToolResult> results;
  results.reserve(tool_calls.size());

  if (tool_calls.size() <= 1) parallel = false;

  const bool effective_parallel = parallel;
  // NOTE: a batch containing multiple bash calls used to be forced
  // sequential to avoid memory spikes. That concern is now handled by
  // BashConcurrencyLimiter inside BashTool::exec_run (bounded slots), so
  // parallel batches with several bash calls are admitted again — this was
  // a major serialization point for large parallel subagent workloads.

  if (!effective_parallel) {
    // Execute sequentially
    for (const auto& tool_call : tool_calls) {
      if (options && options->abort_flag && options->abort_flag->load()) {
        ToolResult aborted_res(tool_call.id, tool_call.tool_name, tool_call.arguments,
                               std::string("Tool execution aborted"));
        if (options && options->on_tool_call_finish.has_value()) {
          options->on_tool_call_finish.value()(aborted_res);
        }
        results.push_back(aborted_res);
        continue;
      }

      // Check confirmation if callback is set
      if (options && options->on_tool_call_confirm.has_value()) {
        bool confirmed = options->on_tool_call_confirm.value()(tool_call);
        if (!confirmed) {
          ToolResult rejected_res(tool_call.id, tool_call.tool_name, tool_call.arguments,
                                 std::string("Tool execution rejected by user"));
          // Still notify finish callback
          if (options && options->on_tool_call_finish.has_value()) {
            options->on_tool_call_finish.value()(rejected_res);
          }
          results.push_back(rejected_res);
          continue;
        }
      }

      // Call the on_tool_call_start callback if provided
      if (options && options->on_tool_call_start.has_value()) {
        options->on_tool_call_start.value()(tool_call);
      }

      auto result = execute_tool(tool_call, tools, options);

      // Call the on_tool_call_finish callback if provided
      if (options && options->on_tool_call_finish.has_value()) {
        options->on_tool_call_finish.value()(result);
      }

      results.push_back(std::move(result));
    }
  } else {
    // Execute in parallel using futures, but bound concurrency so we never
    // spawn an unbounded number of threads (previously one std::async per call).
    constexpr std::size_t kMaxParallelTools = 8;
    const std::size_t n = tool_calls.size();
    for (std::size_t start = 0; start < n; start += kMaxParallelTools) {
      const std::size_t end = std::min(start + kMaxParallelTools, n);
      std::vector<std::future<ToolResult>> batch;
      batch.reserve(end - start);

      for (std::size_t i = start; i < end; ++i) {
        const auto& tool_call = tool_calls[i];
        if (options && options->abort_flag && options->abort_flag->load()) {
          ToolResult aborted_res(tool_call.id, tool_call.tool_name, tool_call.arguments,
                                 std::string("Tool execution aborted"));
          if (options && options->on_tool_call_finish.has_value()) {
            options->on_tool_call_finish.value()(aborted_res);
          }
          results.push_back(aborted_res);
          continue;
        }

        // Confirmation (mirror sequential path) - must run before execution
        if (options && options->on_tool_call_confirm.has_value()) {
          bool confirmed = options->on_tool_call_confirm.value()(tool_call);
          if (!confirmed) {
            ToolResult rejected_res(tool_call.id, tool_call.tool_name,
                                    tool_call.arguments,
                                    std::string("Tool execution rejected by user"));
            if (options && options->on_tool_call_finish.has_value()) {
              options->on_tool_call_finish.value()(rejected_res);
            }
            results.push_back(rejected_res);
            continue;
          }
        }
        // Fire on_tool_call_start callback
        if (options && options->on_tool_call_start.has_value()) {
          options->on_tool_call_start.value()(tool_call);
        }

        // Capture by VALUE to avoid dangling ref (tool_call is a loop variable)
        const std::string current_session =
            (options && !options->session_id.empty())
                ? options->session_id
                : qcode::logger::thread_session_id();
        batch.push_back(
            std::async(std::launch::async, [tool_call, &tools, options, current_session]() {
              std::optional<qcode::logger::ScopedThreadSession> bind;
              if (!current_session.empty()) {
                bind.emplace(current_session);
                qcode::logger::set_thread_name("tool:" + tool_call.tool_name);
              }
              if (options && options->abort_flag && options->abort_flag->load()) {
                ToolResult aborted_res(tool_call.id, tool_call.tool_name, tool_call.arguments,
                                       std::string("Tool execution aborted"));
                return aborted_res;
              }
              ToolResult result = execute_tool(tool_call, tools, options);
              // Fire on_tool_call_finish callback from the async thread
              if (options && options->on_tool_call_finish.has_value()) {
                options->on_tool_call_finish.value()(result);
              }
              return result;
            }));
      }

      // Collect this batch before launching the next one (bounds concurrency).
      // get() is bounded: execute_tool() supervises each tool itself — a user
      // abort releases it within 50 ms and the per-tool timeout abandons a
      // stalled tool (detached with by-value captures). Abandoning the future
      // here instead left threads referencing `tools` and `options` after
      // this function returned, and double-reported results.
      for (auto& future : batch) {
        results.push_back(future.get());
      }
    }
  }

  return results;
}

std::vector<ToolResult> ToolExecutor::execute_tools_with_options(
    const std::vector<ToolCall>& tool_calls,
    const GenerateOptions& options,
    bool parallel) {
  return execute_tools(tool_calls, options.tools, parallel, &options);
}

bool ToolExecutor::validate_tool_call(const ToolCall& tool_call,
                                      const Tool& tool) {
  // Basic validation - check if the arguments match the expected schema
  return validate_json_schema(tool_call.arguments, tool.parameters_schema);
}

bool ToolExecutor::tool_exists(const std::string& tool_name,
                               const ToolSet& tools) {
  return tools.find(tool_name) != tools.end();
}

static std::chrono::milliseconds get_tool_timeout(const ToolCall& tool_call) {
  // NOTE: this deliberately does NOT reuse the tool's own "timeout" argument.
  // BashTool::exec_run reads that same argument as its shell timeout
  // (default 120s), so sharing one number made the executor deadline and the
  // shell deadline expire at the same instant and race. The executor is the
  // outer supervision layer and must always outlive the inner tool, so it
  // applies a grace margin on top of whatever the tool asks for.
  constexpr auto kToolTimeoutGrace = std::chrono::seconds(30);
  // A subagent runs a whole multi-step turn. It is bounded by its own model
  // calls and the parent's abort, not by a per-tool deadline.
  if (tool_call.tool_name == "task") return std::chrono::milliseconds::max();
  if (tool_call.arguments.is_object() && tool_call.arguments.contains("timeout")) {
    try {
      auto t = tool_call.arguments["timeout"];
      if (t.is_number()) {
        int val = t.get<int>();
        if (val > 0) {
          return std::chrono::milliseconds(val) + kToolTimeoutGrace;
        }
      }
    } catch (...) {}
  }
  if (const char* e = std::getenv("QCODE_TOOL_TIMEOUT_MS")) {
    try { return std::chrono::milliseconds(std::stoll(e)); } catch (...) {}
  }
  return std::chrono::minutes(5);
}

ToolResult ToolExecutor::execute_sync_tool(
    const ToolCall& tool_call,
    const Tool& tool,
    const ToolExecutionContext& context,
    const GenerateOptions* options) {
  if (!tool.execute) {
    return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments,
                      std::string("Tool has no synchronous execute function"));
  }

  auto exec_func = tool.execute.value();
  auto args = tool_call.arguments;
  auto ctx = context;
  const std::string caller_session = qcode::logger::thread_session_id();

  auto future = std::async(std::launch::async, [exec_func, args, ctx, caller_session]() {
    qcode::logger::ScopedThreadSession bind(caller_session);
    return exec_func(args, ctx);
  });

  const std::chrono::milliseconds timeout = get_tool_timeout(tool_call);
  const auto start_time = std::chrono::steady_clock::now();
  bool timed_out = false;
  bool user_aborted = false;

  while (true) {
    // Check abort BEFORE future readiness: a tool that honors the abort
    // flag returns quickly, so if readiness were checked first the tool's
    // cooperative "aborted" output would be reported as a SUCCESS result
    // (observed as a flaky failure in ToolTimeoutTest.GlobalAbortInterception
    // under load). User abort must deterministically win.
    if (options && options->abort_flag && options->abort_flag->load()) {
      user_aborted = true;
      break;
    }
    if (future.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready) {
      break;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    if (elapsed >= timeout) {
      timed_out = true;
      break;
    }
  }

  if (!user_aborted && options && options->abort_flag && options->abort_flag->load()) {
    user_aborted = true;
  }

  if (user_aborted || timed_out) {
    // Only a genuine user abort may propagate to the shared abort flag. A
    // tool timeout is scoped to this single tool: flagging the shared
    // options->abort_flag here used to poison the whole session and kill
    // every parallel subagent as soon as one tool timed out.
    if (user_aborted && context.abort_flag) {
      context.abort_flag->store(true);
    }
    std::thread([f = std::move(future)]() mutable {
      try { f.get(); } catch (...) {}
    }).detach();

    if (user_aborted) {
      return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments,
                        std::string("Tool execution aborted by user"));
    } else {
      LOG_WARN("ToolExecutor: tool '{}' timed out after {} ms", tool_call.tool_name, timeout.count());
      return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments,
                        std::string("Tool execution timed out after ") +
                            std::to_string(timeout.count()) + " ms");
    }
  }

  try {
    JsonValue result = future.get();
    qcode::utils::sanitize_json_strings(result);
    return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments,
                      std::move(result));
  } catch (const std::exception& e) {
    return ToolResult(
        tool_call.id, tool_call.tool_name, tool_call.arguments,
        std::string("Tool execution failed: " + std::string(e.what())));
  }
}



ToolResult ToolExecutor::execute_async_tool(
    const ToolCall& tool_call,
    const Tool& tool,
    const ToolExecutionContext& context,
    const GenerateOptions* options) {
  if (!tool.execute_async) {
    return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments,
                      std::string("Tool has no asynchronous execute function"));
  }

  try {
    auto future = tool.execute_async.value()(tool_call.arguments, context);
    const std::chrono::milliseconds timeout = get_tool_timeout(tool_call);
    const auto start_time = std::chrono::steady_clock::now();
    bool timed_out = false;
    bool user_aborted = false;

    while (true) {
      // Same abort-before-readiness ordering as the sync path.
      if (options && options->abort_flag && options->abort_flag->load()) {
        user_aborted = true;
        break;
      }
      if (future.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready) {
        break;
      }
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start_time);
      if (elapsed >= timeout) {
        timed_out = true;
        break;
      }
    }

    if (!user_aborted && options && options->abort_flag && options->abort_flag->load()) {
      user_aborted = true;
    }

    if (user_aborted || timed_out) {
      // Same rule as the sync path: only user aborts propagate to the shared
      // abort flag; a timeout stays scoped to this tool.
      if (user_aborted && context.abort_flag) {
        context.abort_flag->store(true);
      }
      std::thread([f = std::move(future)]() mutable {
        try { f.get(); } catch (...) {}
      }).detach();

      if (user_aborted) {
        return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments,
                          std::string("Tool execution aborted by user"));
      } else {
        LOG_WARN("ToolExecutor: async tool '{}' timed out after {} ms", tool_call.tool_name, timeout.count());
        return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments,
                          std::string("Async tool execution timed out after ") +
                              std::to_string(timeout.count()) + " ms");
      }
    }

    JsonValue result = future.get();
    qcode::utils::sanitize_json_strings(result);
    return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments, std::move(result));
  } catch (const std::exception& e) {
    return ToolResult(
        tool_call.id, tool_call.tool_name, tool_call.arguments,
        std::string("Async tool execution failed: " + std::string(e.what())));
  }
}

bool ToolExecutor::validate_json_schema(const JsonValue& data,
                                        const JsonValue& schema) {
  // Basic JSON schema validation
  // This is a simplified version - a full implementation would use a proper
  // JSON schema validator

  if (!schema.contains("type")) {
    return true;  // No type constraint
  }

  std::string expected_type = schema["type"];

  if (expected_type == "object") {
    if (!data.is_object()) {
      return false;
    }

    // Check required properties
    if (schema.contains("required") && schema["required"].is_array()) {
      for (const auto& required_prop : schema["required"]) {
        if (!data.contains(required_prop.get<std::string>())) {
          return false;
        }
      }
    }

    // Validate properties (basic check)
    if (schema.contains("properties") && schema["properties"].is_object()) {
      for (const auto& [prop_name, prop_schema] :
           schema["properties"].items()) {
        if (data.contains(prop_name)) {
          if (!validate_json_schema(data[prop_name], prop_schema)) {
            return false;
          }
        }
      }
    }

    return true;
  } else if (expected_type == "string") {
    return data.is_string();
  } else if (expected_type == "number") {
    return data.is_number();
  } else if (expected_type == "integer") {
    return data.is_number_integer();
  } else if (expected_type == "boolean") {
    return data.is_boolean();
  } else if (expected_type == "array") {
    return data.is_array();
  }

  return true;  // Unknown type, accept
}

// Helper functions implementation

Tool create_simple_tool(const std::string& name,
                        const std::string& description,
                        const std::map<std::string, std::string>& parameters,
                        ToolExecuteFunction execute_func) {
  JsonValue schema = create_object_schema(parameters);
  Tool tool = create_tool(description, schema, std::move(execute_func));
  tool.name = name;
  return tool;
}

Tool create_simple_async_tool(
    const std::string& name,
    const std::string& description,
    const std::map<std::string, std::string>& parameters,
    AsyncToolExecuteFunction execute_func) {
  JsonValue schema = create_object_schema(parameters);
  Tool tool = create_async_tool(description, schema, std::move(execute_func));
  tool.name = name;
  return tool;
}

}  // namespace qcode