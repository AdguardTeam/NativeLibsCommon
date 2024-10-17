#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

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
    size_t max_file_size = 50;
    size_t max_files = 3;

    std::filesystem::remove(log_file);
    for (auto i = 1; i < 3; i++) {
        std::filesystem::remove(log_file + "." + std::to_string(i));
    }

    ag::RotatingLogToFile logger(log_file, max_file_size, max_files);

    for (int i = 0; i < 10; ++i) {
        logger(ag::LOG_LEVEL_INFO, "Log entry " + std::to_string(i));
    }

    ASSERT_TRUE(std::filesystem::exists(log_file + ".1"));
}

TEST(RotatingLogToFileTest, TestMaxFilesCount) {
    std::string log_file = "limited_log.log";
    size_t max_file_size = 50;
    size_t max_files = 3;

    std::filesystem::remove(log_file);
    std::filesystem::remove(log_file + ".1");
    std::filesystem::remove(log_file + ".2");

    ag::RotatingLogToFile logger(log_file, max_file_size, max_files);

    for (int i = 0; i < 500; ++i) {
        logger(ag::LOG_LEVEL_INFO, "Log entry " + std::to_string(i));
    }

    ASSERT_TRUE(std::filesystem::exists(log_file + ".1"));
    ASSERT_TRUE(std::filesystem::exists(log_file + ".2"));
    ASSERT_FALSE(std::filesystem::exists(log_file + ".3"));
}