#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

#include "common/utils.h"
#include "common/rotating_log_to_file.h"

class RotatingLogToFileTest : public ::testing::Test {
protected:
    std::string m_log_file;
    size_t m_max_files;

    void SetUp() override {
        m_log_file = "test_log.log";
        m_max_files = 10;
    }

    void TearDown() override {
        std::filesystem::remove(m_log_file);
        for (auto i = 1; i < m_max_files; i++) {
            std::filesystem::remove(AG_FMT("{}.{}", m_log_file, i));
        }
    }

    std::string read_file(const std::string &file_name) {
        std::ifstream file(file_name);
        if (!file.is_open()) {
            return "";
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                (std::istreambuf_iterator<char>()));
        return content;
    }
};

TEST_F(RotatingLogToFileTest, TestLogFileCreation) {
    m_max_files = 1;
    size_t max_file_size = 10 * 1024;  // 10 KB

    ag::RotatingLogToFile logger(m_log_file, max_file_size, m_max_files);

    logger(ag::LOG_LEVEL_INFO, "Test log entry");

    std::ifstream file(m_log_file);
    ASSERT_TRUE(file.is_open());
    std::string line;
    std::getline(file, line);

    EXPECT_FALSE(line.empty());
    EXPECT_NE(line.find("Test log entry"), std::string::npos);

    file.close();
}

TEST_F(RotatingLogToFileTest, TestFileRotation) {
    m_max_files = 3;
    size_t max_file_size = 10;

    ag::RotatingLogToFile logger(m_log_file, max_file_size, m_max_files);

    for (int i = 0; i < 10; ++i) {
        logger(ag::LOG_LEVEL_INFO, AG_FMT("Log entry {}", i));
    }

    for (auto i = 1; i < m_max_files; i++) {
        ASSERT_TRUE(std::filesystem::exists(AG_FMT("{}.{}", m_log_file, i)));
    }
}

TEST_F(RotatingLogToFileTest, TestMaxFilesCount) {
    m_max_files = 3;
    size_t max_file_size = 50;

    ag::RotatingLogToFile logger(m_log_file, max_file_size, m_max_files);

    for (int i = 0; i < 100; ++i) {
        logger(ag::LOG_LEVEL_INFO, AG_FMT("Log entry {}", i));
    }

    for (auto i = 1; i < m_max_files; i++) {
        ASSERT_TRUE(std::filesystem::exists(AG_FMT("{}.{}", m_log_file, i)));
    }
    ASSERT_FALSE(std::filesystem::exists(AG_FMT("{}.{}", m_log_file, m_max_files)));
}

TEST_F(RotatingLogToFileTest, TestRename) {
    m_max_files = 3;
    std::string test_string = "Log entry ";
    size_t max_file_size = test_string.size() + 1;

    ag::RotatingLogToFile logger(m_log_file, max_file_size, m_max_files);

    logger(ag::LOG_LEVEL_INFO, AG_FMT("{}{}", test_string, 1));
    std::string content = read_file(m_log_file);
    ASSERT_TRUE(content.find(AG_FMT("{}{}", test_string, 1)) != std::string::npos);

    logger(ag::LOG_LEVEL_INFO, AG_FMT("{}{}", test_string, 2));
    content = read_file(m_log_file);
    ASSERT_TRUE(content.find(AG_FMT("{}{}", test_string, 2)) != std::string::npos);
    ASSERT_TRUE(std::filesystem::exists(AG_FMT("{}.{}", m_log_file, 1)));
    content = read_file(AG_FMT("{}.{}", m_log_file, 1));
    ASSERT_TRUE(content.find(AG_FMT("{}{}", test_string, 1)) != std::string::npos);

    for (auto i = 3; i < 10; i++) {
        logger(ag::LOG_LEVEL_INFO, AG_FMT("{}{}", test_string, i));
        content = read_file(m_log_file);
        ASSERT_TRUE(content.find(AG_FMT("{}{}", test_string, i)) != std::string::npos);
        ASSERT_TRUE(std::filesystem::exists(AG_FMT("{}.{}", m_log_file, 1)));
        content = read_file(AG_FMT("{}.{}", m_log_file, 1));
        ASSERT_TRUE(content.find(AG_FMT("{}{}", test_string, i - 1)) != std::string::npos);
        ASSERT_TRUE(std::filesystem::exists(AG_FMT("{}.{}", m_log_file, 2)));
        content = read_file(AG_FMT("{}.{}", m_log_file, 2));
        ASSERT_TRUE(content.find(AG_FMT("{}{}", test_string, i - 2)) != std::string::npos);
    }
}
