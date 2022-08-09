# Developer docs

## Coroutines

### How coroutines work

Some theory on how C++ coroutines work.

#### Coroutine

Coroutine is a function that can pause execution in several suspension points.
Function is considered a coroutine if contains one of keywords `co_await`, `co_return` or `co_yield`.

- `co_await` suspends coroutine while some async operation completes, and passes it as completion handler to that async operation.
- `co_return` sets return value of coroutine and completes execution of coroutine body (but coroutine promise may have additinal actions to do).
- `co_yield` passes one value to yield handler and `co_await`'s while it is processed.

Here is some coroutine:
```c++
ReturnType test_coro(int param) {
    // Just return param
    co_return param;
}
```

We see `co_return` in function body, so it is coroutine.
Compiler rewrites coroutine body as:
```c++
ReturnType test_coro(int param) {
    // Allocated on real stack frame:
    ReturnType return_object;
    {
        "Create coroutine frame and jump into it";
        "Copy args to coroutine frame";
        
        ReturnType::promise_type promise;

        // Initialize return object on stack frame of caller:
        return_object = p.get_return_object();

        try {
            co_await promise.initial_suspend();                       // suspension point 1
            // Here is original test_coro() body:
            // Just return param
            co_return param;
        catch (...) {
            promise.unhandled_exception();
        }
        co_await promise.final_suspend();                         // suspension point 2
        "RAII destruction of promise, args and coroutine frame";
    }
}
```
Note that there are coroutine keywords, and they are rewritten too in next passes.

But let's read from start. First, coroutine function is returning some value.
It is "return object" - external interface to a running coroutine. It may do nothing but it SHOULD contain one type alias - `ReturnType::promise_type`. 

#### Promise type

Promise is an implementation of all calls to `promise` class in generated code above. 
C++ devs may someway customize it for their own needs.

It has the following interface:
```c++
struct Promise {
    ReturnType get_return_object();
    Awaitable initial_suspend();
    void return_value(T); // or void return_void();
    Awaitable final_suspend();
    void unhandled_exception();
};
```
`get_return_object` is function for receiving return object from promise.

`initial_suspend` is awaitable object that is usually one of two types - `std::suspend_always` or `std::suspend_never`.
If coroutine handle should be suspended prior to execution, then `std::suspend_always` awaitable is returned.  
It is used if caller should do some actions on called coroutines before continue.

Then coroutine body is executed, and execution is finished with `co_return`. `co_return` calls `promise.return_value()` and exits coroutine body scope.

If something gets wrong `promise.unhandled_exception` is called. Use `std::current_exception()` to receive exception.

And finally `final_suspend` is `Awaitable` object that may pass control to some completion handler, which receive returned value or exception, if it was not happened before.

This handler is usually another coroutine that awaits current one.
Or something synchronously waiting coroutine to complete.

Okay, but one thing is unclear yet - what is `Awaitable`.

#### Awaitable

`Awaitable` is something that passed to `co_await`.
It has the following interface:
```c++
struct Awaitable {
    bool await_ready();
    auto await_suspend(coroutine_handle<> caller);
    Ret await_resume();
};
```
It is usually a handle of some "paused" or "completed" async operation.
First `co_await` asks if operation is paused or completed using `await_ready()`. True means "complete", false is "paused".

If it is paused, current coroutine should be suspended. 
`co_await` implementation suspends current coroutine and calls `await_suspend(caller)`.

After completion of awaitable operation, or if async operation was already completed, `await_resume()` is called. Value returned from `await_resume()` is result of whole `co_await` expression.

In example above, expression `co_await some_expression()` is rewritten as:
```c++
    auto temporary = some_expression();
    auto awaitable = temporary.operator co_await();
    if (!awaitable.await_ready()) {
        if (coroutine.suspend() == SUSPENDED) {
            "Jump from coroutine frame to thread stack";
            // Pass suspended coroutine to awaitable:
            auto ret = awaitable.await_suspend(coroutine);
            // Optional: If await_suspend returns some another coroutine, resume it:
            if (ret) { ret.resume(); }

            // Just return from current function call in terms of thread stack
            // It may be "ReturnType test_coro(int)" and "void `ReturnType test_coro(int)`::.resume()":
            return;
        } 
        // Point of resumption
    }
    auto result = awaitable.await_resume()
    // And result of the whole expression is:
    result
```

There are two standard `Awaitables` - `std::suspend_always` and `std::suspend_never`.

`std::suspend_always` tells `co_await` to just suspend execution. Note that caller handle is not saved anywhere, so it is very special.

`std::suspend_never` tells `co_await` to do nothing.

#### How to make coroutine `Awaitable` itself?

So, if we implement `ReturnType` and it's `promise_type`, then we can write coroutines.

Inside coroutine, we may use `co_await` of some asynchronous operations. Such operations should return `Awaitable` interfaces.

But how to make coroutine `Awaitable` itself?

There is simple recipe:

1. Coroutine promise should return `std::suspend_always` in `initial_suspend`.
2. Coroutine return object should contain `operator co_await()` that returns `Awaitable` that is always `await_ready() == false`.
3. Then calle**r** coroutine will be suspended and passed into `await_suspend` of `Awaitable` returned on previous stage. Inside of `await_suspend` we may save the calle**r** to Promise and resume the initial-suspended calle**e**.
4. In `final_suspend` of calle**e**'s Promise we may return control to saved calle**r**.
5. Calle**r** then calls `await_resume()` to receive `co_return`ed result. After that, `Awaitable` calls `coroutine.destroy()` to clean up memory of calle**e**.

Here is simplified implementation of our task class:

```c++
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
```

### How coroutines chaining work in our example

Let's see the following code:
```c++
  1: Task<int> coro2() {
  2:     co_return 42;
  3: }
...
 10: // Inside another calling coroutine...
 11: int x = co_await coro2();
```

- Line 10. 
  - Starting to evaluate `co_await coro2()`. 
  - First, `coro2()` is called. Go to line 1.
- Line 1: 
  - Coroutine frame is created
  - `Task::Promise` is created on coroutine stack.
  - `Promise::initial_suspend()` returns `std::suspend_always`. So, `coro2()` as coroutine is suspended.
  - `coro2()` as function returns `Promise::get_return_object()`, where Task object is created.
  - Back to line 10.
- Line 10: 
  - `co_await` of returned object(`Task`) is called. Compiler checks if returned object is `Awaitable`.
         Since it is not, `Task::operator co_await()` is applied.

  - It returns `Awaitable`, which returns `Awaitable::await_ready()` = `false`.
  
  - The last means that current coroutine should be suspended and passed to `Awaitable::await_suspend(coroutine_handle)`.
         
  - Inside that function, caller is saved inside `Task::Promise` of `coro2()`. Then `coro2()` is resumed. Go to line 2.

- Line 2: 
  - We see `co_return 42`. It means that `Promise::return_value(42)` is called. It stores return value.
  - Then `Promise::final_suspend()` is called. It transfers execution to caller, saved on previous line.
        Back to line 10.
- Line 10: 
  - `Awaitable::await_resume()` is called, and it is result of whole `co_await`. Inside `await_resume`,
        `final_suspend`ed coroutine is destroyed, and saved value returned to caller.

Expression is finally evaluated.
