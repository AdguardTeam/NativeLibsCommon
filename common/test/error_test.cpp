#include <algorithm>
#include <gtest/gtest.h>

#include "common/error.h"

using namespace ag;

enum ExchangeErrorCode {
    TIMED_OUT,
    SOCKET_ERROR,
};

template <> struct ErrorCodeToString<ExchangeErrorCode> {
    std::string operator()(ExchangeErrorCode code) {
        switch (code) {
        case TIMED_OUT:
            return "Timed out";
        case SOCKET_ERROR:
            return "Socket error";
        }
    }
};

Result<std::string, ExchangeErrorCode> do_something_good() {
    return Result<std::string, ExchangeErrorCode>("good");
}

Result<std::string, ExchangeErrorCode> do_something_bad() {
    return Result<std::string, ExchangeErrorCode>(
            make_error(SOCKET_ERROR, "Socket error occurred while doing nothing"));
}

TEST(ErrorTest, TestError) {
    auto error = make_error(ExchangeErrorCode::TIMED_OUT);
    SystemError sysErr = make_error(std::errc::timed_out);
    auto error2 = make_error(ExchangeErrorCode::TIMED_OUT, sysErr);
    std::string s = error2->str();
    std::cout << s << std::endl;
    ASSERT_TRUE(s.find(ErrorCodeToString<ExchangeErrorCode>{}(TIMED_OUT)) != s.npos);
    ASSERT_TRUE(s.find("Error at ErrorTest_TestError_Test::TestBody") != std::string::npos);
    ASSERT_TRUE(s.find("\nCaused by: Error at ErrorTest_TestError_Test::TestBody") != std::string::npos);
    auto result1 = do_something_good();
    if (!result1) {
        std::cout << result1.error()->str() << std::endl;
        FAIL();
    } else {
        std::cout << *result1 << std::endl;
    }
    auto result2 = do_something_bad();
    if (!result2) {
        std::cout << result2.error()->str() << std::endl;
    } else {
        std::cout << *result2 << std::endl;
        FAIL();
    }
    auto result3 = result2;
    auto result4 = std::move(result2);
}
