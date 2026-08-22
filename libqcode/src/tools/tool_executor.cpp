#include <algorithm>
#include <qcode/core/logger.h>
#include <chrono>
#include <future>
#include <random>
#include <thread>

#include <qcode/tools/tool_executor.h>
#include <qcode/tools/tool_factory.h>
#include <qcode/core/tool.h>
#include <qcode/core/utf8.h>

namespace qcode {

ToolResult ToolExecutor::execute_tool(const ToolCall& tool_call,
                                      const ToolSet& tools,
                                      const Messages& messages,
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
  context.messages = messages;
  if (options) {
    context.workspace = options->workspace;
  }
  if (options && options->abort_flag) {
    context.abort_flag = std::make_shared<std::atomic<bool>>(options->abort_flag->load());
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
    const Messages& messages,
    bool parallel,
    const GenerateOptions* options) {
  std::vector<ToolResult> results;
  results.reserve(tool_calls.size());

  bool effective_parallel = parallel;
  if (effective_parallel) {
    for (const auto& call : tool_calls) {
      if (call.tool_name == "bash") {
        effective_parallel = false;
        break;
      }
    }
  }

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

      auto result = execute_tool(tool_call, tools, messages, options);

      // Call the on_tool_call_finish callback if provided
      if (options && options->on_tool_call_finish.has_value()) {
        options->on_tool_call_finish.value()(result);
      }

      results.push_back(result);
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
        batch.emplace_back(
            std::async(std::launch::async, [tool_call, &tools, &messages, options]() {
              if (options && options->abort_flag && options->abort_flag->load()) {
                ToolResult aborted_res(tool_call.id, tool_call.tool_name, tool_call.arguments,
                                       std::string("Tool execution aborted"));
                return aborted_res;
              }
              ToolResult result = execute_tool(tool_call, tools, messages, options);
              // Fire on_tool_call_finish callback from the async thread
              if (options && options->on_tool_call_finish.has_value()) {
                options->on_tool_call_finish.value()(result);
              }
              return result;
            }));
      }

      // Collect this batch before launching the next one (bounds concurrency).
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
  return execute_tools(tool_calls, options.tools, options.messages, parallel,
                       &options);
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
  if (tool_call.arguments.is_object() && tool_call.arguments.contains("timeout")) {
    try {
      auto t = tool_call.arguments["timeout"];
      if (t.is_number()) {
        int val = t.get<int>();
        if (val > 0) {
          return std::chrono::milliseconds(val);
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

  auto future = std::async(std::launch::async, [exec_func, args, ctx]() {
    return exec_func(args, ctx);
  });

  const std::chrono::milliseconds timeout = get_tool_timeout(tool_call);
  const auto start_time = std::chrono::steady_clock::now();
  bool timed_out = false;
  bool user_aborted = false;

  while (true) {
    if (future.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready) {
      break;
    }
    if (options && options->abort_flag && options->abort_flag->load()) {
      user_aborted = true;
      break;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    if (elapsed >= timeout) {
      timed_out = true;
      break;
    }
  }

  if (user_aborted || timed_out) {
    if (context.abort_flag) {
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
                      result);
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
      if (future.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready) {
        break;
      }
      if (options && options->abort_flag && options->abort_flag->load()) {
        user_aborted = true;
        break;
      }
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start_time);
      if (elapsed >= timeout) {
        timed_out = true;
        break;
      }
    }

    if (user_aborted || timed_out) {
      if (context.abort_flag) {
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
    return ToolResult(tool_call.id, tool_call.tool_name, tool_call.arguments, result);
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

ToolSet create_tool_set(
    const std::vector<std::pair<std::string, Tool>>& tool_list) {
  ToolSet tools;
  for (const auto& [name, tool] : tool_list) {
    tools[name] = tool;
  }
  return tools;
}

ToolCall create_tool_call(const std::string& tool_name,
                          const JsonValue& arguments,
                          const std::string& call_id) {
  std::string id = call_id.empty() ? generate_tool_call_id() : call_id;
  return ToolCall(id, tool_name, arguments);
}

std::string generate_tool_call_id() {
  // Generate a unique ID for tool calls
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 15);

  std::string id = "call_";
  for (int i = 0; i < 24; ++i) {
    int val = dis(gen);
    if (val < 10) {
      id += static_cast<char>('0' + val);
    } else {
      id += static_cast<char>('a' + val - 10);
    }
  }

  return id;
}

}  // namespace qcode