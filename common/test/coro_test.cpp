#include <map>

#include "common/gtest_coro.h"
#include "common/parallel.h"
#include "common/defs.h"
#include "common/utils.h"

namespace ag::test {

struct Scheduler {
    Scheduler() {
        thread = std::thread([this]{ this->thread_worker(); });
    }
    ~Scheduler() {
        if (std::unique_lock l{mutex}) {
            stopping = true;
            wakeup.notify_all();
        }
        thread.join();
    }
    std::thread thread;
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> tasks;
    std::mutex mutex;
    std::condition_variable wakeup;
    bool stopping{false};

    void schedule(std::function<void()> f, Millis millis) {
        std::scoped_lock l(mutex);
        tasks.insert(std::make_pair(std::chrono::steady_clock::now() + millis, f));
        wakeup.notify_all();
    }

    void thread_worker() {
        std::unique_lock l(mutex);
        while (!stopping) {
            if (tasks.empty()) {
                wakeup.wait(l);
            } else {
                wakeup.wait_until(l, tasks.begin()->first);
            }
            while (!tasks.empty()) {
                auto it = tasks.begin();
                if (std::chrono::steady_clock::now() < it->first) {
                    break;
                }
                auto node = tasks.extract(it);
                l.unlock();
                node.mapped()();
                l.lock();
            }
        }
        while (!tasks.empty()) {
            auto node = tasks.extract(tasks.begin());
            l.unlock();
            node.mapped()();
            l.lock();
        }
    }

    auto sleep(Millis millis) {
        struct Awaitable { // NOLINT: awaitable trait
            Scheduler *self;
            Millis millis;
            bool await_ready() { return false; }
            void await_suspend(std::coroutine_handle<> h) {
                self->schedule(h, millis);
            }
            void await_resume() {}
        };
        return Awaitable{.self = this, .millis = millis};
    }
};

class CoroTest : public ::testing::Test {
protected:
    Scheduler m_scheduler;
};

coro::Task<int> coro1() {
    co_return 42;
}

coro::Task<int> coro2() {
    co_return 43;
}

coro::Task<void> coro3() {
    co_return;
}

TEST_F(CoroTest, Test) {
    int x = co_await coro1();
    ASSERT_EQ(42, x);
    x = co_await coro2();
    ASSERT_EQ(43, x);
    co_return;
}

TEST_F(CoroTest, ParallelTest) {
    // @fixme: fails on MSVC 17
#ifdef _WIN32
    GTEST_SKIP();
#endif

    int x = co_await parallel::any_of<int>(coro1(), coro2());
    ASSERT_EQ(42, x);
    auto is_odd = [](int x) { return (x % 2) != 0; };
    auto y = co_await parallel::any_of_cond<int>(is_odd, coro1(), coro2());
    ASSERT_EQ(43, y.value());
    auto never = [](int /*x*/) { return false; };
    auto z = co_await parallel::any_of_cond<int>(never, coro1(), coro2());
    ASSERT_FALSE(z.has_value());
    co_return;
}

coro::Task<bool> increment(Scheduler &sched, int &x) {
    using namespace std::chrono_literals;
    static auto timeout = 21ms;
    if (timeout % 2ms == 1ms) {
        co_await sched.sleep(timeout++ % 42ms);
    }
    x++;
    co_return true;
}

TEST_F(CoroTest, ParallelTestMany) {
    int x = 0;
    auto aw = parallel::all_of<bool>();
    for (int i = 0; i < 42; i++) {
        aw.add(increment(m_scheduler, x));
    }
    auto vec = co_await aw;
    ASSERT_EQ(42, vec.size());
    ASSERT_EQ(42, x);
}

TEST_F(CoroTest, Sleep) {
    utils::Timer timer;
    static constexpr auto SLEEP_TIME = Millis(500);
    co_await m_scheduler.sleep(SLEEP_TIME);
    ASSERT_GE(timer.elapsed<Millis>(), SLEEP_TIME);
    timer.reset();
    co_await parallel::all_of_void(
            m_scheduler.sleep(SLEEP_TIME),
            m_scheduler.sleep(SLEEP_TIME * 2)
    );
    ASSERT_GE(timer.elapsed<Millis>(), SLEEP_TIME * 2);
    timer.reset();
    co_await parallel::any_of_void(
            m_scheduler.sleep(SLEEP_TIME),
            m_scheduler.sleep(SLEEP_TIME * 2)
    );
    auto elapsed = timer.elapsed<Millis>();
    ASSERT_GE(elapsed, SLEEP_TIME);
    ASSERT_LE(elapsed, SLEEP_TIME * 2);
}

TEST_F(CoroTest, RunDetached) {
    run_detached(coro3());
    co_return;
}

TEST_F(CoroTest, ToFuture) {
    to_future([this]()->coro::Task<void>{
        co_await m_scheduler.sleep(Millis{500});
    }()).get();
    co_return;
}

} // namespace ag::test
