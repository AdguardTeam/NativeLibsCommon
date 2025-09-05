#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace ag {

/**
 * @brief Move-only function wrapper, similar to std::move_only_function in C++23
 *
 * This class provides a move-only function wrapper that can store any callable
 * object (function pointers, lambdas, function objects) with the specified signature.
 * Unlike std::function, this wrapper is move-only, which allows it to store
 * move-only callables like lambdas with unique_ptr captures.
 *
 * @tparam Signature Function signature in the form R(Args...)
 */
#if __cplusplus >= 202302L
// Use std::move_only_function if available
template<typename Signature>
using MoveOnlyFunction = std::move_only_function<Signature>;
#else
// Own implementation for C++20 and earlier
template <typename>
class MoveOnlyFunction;

template <typename R, typename... Args>
class MoveOnlyFunction<R(Args...)> {
private:
    struct CallableBase {
        virtual ~CallableBase() = default;
        CallableBase() = default;
        CallableBase(const CallableBase &) = delete;
        CallableBase &operator=(const CallableBase &) = delete;
        CallableBase(CallableBase &&) = delete;
        CallableBase &operator=(CallableBase &&) = delete;
        virtual R call(Args &&...args) = 0;
        virtual CallableBase *move_into(void *memory_buffer) noexcept = 0;
    };

    template <typename F>
    struct Callable : CallableBase {
        F func;
        explicit Callable(F &&f)
                : func(std::move(f)) {
        }
        R call(Args &&...args) override {
            return func(std::forward<Args>(args)...);
        }
        // Only valid if memory_base has enough capacity to store the value
        // (e.g. this class size is small enough to be used in SBO)
        // Memory buffer should have alignas() of this class or more.
        CallableBase *move_into(void *memory_buffer) noexcept override {
            return new (memory_buffer) Callable(std::move(func));
        }
    };

    /**
     * Movable heap storage with SBO, like __function_base implementations in STL.
     * BUFFER_SIZE is the same as in libc++ std::__function_base, together with m_ptr
     * having a total size of 4 void *'s.
     */
    template <typename BaseType, size_t BUFFER_SIZE = 3 * sizeof(void *)>
        requires requires { std::declval<BaseType *>()->move_into(nullptr); }
    class SboStorage {
        alignas(std::max_align_t) uint8_t m_buf[BUFFER_SIZE]{};
        BaseType *m_ptr = nullptr;

    public:
        SboStorage() = default;

        template <typename T, typename... MakeArgs>
        static SboStorage make(MakeArgs &&...args)
        {
            SboStorage box;
            if constexpr (sizeof(T) <= BUFFER_SIZE) {
                box.m_ptr = new (box.m_buf) T(std::forward<MakeArgs>(args)...);
            } else {
                box.m_ptr = new T(std::forward<MakeArgs>(args)...);
            }
            return box;
        }

        SboStorage(const SboStorage &) = delete;
        SboStorage &operator=(const SboStorage &) = delete;

        SboStorage(SboStorage &&other) noexcept {
            if (other.m_ptr && other.is_sbo()) {
                m_ptr = other.m_ptr->move_into(m_buf);
                other.reset();
            } else {
                m_ptr = other.m_ptr;
                other.m_ptr = nullptr;
            }
        }

        SboStorage &operator=(SboStorage &&other) noexcept {
            if (this != &other) {
                reset();
                if (other.m_ptr && other.is_sbo()) {
                    m_ptr = other.m_ptr->move_into(m_buf);
                    other.reset();
                } else {
                    m_ptr = other.m_ptr;
                    other.m_ptr = nullptr;
                }
            }
            return *this;
        }

        ~SboStorage() {
            reset();
        }

        void reset() {
            if (m_ptr) {
                if (is_sbo()) {
                    if constexpr (std::is_destructible_v<BaseType>) {
                        m_ptr->~BaseType();
                    }
                } else {
                    delete m_ptr;
                }
                m_ptr = nullptr;
            }
        }

        [[nodiscard]] BaseType *get() const noexcept {
            return m_ptr;
        }

        [[nodiscard]] BaseType &operator*() const noexcept {
            return *m_ptr;
        }

        [[nodiscard]] BaseType *operator->() const noexcept {
            return m_ptr;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_ptr != nullptr;
        }

        [[nodiscard]] bool is_sbo() const {
            const void *ptr = m_ptr;
            const void *begin = m_buf;
            const void *end = m_buf + BUFFER_SIZE;
            return ptr >= begin && ptr < end;
        }
    };

    SboStorage<CallableBase> m_impl;

public:
    MoveOnlyFunction() = default;

    template <typename F>
    MoveOnlyFunction(F &&f) // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
        requires(!std::is_same_v<std::decay_t<F>, MoveOnlyFunction> && std::is_invocable_r_v<R, F, Args...>)
            : m_impl(SboStorage<CallableBase>::template make<Callable<std::decay_t<F>>>(std::forward<F>(f))) {
    }

    MoveOnlyFunction(const MoveOnlyFunction &) = delete;
    MoveOnlyFunction &operator=(const MoveOnlyFunction &) = delete;

    MoveOnlyFunction(MoveOnlyFunction &&) noexcept = default;
    MoveOnlyFunction &operator=(MoveOnlyFunction &&) noexcept = default;

    ~MoveOnlyFunction() = default;

    R operator()(Args &&...args) {
        if (!m_impl) {
#ifdef __cpp_exceptions
            throw std::bad_function_call();
#else
            std::abort();
#endif
        }
        return m_impl->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(m_impl);
    }
};
#endif

} // namespace ag
