#include "common/logger.h"
#include <gtest/gtest.h>

TEST(Logger, Works) {
    using namespace ag;
    Logger logger("TEST_LOGGER");

    logger.log(LOG_LEVEL_INFO, FMT_STRING("{}"), "Hello, world!");

    int counter;
    Logger::set_callback([&counter](LogLevel level, std::string_view message){
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
}
