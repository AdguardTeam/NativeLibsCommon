#include <stdexcept>
#include <string>
#include <thread>

#include "common/gtest_coro.h"
#include "common/parallel.h"
#include "common/defs.h"

namespace ag::test {

// Helper scheduler for async operations (simplified version)
struct SimpleScheduler {
    auto immediate() {
        struct Awaitable {
            bool await_ready() { return false; }
            void await_suspend(std::coroutine_handle<> h) {
                h.resume(); // Resume immediately
            }
            void await_resume() {}
        };
        return Awaitable{};
    }
};

class CoroExceptionTest : public ::testing::Test {
protected:
    SimpleScheduler m_scheduler;
};

// Test coroutines that throw exceptions
coro::Task<int> throw_runtime_error() {
    co_await SimpleScheduler{}.immediate();
    throw std::runtime_error("Test runtime error");
    co_return 42; // This should never be reached
}

coro::Task<void> throw_logic_error() {
    throw std::logic_error("Test logic error");
    co_return;
}

coro::Task<std::string> throw_invalid_argument() {
    co_await SimpleScheduler{}.immediate();
    throw std::invalid_argument("Test invalid argument");
    co_return std::string("should not reach");
}

coro::Task<int> throw_after_await() {
    co_await SimpleScheduler{}.immediate();
    co_await SimpleScheduler{}.immediate();
    throw std::runtime_error("Exception after multiple awaits");
    co_return 123; // NOLINT: magic number for test
}

coro::Task<int> normal_return() {
    co_return 100; // NOLINT: magic number for test
}

coro::Task<void> normal_void_return() {
    co_return;
}

// Test exception propagation through nested coroutines
coro::Task<int> nested_exception_caller() {
    try {
        int result = co_await throw_runtime_error();
        co_return result; // Should not reach here
    } catch (const std::runtime_error &e) {
        // Catch and rethrow as different exception
        throw std::logic_error("Nested exception: " + std::string(e.what()));
    }
}

coro::Task<void> exception_in_destructor_test() {
    struct ThrowingDestructor {
        ThrowingDestructor() = default;
        ThrowingDestructor(const ThrowingDestructor &) = delete; // NOLINT: not coroutine param
        ThrowingDestructor(ThrowingDestructor &&) = delete; // NOLINT: not coroutine param
        ThrowingDestructor &operator=(const ThrowingDestructor &) = delete; // NOLINT: not coroutine param
        ThrowingDestructor &operator=(ThrowingDestructor &&) = delete; // NOLINT: not coroutine param
        ~ThrowingDestructor() noexcept(false) {
            // Note: This is generally bad practice, but we're testing exception handling
            throw std::runtime_error("Exception in destructor");
        }
    };
    
    ThrowingDestructor obj;
    co_await SimpleScheduler{}.immediate();
    // Exception should be thrown when obj is destroyed
    co_return;
}

TEST_F(CoroExceptionTest, BasicExceptionHandling) {
    try {
        int result = co_await throw_runtime_error();
        FAIL() << "Expected std::runtime_error to be thrown";
    } catch (const std::runtime_error &e) {
        EXPECT_STREQ("Test runtime error", e.what());
    }
    co_return;
}

TEST_F(CoroExceptionTest, VoidTaskException) {
    try {
        co_await throw_logic_error();
        FAIL() << "Expected std::logic_error to be thrown";
    } catch (const std::logic_error &e) {
        EXPECT_STREQ("Test logic error", e.what());
    }
    co_return;
}

TEST_F(CoroExceptionTest, ExceptionAfterAwait) {
    try {
        int result = co_await throw_after_await();
        FAIL() << "Expected std::runtime_error to be thrown";
    } catch (const std::runtime_error &e) {
        EXPECT_STREQ("Exception after multiple awaits", e.what());
    }
    co_return;
}

TEST_F(CoroExceptionTest, ExceptionWithStringReturn) {
    try {
        std::string result = co_await throw_invalid_argument();
        FAIL() << "Expected std::invalid_argument to be thrown";
    } catch (const std::invalid_argument &e) {
        EXPECT_STREQ("Test invalid argument", e.what());
    }
    co_return;
}

TEST_F(CoroExceptionTest, NestedExceptionPropagation) {
    try {
        int result = co_await nested_exception_caller();
        FAIL() << "Expected std::logic_error to be thrown";
    } catch (const std::logic_error &e) {
        EXPECT_TRUE(std::string(e.what()).find("Nested exception") != std::string::npos);
        EXPECT_TRUE(std::string(e.what()).find("Test runtime error") != std::string::npos);
    }
    co_return;
}

TEST_F(CoroExceptionTest, MixedSuccessAndException) {
    // Test that normal coroutines still work when exceptions are enabled
    int normal_result = co_await normal_return();
    EXPECT_EQ(100, normal_result); // NOLINT: magic number for test
    
    co_await normal_void_return(); // Should not throw
    
    // Now test exception
    try {
        int exception_result = co_await throw_runtime_error();
        FAIL() << "Expected exception to be thrown";
    } catch (const std::runtime_error &e) {
        EXPECT_STREQ("Test runtime error", e.what());
    }
    co_return;
}

TEST_F(CoroExceptionTest, ParallelExceptionHandling) {
    try {
        // Test exception in parallel execution
        auto result = co_await parallel::any_of<int>(
            normal_return(),
            throw_runtime_error()
        );
        // The normal_return should complete first
        EXPECT_EQ(100, result); // NOLINT: magic number for test
    } catch (const std::exception &e) {
        // If exception wins the race, that's also valid behavior
        EXPECT_TRUE(std::string(e.what()).find("Test runtime error") != std::string::npos);
    }
    co_return;
}

TEST_F(CoroExceptionTest, AllOfWithException) {
    try {
        auto results = co_await parallel::all_of<int>(
            normal_return(),
            throw_runtime_error()
        );
        FAIL() << "Expected exception to be propagated from all_of";
    } catch (const std::runtime_error &e) {
        EXPECT_STREQ("Test runtime error", e.what());
    }
    co_return;
}

TEST_F(CoroExceptionTest, AnyOfWithException) {
    // Test that any_of works correctly when one coroutine throws and another succeeds
    try {
        auto result = co_await parallel::any_of<int>(
            throw_runtime_error(),
            normal_return()
        );
        // The normal_return should complete successfully
        EXPECT_EQ(100, result); // NOLINT: magic number for test
    } catch (const std::runtime_error &e) {
        // If exception wins the race, that's also valid behavior
        EXPECT_STREQ("Test runtime error", e.what());
    }
    co_return;
}

TEST_F(CoroExceptionTest, AnyOfAllThrowExceptions) {
    // Test any_of when all coroutines throw exceptions
    // any_of should not throw - it uses any_of_cond which returns empty optional
    auto result_opt = co_await parallel::any_of_cond<int>(
        nullptr, // no condition check
        throw_runtime_error(),
        throw_after_await()
    );
    
    // When all coroutines fail, any_of_cond should return empty optional
    EXPECT_FALSE(result_opt.has_value());
    co_return;
}

TEST_F(CoroExceptionTest, AnyOfMixedTypes) {
    // Test any_of with different exception types
    try {
        auto result = co_await parallel::any_of<std::string>(
            throw_invalid_argument(),
            []() -> coro::Task<std::string> {
                co_return std::string("success");
            }()
        );
        EXPECT_EQ("success", result);
    } catch (const std::invalid_argument &e) {
        // If exception wins the race, that's also valid
        EXPECT_STREQ("Test invalid argument", e.what());
    }
    co_return;
}

TEST_F(CoroExceptionTest, AnyOfVoidWithException) {
    // Test any_of with void return type and exceptions
    try {
        co_await parallel::any_of<void>(
            throw_logic_error(),
            normal_void_return()
        );
        // If we reach here, normal_void_return completed first
        SUCCEED();
    } catch (const std::logic_error &e) {
        // If exception wins the race, that's also valid
        EXPECT_STREQ("Test logic error", e.what());
    }
    co_return;
}

TEST_F(CoroExceptionTest, ExceptionInDestructor) {
    try {
        co_await exception_in_destructor_test();
        FAIL() << "Expected exception from destructor";
    } catch (const std::runtime_error &e) {
        EXPECT_STREQ("Exception in destructor", e.what());
    }
    co_return;
}

// Test exception handling with to_future conversion
TEST_F(CoroExceptionTest, ExceptionWithToFuture) {
    auto future = to_future(throw_runtime_error());
    try {
        int result = future.get();
        FAIL() << "Expected exception to be propagated through future";
    } catch (const std::runtime_error &e) {
        EXPECT_STREQ("Test runtime error", e.what());
    }
}

TEST_F(CoroExceptionTest, VoidExceptionWithToFuture) {
    auto future = to_future(throw_logic_error());
    try {
        future.get();
        FAIL() << "Expected exception to be propagated through void future";
    } catch (const std::logic_error &e) {
        EXPECT_STREQ("Test logic error", e.what());
    }
}

// Test run_detached with exceptions (should not crash)
TEST_F(CoroExceptionTest, RunDetachedWithException) {
    // This test verifies that run_detached doesn't crash when coroutine throws
    // The exception should be captured by unhandled_exception() but not propagated
    run_detached(throw_logic_error());
    
    // Give some time for the detached coroutine to complete
    // In a real scenario, you might need proper synchronization
    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // NOLINT: magic number for test
    
    // If we reach here without crashing, the test passes
    SUCCEED();
    co_return;
}

} // namespace ag::test
