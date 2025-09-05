#include <array>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <numeric>

#include "common/move_only_function.h"

TEST(MoveOnlyFunction, BasicFunctionality) {
    ag::MoveOnlyFunction<int(int, int)> func{[](int a, int b) { return a + b; }};
    ASSERT_TRUE(func);
    ASSERT_EQ(7, func(3, 4));
}

TEST(MoveOnlyFunction, VoidReturnType) {
    int result = 0;
    ag::MoveOnlyFunction<void(int)> func{[&result](int x) { result = x * 2; }};
    ASSERT_TRUE(func);
    func(5);
    ASSERT_EQ(10, result);
}

TEST(MoveOnlyFunction, EmptyFunction) {
    ag::MoveOnlyFunction<int()> func;
    ASSERT_FALSE(func);
#ifdef __cpp_exceptions
    ASSERT_THROW(func(), std::bad_function_call);
#endif
}

TEST(MoveOnlyFunction, MoveSemantics) {
    ag::MoveOnlyFunction<int(int)> func1{[](int x) { return x * 2; }};
    ASSERT_TRUE(func1);
    ASSERT_EQ(10, func1(5));

    ag::MoveOnlyFunction<int(int)> func2 = std::move(func1);
    ASSERT_TRUE(func2);
    ASSERT_EQ(10, func2(5));
    // func1 should be empty after std::move
    ASSERT_FALSE(func1);
}

TEST(MoveOnlyFunction, LambdaWithUniquePtrCaptures) {
    auto ptr1 = std::make_unique<int>(10);
    auto ptr2 = std::make_unique<int>(20);

    ag::MoveOnlyFunction<int()> func = [p1 = std::move(ptr1), p2 = std::move(ptr2)]() -> int {
        return *p1 + *p2;
    };

    ASSERT_TRUE(func);
    ASSERT_EQ(30, func());
    ASSERT_EQ(nullptr, ptr1);
    ASSERT_EQ(nullptr, ptr2);
}

TEST(MoveOnlyFunction, MovingLargeLambda) {
    // Create a lambda with large capture that exceeds SBO buffer size
    // This should force heap allocation
    constexpr size_t LARGE_ARRAY_SIZE = 100;
    struct LargeCapture {
        std::array<int, LARGE_ARRAY_SIZE> data{};
        std::unique_ptr<int> ptr;

        LargeCapture() : ptr(std::make_unique<int>(42)) {
            std::iota(data.begin(), data.end(), 1);
        }
    };

    auto large_capture = LargeCapture{};
    int expected_sum = std::accumulate(large_capture.data.begin(), large_capture.data.end(), 0);
    expected_sum += *large_capture.ptr;

    ag::MoveOnlyFunction<int()> func1 = [capture = std::move(large_capture)]() -> int {
        int sum = std::accumulate(capture.data.begin(), capture.data.end(), 0);
        return sum + *capture.ptr;
    };

    ASSERT_TRUE(func1);
    ASSERT_EQ(expected_sum, func1());

    // Test moving the large lambda
    ag::MoveOnlyFunction<int()> func2 = std::move(func1);
    ASSERT_TRUE(func2);
    ASSERT_FALSE(func1);  // func1 should be empty after move
    ASSERT_EQ(expected_sum, func2());

    // Test move assignment with large lambda
    ag::MoveOnlyFunction<int()> func3;
    func3 = std::move(func2);
    ASSERT_TRUE(func3);
    ASSERT_FALSE(func2);  // func2 should be empty after move
    ASSERT_EQ(expected_sum, func3());
}
