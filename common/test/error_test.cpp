#include <algorithm>
#include <gtest/gtest.h>

#include "common/error.h"
#include "common/utils.h"

using namespace ag; // NOLINT(*-build-using-namespace)

enum ExchangeErrorCode {
    TIMED_OUT,
    SOCKET_ERROR,
};

// clang-format off
template <>
struct ag::ErrorCodeToString<ExchangeErrorCode> {
    std::string operator()(ExchangeErrorCode code) {
        switch (code) {
        case TIMED_OUT: return "Timed out";
        case SOCKET_ERROR: return "Socket error";
        }
    }
};
// clang-format on

Result<std::string, ExchangeErrorCode> do_something_good() {
    return {"good"};
}

Result<std::string, ExchangeErrorCode> do_something_bad() {
    return Result<std::string, ExchangeErrorCode>(
            make_error(SOCKET_ERROR, "Socket error occurred while doing nothing"));
}

TEST(ErrorTest, TestError) {
    auto error = make_error(ExchangeErrorCode::TIMED_OUT);
    SystemError sys_err = make_error(std::errc::timed_out);
    auto error2 = make_error(ExchangeErrorCode::TIMED_OUT, sys_err);
    std::string s = error2->str();
    ASSERT_NE(s.find(ErrorCodeToString<ExchangeErrorCode>{}(TIMED_OUT)), s.npos) << s;
    ASSERT_NE(s.find("Error at ErrorTest_TestError_Test::TestBody"), std::string::npos) << s;
    ASSERT_NE(s.find("\nCaused by: Error at ErrorTest_TestError_Test::TestBody"), std::string::npos) << s;

    auto result1 = do_something_good();
    ASSERT_TRUE(result1.has_value()) << result1.error()->str();
    ASSERT_EQ(*result1, "good");

    auto result2 = do_something_bad();
    ASSERT_TRUE(result2.has_error()) << *result2;
    ASSERT_TRUE(utils::ends_with(result2.error()->str(), "Socket error: Socket error occurred while doing nothing"))
            << result2.error()->str();

    auto result3 = result2;
    auto result4 = std::move(result2);
}

enum TestErrorEmptyString {};

template <>
struct ag::ErrorCodeToString<TestErrorEmptyString> {
    std::string operator()(TestErrorEmptyString) {
        return {};
    }
};

TEST(ErrorTest, EmptyErrorStringRepresentation) {
    constexpr std::string_view MESSAGE = "haha";
    // clang-format off
    auto error = make_error(TestErrorEmptyString{}, MESSAGE); int line = __LINE__;
    // clang-format on
    ASSERT_EQ(AG_FMT("Error at ErrorTest_EmptyErrorStringRepresentation_Test::{}:{}: {}", __func__, line, MESSAGE),
            error->str());
}

TEST(ErrorTest, EmptyDescription) {
    // clang-format off
    auto error = make_error(ExchangeErrorCode::TIMED_OUT); int line = __LINE__;
    // clang-format on
    ASSERT_EQ(AG_FMT("Error at ErrorTest_EmptyDescription_Test::{}:{}: Timed out", __func__, line), error->str());
}
