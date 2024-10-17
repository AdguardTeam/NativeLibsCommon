#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

#include "common/utils.h"
#include "common/rotating_log_to_file.h"

TEST(RotatingLogToFileTest, TestLogFileCreation) {
    std::string log_file = "test_log.log";
    std::filesystem::remove(log_file);

    size_t max_file_size = 10 * 1024;  // 10 KB
    size_t max_files = 1;

    ag::RotatingLogToFile logger(log_file, max_file_size, max_files);

    logger(ag::LOG_LEVEL_INFO, "Test log entry");

    std::ifstream file(log_file);
    ASSERT_TRUE(file.is_open());
    std::string line;
    std::getline(file, line);

    EXPECT_FALSE(line.empty());
    EXPECT_NE(line.find("Test log entry"), std::string::npos);

    file.close();
}

TEST(RotatingLogToFileTest, TestFileRotation) {
    std::string log_file = "rotating_log.log";
    size_t max_file_size = 10;
    size_t max_files = 3;

    std::filesystem::remove(log_file);
    for (auto i = 1; i < max_files; i++) {
        std::filesystem::remove(AG_FMT("{}.{}", log_file, i));
    }

    ag::RotatingLogToFile logger(log_file, max_file_size, max_files);

    for (int i = 0; i < 10; ++i) {
        logger(ag::LOG_LEVEL_INFO, "Log entry " + std::to_string(i));
    }

    for (auto i = 1; i < max_files; i++) {
        ASSERT_TRUE(std::filesystem::exists(AG_FMT("{}.{}", log_file, i)));
    }
}

TEST(RotatingLogToFileTest, TestMaxFilesCount) {
    std::string log_file = "limited_log.log";
    size_t max_file_size = 50;
    size_t max_files = 3;

    std::filesystem::remove(log_file);
    for (auto i = 1; i < max_files; i++) {
        std::filesystem::remove(AG_FMT("{}.{}", log_file, i));
    }

    ag::RotatingLogToFile logger(log_file, max_file_size, max_files);

    for (int i = 0; i < 100; ++i) {
        logger(ag::LOG_LEVEL_INFO, "Log entry " + std::to_string(i));
    }

    for (auto i = 1; i < max_files; i++) {
        ASSERT_TRUE(std::filesystem::exists(AG_FMT("{}.{}", log_file, i)));
    }
    ASSERT_FALSE(std::filesystem::exists(AG_FMT("{}.{}", log_file, max_files)));
}