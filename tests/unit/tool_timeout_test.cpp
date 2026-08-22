#include <qcode/tools/tool_executor.h>
#include <qcode/core/tool.h>
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <qcode/core/logger.h>

namespace qcode {
namespace test {

TEST(ToolTimeoutTest, SyncToolTimeout) {
  ToolSet tools;
  tools["sleep_tool"] = Tool(
      "A tool that sleeps",
      nlohmann::json::object(),
      [](const nlohmann::json& args, const ToolExecutionContext& ctx) -> nlohmann::json {
        (void)args;
        for (int i = 0; i < 40; ++i) {
          if (ctx.abort_flag && ctx.abort_flag->load()) {
            return "aborted";
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return "success";
      }
  );

  nlohmann::json args = nlohmann::json::object();
  args["timeout"] = 100;
  ToolCall call("1", "sleep_tool", args);
  
  auto start = std::chrono::steady_clock::now();
  auto result = ToolExecutor::execute_tool(call, tools);
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  EXPECT_TRUE(result.error.has_value());
  EXPECT_NE(result.error.value().find("timed out"), std::string::npos);
  EXPECT_LT(duration.count(), 1000);
}

TEST(ToolTimeoutTest, GlobalAbortInterception) {
  ToolSet tools;
  tools["sleep_tool"] = Tool(
      "A tool that sleeps",
      nlohmann::json::object(),
      [](const nlohmann::json& args, const ToolExecutionContext& ctx) -> nlohmann::json {
        (void)args;
        for (int i = 0; i < 40; ++i) {
          if (ctx.abort_flag && ctx.abort_flag->load()) {
            return "aborted";
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return "success";
      }
  );

  ToolCall call("1", "sleep_tool", nlohmann::json::object());
  GenerateOptions options;
  options.abort_flag = std::make_shared<std::atomic<bool>>(false);
  
  std::thread abort_trigger([&options]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    options.abort_flag->store(true);
  });

  auto start = std::chrono::steady_clock::now();
  auto result = ToolExecutor::execute_tool(call, tools, {}, &options);
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  abort_trigger.join();

  EXPECT_TRUE(result.error.has_value());
  EXPECT_NE(result.error.value().find("aborted"), std::string::npos);
  EXPECT_LT(duration.count(), 1000);
}

}
}
