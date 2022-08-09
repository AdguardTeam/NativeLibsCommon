#pragma once

#include <optional>
#include <future>
#include <version>

#if defined _LIBCPP_VERSION && _LIBCPP_VERSION < 14000

#include <experimental/coroutine>

namespace std {

template <typename Promise = void>
using coroutine_handle = std::experimental::coroutine_handle<Promise>;

using suspend_always = std::experimental::suspend_always;

using suspend_never = std::experimental::suspend_never;

} // namespace std

#else

#include <coroutine>

#endif

namespace ag::coro {

/**
 * This class implements interface to coroutine that can be awaitable.
 *
 * It is always in suspended state after creation.
 * operator co_await() provides Awaitable for saving caller and resuming coroutine.
 * If you want to "resume and forget" this task without caller, use `coro::run_detached(Task &&task)`.
 * If you want to convert this task into std::future, use `coro::to_future(Task &&task)`.
 *
 * Some theory on how coroutines work.
 * Coroutine is a function that contains one of keywords `co_await`, `co_return` or `co_yield`.
 * Here is some coroutine:
 * ```
 * ReturnType test_coro(int param) {
 *     // coroutine_body
 *     co_return param;
 * }
 * Compiler rewrites coroutine body, then keywords are recursively rewritten too.
 * At first, coroutine body is rewritten as:
 * ```
 * ReturnType test_coro(int param) {
 *     // Allocated on real stack frame:
 *     ReturnType return_object;
 *     {
 *         // create coroutine frame
 *         // copy args to coroutine frame
 *         // call jump to coroutine frame, thread stack frame becomes parent stack frame
 *         ReturnType::promise_type promise;
 *         // Initialize return object on parent stack frame
 *         return_object = p.get_return_object();
 *
 *         co_await promise.initial_suspend();                       // suspension point 1
 *         try {
 *             // here placed body of original test_coro()
 *             co_return param;
 *         catch (...) {
 *             promise.unhandled_exception();
 *         }
 *         co_await promise.final_suspend();                         // suspension point 2
 *         // RAII destruction of promise, args and coroutine frame
 *     }
 * }
 * ```
 *
 * 1. expression `co_await some_expression()` is rewritten as:
 * ```
 *     auto temporary = some_expression();
 *     auto awaitable = temporary.operator co_await();
 *     if (!awaitable.await_ready()) {
 *         // a. Increase program counter and jump out coroutine frame
 *         // b. Pass suspending coroutine to awaitable:
 *         auto another_coroutine = awaitable.await_suspend(coroutine);
 *         if (another_coroutine != nullptr) {
 *             another_coroutine.resume();
 *         };
 *         // c. Return from current function call in the terms of thread stack
 *         // Note that this `return` just returns from current function call
 *         // It may be "ReturnType test_coro(int)" and "void `ReturnType test_coro(int)`::.resume()"
 *         return;
 *     }
 *     // Result is:
 *     awaitable.await_resume()
 * ```
 *
 * 2. statement `co_return some_value` is rewritten as:
 * ```
 *     promise.return_value(some_value); // or promise.return_void()
 *     // jump out remaining coro body
 * ```
 * ReturnType itself is usually a class that helps control coroutine.
 * In our case it is `Task` class.
 *
 * Concrete example with this task class:
 * Let's see the following code:
 * ```
 * 1: Task<int> coro2() {
 * 2:     co_return 42;
 * 3: }
 * ...
 * 10: // Inside some calling coroutine...
 * 11: int x = co_await coro2();
 * ```
 *
 * Line 10: Starting to evaluate `co_await coro2()`. First, coro2() is called. Go to line 1.
 * Line 1: Coroutine frame is created, `Task::Promise` is created on coroutine stack.
 *         `Promise::initial_suspend()` returns `std::suspend_always`. So, `coro2()` as coroutine is suspended.
 *         `coro2()` as function returns `Promise::get_return_object()`, where Task object is created.
 *         Back to line 10.
 * Line 10: `co_await` of returned object(`Task`) is called. Compiler checks if returned object is `Awaitable`.
 *          Since it is not, `Task::operator co_await()` is applied.
 *          It returns `Awaitable`, which returns `Awaitable::await_ready()` = `false`.
 *          The last means that current coroutine should be suspended and passed to `Awaitable::await_suspend(coroutine_handle)`.
 *          Inside that function, caller is saved inside `Task::Promise` of `coro2()`. Then `coro2()` is resumed.
 *          Go to line 2.
 * Line 2: We see co_return 42. It means that `Promise::return_value(42)` is called. It stores return value.
 *         Then `Promise::final_suspend()` is called. It transfers execution to caller, saved on previous line.
 *         Back to line 10.
 * Line 10: `Awaitable::await_resume()` is called, and it is result of whole `co_await`. Inside `await_resume`,
 *         `final_suspend`ed coroutine is destroyed, and saved value returned to caller.
 * Expression is finally evaluated.
 *
 * @tparam Ret return type used in `co_return` inside coroutine.
 */
template<typename Ret>
struct Task {
    struct Promise;
    using promise_type = Promise; //< NOLINT: coroutine trait
    std::coroutine_handle<Promise> handle;

    auto operator co_await() const & noexcept {
        struct Awaitable {
            std::coroutine_handle<Promise> handle;

            bool await_ready() const noexcept {
                // We need to save caller
                return false;
            };

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) const noexcept {
                // Save caller
                handle.promise().caller = h;
                return handle;
            }

            Ret await_resume() noexcept {
                Ret ret = std::move(handle.promise().ret.value());
                // Destroy handle because it is in final_suspended state if we get here
                handle.destroy();
                return ret;
            }
        };
        return Awaitable{.handle = handle};
    }

    struct Promise {
        Promise() = default;

        std::coroutine_handle<> caller{};
        std::optional<Ret> ret;

        std::suspend_always initial_suspend() noexcept { return {}; }

        void return_value(Ret &&result) {
            ret = std::move(result);
        }

        void return_value(const Ret &result) {
            ret = result;
        }

        auto final_suspend() noexcept {
            struct Awaitable {
                bool has_caller;

                bool await_ready() noexcept { return !has_caller; }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
                    // Pass control to the caller without creating additional stack frame.
                    // h will be freed in `operator co_await()::Awaitable::await_resume`
                    return h.promise().caller;
                };

                void await_resume() noexcept {
                }
            };
            return Awaitable{.has_caller = (caller.address() != nullptr)};
        }

        void unhandled_exception() noexcept {
            *(int *) 0x142 = 42;
        }

        Task get_return_object() {
            return {.handle = std::coroutine_handle<Promise>::from_promise(*this)};
        }
    };

    struct SyncPromise;
    struct SyncObject : public std::future<Ret> {
        using promise_type = SyncPromise;
    };

    struct SyncPromise : public std::promise<Ret> {
        std::suspend_never initial_suspend() noexcept { return {}; }

        std::suspend_never final_suspend() noexcept { return {}; }

        void unhandled_exception() { this->set_exception(std::current_exception()); }

        SyncObject get_return_object() { return {this->get_future()}; }

        void return_value(Ret &&t) { this->set_value(std::move(t)); }

        void return_value(const Ret &t) { this->set_value(t); }
    };

    std::future<Ret> to_future() && {
        return [](Task &&t) -> SyncObject {
            co_return co_await t;
        }(std::move(*this));
    }
};

template<>
struct Task<void> {
    struct Promise;
    using promise_type = Promise; //< NOLINT: coroutine trait
    std::coroutine_handle<Promise> handle;

    auto operator co_await() const &noexcept {
        struct Awaitable {
            std::coroutine_handle<Promise> handle;

            [[nodiscard]] bool await_ready() const noexcept {
                // We need to save caller
                return false;
            };

            [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) const noexcept {
                // Save caller
                handle.promise().caller = h;
                return handle;
            }

            void await_resume() noexcept {
                // Destroy handle because it is in final_suspended state if we get here
                handle.destroy();
            }
        };
        return Awaitable{.handle = handle};
    }

    struct Promise {
        Promise() = default;

        std::coroutine_handle<> caller{};

        std::suspend_always initial_suspend() noexcept { return {}; }

        void return_void() {
        }

        auto final_suspend() noexcept {
            struct Awaitable {
                bool has_caller;

                bool await_ready() noexcept { return !has_caller; }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
                    // Pass control to the caller without creating additional stack frame.
                    // h will be freed in `operator co_await()::Awaitable::await_resume`
                    return h.promise().caller;
                };

                void await_resume() noexcept {
                }
            };
            return Awaitable{.has_caller = (caller.address() != nullptr)};
        }

        void unhandled_exception() noexcept {
            *(int *) 0x142 = 42;
        }

        Task get_return_object() {
            return {.handle = std::coroutine_handle<Promise>::from_promise(*this)};
        }
    };

    struct SyncPromise;
    struct SyncObject : public std::future<void> {
        using promise_type = SyncPromise; // NOLINT: coroutine traits
    };

    struct SyncPromise : public std::promise<void> {
        std::suspend_never initial_suspend() noexcept { return {}; }

        std::suspend_never final_suspend() noexcept { return {}; }

        void unhandled_exception() { this->set_exception(std::current_exception()); }

        SyncObject get_return_object() { return {this->get_future()}; }

        void return_void() { this->set_value(); }
    };

    std::future<void> to_future() && {
        return [](Task &&t) -> SyncObject {
            co_return co_await t;
        }(std::move(*this));
    }

    void run_detached() && {
        handle.resume();
    }
};

/**
 * Run coroutine without caller
 * @param aw Coroutine
 */
inline void run_detached(Task<void> &&t) {
    std::move(t).run_detached();
}

/**
 * Get std::future from coroutine. Should be run only outside the event loop.
 * @tparam T Return type
 * @param aw Awaitable coroutine
 * @return `std::future<T>`
 */
template<typename T>
inline std::future<T> to_future(Task<T> &&t) {
    return std::move(t).to_future();
}

template<>
inline std::future<void> to_future(Task<void> &&t) {
    return std::move(t).to_future();
}

} // namespace ag::coro
