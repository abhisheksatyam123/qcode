#include <qcode/core/session_file_logger.h>
#include <qcode/core/tool.h>
#include <qcode/tools/tool_executor.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace qcode {
namespace {

class SessionFileLoggerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = "/tmp/qcode_test_logger_" + std::to_string(::getpid()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
    std::filesystem::create_directories(test_dir_, ec);
    qcode::logger::set_thread_session_id("");
  }

  void TearDown() override {
    qcode::logger::set_thread_session_id("");
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }

  std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
  }

  std::string test_dir_;
};

TEST_F(SessionFileLoggerTest, WritesToSeparateSessionFiles) {
  auto logger = std::make_shared<SessionFileLogger>(test_dir_, "test-server",
                                                    logger::LogLevel::kLogLevelDebug);

  // Unscoped log
  qcode::logger::set_thread_session_id("");
  logger->log(logger::LogLevel::kLogLevelInfo, "Unscoped server event",
              std::source_location::current());

  // Session A log
  qcode::logger::set_thread_session_id("sess-aaa");
  logger->log(logger::LogLevel::kLogLevelInfo, "Session A line 1",
              std::source_location::current());
  logger->log(logger::LogLevel::kLogLevelDebug, "Session A line 2",
              std::source_location::current());

  // Session B log
  qcode::logger::set_thread_session_id("sess-bbb");
  logger->log(logger::LogLevel::kLogLevelWarn, "Session B line 1",
              std::source_location::current());

  logger->close_all();

  const std::string server_path = test_dir_ + "/test-server.log";
  const std::string sess_a_path = test_dir_ + "/test-server-sess-aaa.log";
  const std::string sess_b_path = test_dir_ + "/test-server-sess-bbb.log";

  EXPECT_TRUE(std::filesystem::exists(server_path));
  EXPECT_TRUE(std::filesystem::exists(sess_a_path));
  EXPECT_TRUE(std::filesystem::exists(sess_b_path));

  std::string server_content = read_file(server_path);
  std::string sess_a_content = read_file(sess_a_path);
  std::string sess_b_content = read_file(sess_b_path);

  EXPECT_NE(server_content.find("Unscoped server event"), std::string::npos);
  EXPECT_EQ(server_content.find("Session A"), std::string::npos);
  EXPECT_EQ(server_content.find("Session B"), std::string::npos);

  EXPECT_NE(sess_a_content.find("Session A line 1"), std::string::npos);
  EXPECT_NE(sess_a_content.find("Session A line 2"), std::string::npos);
  EXPECT_EQ(sess_a_content.find("Session B"), std::string::npos);
  EXPECT_EQ(sess_a_content.find("Unscoped server event"), std::string::npos);

  EXPECT_NE(sess_b_content.find("Session B line 1"), std::string::npos);
  EXPECT_EQ(sess_b_content.find("Session A"), std::string::npos);
  EXPECT_EQ(sess_b_content.find("Unscoped server event"), std::string::npos);
}

TEST_F(SessionFileLoggerTest, ConcurrentWritesAreThreadSafeAndIsolated) {
  auto logger = std::make_shared<SessionFileLogger>(test_dir_, "test-server",
                                                    logger::LogLevel::kLogLevelDebug);

  constexpr int kNumThreads = 4;
  constexpr int kLinesPerThread = 50;
  std::vector<std::thread> threads;

  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([logger, t]() {
      const std::string sid = "worker-" + std::to_string(t);
      qcode::logger::set_thread_session_id(sid);
      for (int i = 0; i < kLinesPerThread; ++i) {
        logger->log(logger::LogLevel::kLogLevelInfo,
                    "worker " + std::to_string(t) + " iter " + std::to_string(i),
                    std::source_location::current());
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  logger->close_all();

  for (int t = 0; t < kNumThreads; ++t) {
    const std::string path = test_dir_ + "/test-server-worker-" + std::to_string(t) + ".log";
    EXPECT_TRUE(std::filesystem::exists(path));
    std::string content = read_file(path);
    EXPECT_NE(content.find("worker " + std::to_string(t) + " iter 0"), std::string::npos);
    EXPECT_NE(content.find("worker " + std::to_string(t) + " iter 49"), std::string::npos);
    // Should not contain logs from another worker
    const std::string other_worker = "worker " + std::to_string((t + 1) % kNumThreads);
    EXPECT_EQ(content.find(other_worker), std::string::npos);
  }
}

TEST_F(SessionFileLoggerTest, PruneClosesOnlyDeadSessions) {
  auto logger = std::make_shared<SessionFileLogger>(test_dir_, "test-server",
                                                    logger::LogLevel::kLogLevelDebug);

  qcode::logger::set_thread_session_id("live-1");
  logger->log(logger::LogLevel::kLogLevelInfo, "live 1", std::source_location::current());

  qcode::logger::set_thread_session_id("dead-1");
  logger->log(logger::LogLevel::kLogLevelInfo, "dead 1", std::source_location::current());

  EXPECT_TRUE(logger->has_open_sink("live-1"));
  EXPECT_TRUE(logger->has_open_sink("dead-1"));

  logger->prune([](const std::string& sid) {
    return sid == "live-1";
  });

  EXPECT_TRUE(logger->has_open_sink("live-1"));
  EXPECT_FALSE(logger->has_open_sink("dead-1"));

  // Dead session file should still exist on disk
  EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/test-server-dead-1.log"));
  EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/test-server-live-1.log"));

  logger->close_all();
}

TEST_F(SessionFileLoggerTest, IndependentRotationPerSession) {
  // Use small max_bytes = 200 bytes
  auto logger = std::make_shared<SessionFileLogger>(test_dir_, "test-server",
                                                    logger::LogLevel::kLogLevelDebug, 200);

  qcode::logger::set_thread_session_id("heavy");
  // Write several long lines to exceed 200 bytes
  for (int i = 0; i < 10; ++i) {
    logger->log(logger::LogLevel::kLogLevelInfo,
                "Heavy session line with plenty of data padding to trigger rotation: " + std::to_string(i),
                std::source_location::current());
  }

  qcode::logger::set_thread_session_id("light");
  logger->log(logger::LogLevel::kLogLevelInfo, "Light short line",
              std::source_location::current());

  logger->close_all();

  const std::string heavy_path = test_dir_ + "/test-server-heavy.log";
  const std::string heavy_old_path = test_dir_ + "/test-server-heavy.log.old";
  const std::string light_path = test_dir_ + "/test-server-light.log";
  const std::string light_old_path = test_dir_ + "/test-server-light.log.old";

  // Heavy should have rotated
  EXPECT_TRUE(std::filesystem::exists(heavy_path));
  EXPECT_TRUE(std::filesystem::exists(heavy_old_path));

  // Light should NOT have rotated
  EXPECT_TRUE(std::filesystem::exists(light_path));
  EXPECT_FALSE(std::filesystem::exists(light_old_path));
}


TEST_F(SessionFileLoggerTest, ScopedThreadSessionBindsAndRestores) {
  qcode::logger::set_thread_session_id("outer");
  {
    qcode::logger::ScopedThreadSession bind("inner");
    EXPECT_EQ(qcode::logger::thread_session_id(), "inner");
  }
  EXPECT_EQ(qcode::logger::thread_session_id(), "outer");
}

// A spawned thread starts unbound; binding it with the captured id routes its
// lines to the session file, and flush() makes them readable without closing.
TEST_F(SessionFileLoggerTest, SpawnedThreadLogsToItsSessionFile) {
  auto logger = std::make_shared<SessionFileLogger>(test_dir_, "test-server",
                                                    logger::LogLevel::kLogLevelDebug);
  qcode::logger::set_thread_session_id("sess-spawn");
  std::thread([&, sid = qcode::logger::thread_session_id()] {
    logger->log(logger::LogLevel::kLogLevelInfo, "spawned-unbound-marker",
                std::source_location::current());
    qcode::logger::ScopedThreadSession bind(sid);
    logger->log(logger::LogLevel::kLogLevelInfo, "spawned-bound-marker",
                std::source_location::current());
  }).join();
  logger->flush();

  const std::string session_log = read_file(logger->path_for("sess-spawn"));
  const std::string unscoped = read_file(logger->path_for(""));
  EXPECT_NE(session_log.find("spawned-bound-marker"), std::string::npos);
  EXPECT_EQ(session_log.find("spawned-unbound-marker"), std::string::npos);
  EXPECT_NE(unscoped.find("spawned-unbound-marker"), std::string::npos);
  EXPECT_EQ(unscoped.find("spawned-bound-marker"), std::string::npos);
}

// Tools run on executor threads; their log lines must land in the calling
// session's file, not the shared one.
TEST_F(SessionFileLoggerTest, ToolExecutorThreadsInheritTheSession) {
  auto file_logger = std::make_shared<SessionFileLogger>(
      test_dir_, "test-server", logger::LogLevel::kLogLevelDebug);
  logger::install_logger(file_logger);

  ToolSet tools;
  tools["noisy"] = Tool(
      "logs a marker", nlohmann::json::object(),
      [](const nlohmann::json&, const ToolExecutionContext&) -> nlohmann::json {
        LOG_INFO("marker-from-tool-thread");
        return "ok";
      });
  GenerateOptions options;
  options.tools = tools;
  options.session_id = "sess-tool";
  qcode::logger::set_thread_session_id("sess-tool");
  const std::vector<ToolCall> calls = {ToolCall("1", "noisy", nlohmann::json::object()),
                                       ToolCall("2", "noisy", nlohmann::json::object())};
  const auto results = ToolExecutor::execute_tools_with_options(calls, options, true);
  ASSERT_EQ(results.size(), 2u);
  const auto single = ToolExecutor::execute_tool(calls[0], tools, &options);
  EXPECT_TRUE(single.is_success());
  file_logger->flush();
  logger::install_logger(std::make_shared<logger::NullLogger>());

  const std::string session_log = read_file(file_logger->path_for("sess-tool"));
  const std::string shared_log = read_file(file_logger->path_for(""));
  EXPECT_NE(session_log.find("marker-from-tool-thread"), std::string::npos);
  EXPECT_EQ(shared_log.find("marker-from-tool-thread"), std::string::npos);
}

// A quiet tail (no further log calls, no explicit flush) must still reach
// disk: the background flusher pushes it within its interval.
TEST_F(SessionFileLoggerTest, IdleTailReachesDiskWithoutExplicitFlush) {
  auto logger = std::make_shared<SessionFileLogger>(
      test_dir_, "test-idle", logger::LogLevel::kLogLevelDebug);
  qcode::logger::set_thread_session_id("sess-idle");
  logger->log(logger::LogLevel::kLogLevelInfo, "idle tail line",
              std::source_location::current());

  const std::string path = logger->path_for("sess-idle");
  bool found = false;
  for (int i = 0; i < 50 && !found; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    found = read_file(path).find("idle tail line") != std::string::npos;
  }
  EXPECT_TRUE(found);
}

// Startup sweep: only this stem's session files older than the retention
// window are removed; fresh files, the unscoped stem file and other stems stay.
TEST_F(SessionFileLoggerTest, StartupSweepDeletesOnlyOldScopedLogs) {
  ::unsetenv("QCODE_LOG_RETENTION_DAYS");
  const auto old_time =
      std::filesystem::file_time_type::clock::now() - std::chrono::days(20);
  const std::string old_a = test_dir_ + "/test-server-old.log";
  const std::string old_b = test_dir_ + "/test-server-old.log.old";
  const std::string fresh = test_dir_ + "/test-server-new.log";
  const std::string unscoped = test_dir_ + "/test-server.log";
  const std::string other = test_dir_ + "/other-old.log";
  for (const auto& path : {old_a, old_b, fresh, unscoped, other}) {
    std::ofstream(path) << "line\n";
  }
  std::filesystem::last_write_time(old_a, old_time);
  std::filesystem::last_write_time(old_b, old_time);
  std::filesystem::last_write_time(unscoped, old_time);
  std::filesystem::last_write_time(other, old_time);

  auto logger = std::make_shared<SessionFileLogger>(
      test_dir_, "test-server", logger::LogLevel::kLogLevelDebug);

  EXPECT_FALSE(std::filesystem::exists(old_a));
  EXPECT_FALSE(std::filesystem::exists(old_b));
  EXPECT_TRUE(std::filesystem::exists(fresh));
  EXPECT_TRUE(std::filesystem::exists(unscoped));
  EXPECT_TRUE(std::filesystem::exists(other));
}

TEST_F(SessionFileLoggerTest, RetentionZeroKeepsOldLogs) {
  ::setenv("QCODE_LOG_RETENTION_DAYS", "0", 1);
  const auto old_time =
      std::filesystem::file_time_type::clock::now() - std::chrono::days(20);
  const std::string old_a = test_dir_ + "/test-server-old.log";
  const std::string old_b = test_dir_ + "/test-server-old.log.old";
  std::ofstream(old_a) << "line\n";
  std::ofstream(old_b) << "line\n";
  std::filesystem::last_write_time(old_a, old_time);
  std::filesystem::last_write_time(old_b, old_time);

  auto logger = std::make_shared<SessionFileLogger>(
      test_dir_, "test-server", logger::LogLevel::kLogLevelDebug);
  ::unsetenv("QCODE_LOG_RETENTION_DAYS");

  EXPECT_TRUE(std::filesystem::exists(old_a));
  EXPECT_TRUE(std::filesystem::exists(old_b));
}

}  // namespace

}  // namespace qcode
