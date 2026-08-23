// Coverage for reliability work: per-session agent/reasoning mode
// persistence (migration v7) and FileLogger size-cap rotation.

#include <qcode/session/session_store.h>
#include <qcode/core/file_logger.h>
#include <qcode/core/logger.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace qcode {
namespace {

using namespace qcode::session;

class SessionModesStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        std::filesystem::create_directories("/tmp/qcode_modes_test", ec);
        db_path_ = "/tmp/qcode_modes_test/test.db";
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(db_path_ + "-wal", ec);
        std::filesystem::remove(db_path_ + "-shm", ec);
        setenv("QCODE_DB_PATH", db_path_.c_str(), 1);
        init_database();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(db_path_ + "-wal", ec);
        std::filesystem::remove(db_path_ + "-shm", ec);
        unsetenv("QCODE_DB_PATH");
    }

    std::string db_path_;
};

TEST_F(SessionModesStoreTest, RoundTripBothModes) {
    const std::string sid = create_new_session("prov", "model", "ws");
    set_session_modes(sid, "plan", "high");
    const auto [agent, reasoning] = get_session_modes(sid);
    EXPECT_EQ(agent, "plan");
    EXPECT_EQ(reasoning, "high");
}

TEST_F(SessionModesStoreTest, UnsetModesReturnEmpty) {
    const std::string sid = create_new_session("prov", "model", "ws");
    const auto [agent, reasoning] = get_session_modes(sid);
    EXPECT_TRUE(agent.empty());
    EXPECT_TRUE(reasoning.empty());
}

TEST_F(SessionModesStoreTest, InvalidSessionIdIsNoop) {
    set_session_modes("", "plan", "high");
    set_session_modes("does-not-exist", "plan", "high");
    SUCCEED();
}

class FileLoggerRotationTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = "/tmp/qcode_rotation_test/app.log";
        std::error_code ec;
        std::filesystem::create_directories("/tmp/qcode_rotation_test", ec);
        std::filesystem::remove(path_, ec);
        std::filesystem::remove(path_ + ".old", ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        std::filesystem::remove(path_ + ".old", ec);
    }

    static std::uintmax_t size_or_zero(const std::string& p) {
        std::error_code ec;
        return std::filesystem::exists(p, ec) ? std::filesystem::file_size(p, ec)
                                              : 0;
    }

    std::string path_;
};

TEST_F(FileLoggerRotationTest, RotatesWhenCapExceeded) {
    {
        FileLogger logger(path_, logger::LogLevel::kLogLevelInfo,
                          /*max_bytes=*/2048);
        const std::string big(200, 'x');
        for (int i = 0; i < 20; ++i) {
            logger.log(logger::LogLevel::kLogLevelInfo, big,
                       std::source_location::current());
        }
    }
    // Active file stays under the cap (header overhead included); the
    // rotated copy holds earlier lines. Single-slot .old keeps the most
    // recent rotated segment only.
    EXPECT_LE(size_or_zero(path_), 2560u);
    EXPECT_GT(size_or_zero(path_ + ".old"), 1024u);
    EXPECT_GE(size_or_zero(path_) + size_or_zero(path_ + ".old"), 3000u);

    // The rotated file is complete lines only (ends with newline).
    std::ifstream old_file(path_ + ".old");
    old_file.seekg(-1, std::ios::end);
    const char last = static_cast<char>(old_file.get());
    EXPECT_EQ(last, '\n');
}

TEST_F(FileLoggerRotationTest, ReplacesPreviousRotatedFile) {
    // Payload sentinels (not single letters): the line header embeds the
    // source file name, so ordinary letters appear in every record.
    std::string first_payload;
    for (int i = 0; i < 25; ++i) first_payload += "FIRST";
    {
        FileLogger logger(path_, logger::LogLevel::kLogLevelInfo, 512);
        for (int i = 0; i < 10; ++i)
            logger.log(logger::LogLevel::kLogLevelInfo, first_payload,
                       std::source_location::current());
    }  // rotated at least once
    std::error_code ec;
    std::filesystem::remove(path_, ec);  // fresh start for run two
    std::cout << "[dbg] removed active, exists="
              << (std::filesystem::exists(path_) ? "Y" : "N")
              << " old_size=" << size_or_zero(path_ + ".old") << "\n";
    std::filesystem::remove(path_, ec);  // fresh start for run two
    {
        FileLogger logger(path_, logger::LogLevel::kLogLevelInfo, 512);
        std::string second_payload;
        for (int i = 0; i < 25; ++i) second_payload += "SECOND";
        for (int i = 0; i < 10; ++i)
            logger.log(logger::LogLevel::kLogLevelInfo, second_payload,
                       std::source_location::current());
    }  // rotated again — .old must be replaced, not appended
    std::ifstream old_file(path_ + ".old");
    const std::string content((std::istreambuf_iterator<char>(old_file)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("SECOND"), std::string::npos);
    EXPECT_EQ(content.find("FIRST"), std::string::npos);
}

}  // namespace
}  // namespace qcode
