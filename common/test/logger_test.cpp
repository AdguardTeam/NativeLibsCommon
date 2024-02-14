#include <gtest/gtest.h>

#include "common/logger.h"
#include "common/socket_address.h"

class FileHandler {
public:
    FileHandler(std::string_view filename)
            : m_filename(filename) {
        m_file = std::fopen(filename.data(), "w");
    }
    ~FileHandler() {
        std::fclose(m_file);
        std::remove(m_filename.data());
    }
    FILE *get_file() {
        return m_file;
    }

private:
    std::string m_filename;
    FILE *m_file;
};

TEST(Logger, Works) {
    using namespace ag;
    Logger logger("TEST_LOGGER");

    logger.log(LOG_LEVEL_INFO, FMT_STRING("{}"), "Hello, world!");
    ASSERT_EQ(ag::Logger::get_log_level(), ag::LOG_LEVEL_INFO);
    int counter;
    Logger::set_callback([&counter](LogLevel level, std::string_view message) {
        Logger::LOG_TO_STDERR(level, message);
        counter++;
    });

    counter = 0;
    Logger::set_log_level(LOG_LEVEL_INFO);
    logger.log(LOG_LEVEL_TRACE, FMT_STRING("{}"), "Hello, world!");
    ASSERT_EQ(counter, 0);
    logger.log(LOG_LEVEL_DEBUG, FMT_STRING("{}"), "Hello, world!");
    ASSERT_EQ(counter, 0);
    logger.log(LOG_LEVEL_INFO, FMT_STRING("{}"), "Hello, world!");
    logger.log(LOG_LEVEL_WARN, FMT_STRING("{}"), "Hello, world!");
    logger.log(LOG_LEVEL_ERROR, FMT_STRING("{}"), "Hello, world!");
    ASSERT_EQ(counter, 3);

    counter = 0;
    Logger::set_log_level(LOG_LEVEL_TRACE);
    logger.log(LOG_LEVEL_TRACE, FMT_STRING("{}"), "Hello, world!");
    logger.log(LOG_LEVEL_DEBUG, FMT_STRING("{}"), "Hello, world!");
    logger.log(LOG_LEVEL_INFO, FMT_STRING("{}"), "Hello, world!");
    logger.log(LOG_LEVEL_WARN, FMT_STRING("{}"), "Hello, world!");
    logger.log(LOG_LEVEL_ERROR, FMT_STRING("{}"), "Hello, world!");
    ASSERT_EQ(counter, 5);

    counter = 0;
    Logger::set_log_level(LOG_LEVEL_TRACE);
    tracelog(logger, "{}", "Hello, world!");
    dbglog(logger, "{}", "Hello, world!");
    infolog(logger, "{}", "Hello, world!");
    warnlog(logger, "{}", "Hello, world!");
    errlog(logger, "{}", "Hello, world!");
    ASSERT_EQ(counter, 5);

    Logger::set_log_level(LOG_LEVEL_DEBUG);
    FileHandler logfile{"logfile.txt"};
    Logger::LogToFile logtofile{logfile.get_file()};
    ag::Logger::set_callback(logtofile);
    dbglog(logger, "{}", "Hello, world!");
    ag::Logger::set_callback(ag::Logger::LOG_TO_STDERR);
}

TEST(Logger, SocketAddressFormatter) {
#ifdef _WIN32
    WSADATA wsa_data = {};
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data));
#endif

    ag::Logger logger("TEST_LOGGER");
    infolog(logger, "{}", ag::SocketAddress{"1.2.3.4:443"});
    infolog(logger, "{}", ag::SocketAddress{"[12:03:04:05::0067]:443"});
}
