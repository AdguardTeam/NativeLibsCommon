#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "common/coro.h"

namespace ag::parallel {

template<typename R>
struct AnyOfCondSharedState : public std::enable_shared_from_this<AnyOfCondSharedState<R>> {
    template<typename Func, typename = std::enable_if_t<std::is_assignable_v<std::function<bool(const R &)>, Func>>>
    AnyOfCondSharedState(Func &&cond) : check_cond(std::forward<Func>(cond)) {}

    std::function<bool(const R &)> check_cond;
    std::mutex mutex{};
    std::coroutine_handle<> suspended_handle{};
    size_t remaining = 0;
    std::optional<R> return_value{};

    coro::Task<void> run(auto aw) {
        auto weak_self = this->weak_from_this();
        this->remaining++;
        auto r = co_await aw;
        if (weak_self.expired()) {
            co_return;
        }
        std::unique_lock l(this->mutex);
        --this->remaining;
        bool has_return_value = false;
        if (!this->return_value.has_value() && (!this->check_cond || this->check_cond(r))) {
            this->return_value = std::move(r);
            has_return_value = true;
        }
        if ((!this->return_value.has_value() && this->remaining == 0) || has_return_value) {
            if (this->suspended_handle) {
                auto h = this->suspended_handle;
                l.unlock();
                h.resume();
            }
        }
        co_return;
    }
};

template<typename R>
struct AnyOfCondAwaitable {
    // For some inexplicable reason, we are forced to declare a constructor because
    // otherwise MSCV does not correctly manage the lifetime of this structure
    explicit AnyOfCondAwaitable(std::function<bool(const R &)> &&check_cond) {
        state = std::make_shared<AnyOfCondSharedState<R>>(std::move(check_cond));
    }

    std::shared_ptr<AnyOfCondSharedState<R>> state;

    template <typename Aw>
    void add(Aw &&aw) {
        coro::run_detached(state->run(std::forward<Aw>(aw)));
    }

    bool await_ready() {
        std::unique_lock l{state->mutex};
        return state->return_value.has_value() || state->remaining == 0;
    }

    void await_suspend(std::coroutine_handle<> h) {
        std::unique_lock l{state->mutex};
        state->suspended_handle = h;
        if (state->return_value) {
            l.unlock();
            h.resume();
        }
    }

    std::optional<R> await_resume() {
        std::unique_lock l{state->mutex};
        return std::move(state->return_value);
    }
};

/**
 * Returns when any of awaitables is finished and result matches condition.
 * Remaining awaitables will be completed anyway, but without continuation.
 * @tparam R Return type of every awaitable in parameters.
 * @return Awaitable with return type std::optional<R>. Optional is set if at least one coroutine was
 * finished and matched condition.
 */
template<typename R, typename ...Aws>
auto any_of_cond(std::function<bool(const R &)> check_cond, Aws &&...aws) {
    // Execute this immediately to copy/move all awaitables into shared state - parameters may be temporary
    AnyOfCondAwaitable<R> ret{std::move(check_cond)};
    (ret.add(std::forward<Aws>(aws)), ...);
    return ret;
}

template <typename R>
concept NonVoid = !std::is_void_v<R>;

template <typename R>
concept Void = std::is_void_v<R>;

/**
 * Returns when any of awaitables is finished.
 * Remaining awaitables will be completed anyway, but without continuation.
 * @tparam R Return type of every awaitable in parameters.
 * @return Awaitable with return type R.
 */
template<NonVoid R, typename Aw, typename ...Aws>
auto any_of(Aw &&aw, Aws &&...aws) {
    // Execute this immediately to copy/move all awaitables into shared state - parameters may be temporary
    auto any_of_cond_awaitable = any_of_cond<R>(nullptr, std::forward<Aw>(aw), std::forward<Aws>(aws)...);

    auto await_and_transform_result = [](auto any_of_cond_awaitable) -> coro::Task<R> {
        R ret = (co_await any_of_cond_awaitable).value();
        co_return ret;
    }(std::move(any_of_cond_awaitable));

    return await_and_transform_result;
}

/**
 * Returns when any of awaitables is finished.
 * Remaining awaitables will be completed anyway, but without continuation.
 * Return type of awaitables in parameters may be any.
 * @return Awaitable with void return type.
 */
template<Void R, typename ...Aws>
auto any_of(Aws &&...aws) {
    // Execute this immediately to copy/move all awaitables into shared state - parameters may be temporary
    auto any_of_cond_awaitable = any_of<bool>([](Aws &&a) -> coro::Task<bool> {
        co_await std::forward<Aws>(a);
        co_return true;
    }(std::forward<Aws>(aws))...);

    auto await_and_transform_result = [](auto any_of_cond_awaitable) -> coro::Task<R> {
        (void) co_await any_of_cond_awaitable;
        co_return;
    }(std::move(any_of_cond_awaitable));

    return await_and_transform_result;
}

template<typename R>
struct AllOfSharedState {
    std::mutex mutex{};
    std::coroutine_handle<> suspended_handle{};
    size_t remaining = 0;
    std::vector<R> return_values{};

    coro::Task<void> run(auto aw) {
        this->remaining++;
        auto r = co_await aw;
        std::unique_lock l(this->mutex);
        this->return_values.emplace_back(std::move(r));
        if (--this->remaining == 0) {
            if (this->suspended_handle) {
                auto h = this->suspended_handle;
                l.unlock();
                h.resume();
            }
        }
        co_return;
    };
};

template<typename R>
struct AllOfAwaitable {
    std::shared_ptr<AllOfSharedState<R>> state;

    template <typename Aw>
    void add(Aw &&aw) {
        coro::run_detached(state->run(std::forward<Aw>(aw)));
    }

    bool await_ready() {
        std::unique_lock l{state->mutex};
        return state->remaining == 0;
    }

    void await_suspend(std::coroutine_handle<> h) {
        std::unique_lock l{state->mutex};
        state->suspended_handle = h;
        if (state->remaining == 0) {
            l.unlock();
            h.resume();
        }
    }

    std::vector<R> await_resume() {
        std::unique_lock l{state->mutex};
        return std::move(state->return_values);
    }
};

/**
 * Returns when all awaitables are finished.
 * @tparam R Return type of every awaitable in parameters.
 * @return Awaitable with return type R
 */
template<NonVoid R, typename ...Aws>
auto all_of(Aws &&...aws) {
    // Execute this immediately to copy/move all awaitables into shared state - parameters may be temporary
    AllOfAwaitable<R> ret = {.state = std::make_shared<AllOfSharedState<R>>()};
    (ret.add(std::forward<Aws>(aws)), ...);
    return ret;
}

/**
 * Returns when all awaitables are finished.
 * Return type of awaitables in parameters may be any.
 * @return Awaitable with void return type.
 */
template<Void R, typename ...Awaitables>
auto all_of(Awaitables &&...awaitables) {
    // Execute this immediately to copy/move all awaitables into shared state - parameters may be temporary
    auto all_of_awaitable = all_of<bool>([](Awaitables &&a) -> coro::Task<bool> {
        co_await std::forward<Awaitables>(a);
        co_return true;
    }(std::forward<Awaitables>(awaitables))...);

    auto await_and_transform_result = [](auto all_of_awaitable) -> coro::Task<R> {
        (void) co_await all_of_awaitable;
        co_return;
    }(std::move(all_of_awaitable));

    return await_and_transform_result;
}

} // namespace ag::parallel
