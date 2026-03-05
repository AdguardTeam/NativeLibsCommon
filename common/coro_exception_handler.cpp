#include <exception>

namespace ag::coro {

[[noreturn]] void rethrow_current_exception() {
    std::rethrow_exception(std::current_exception());
}

} // namespace ag::coro
