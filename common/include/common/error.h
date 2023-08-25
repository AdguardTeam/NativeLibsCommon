#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>

#include <fmt/format.h>

#include "common/logger.h"

namespace ag {

// C++17 equivalent of C++20 std::source_location. Currently filled by macros.
struct SourceLocation {
    std::string_view func_name;
    int line;
};

/**
 * Error code to string convertor interface.
 * Custom error should implement this class.
 * @tparam Enum     Enum type that contains error codes.
 */
template <typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
struct ErrorCodeToString {
    std::string operator()(Enum code) = 0;
};

class ErrorBase { // NOLINT(*-special-member-functions)
public:
    virtual std::string str() = 0;
    virtual ~ErrorBase() = default;
};

using ErrorBasePtr = std::shared_ptr<ErrorBase>;

/**
 * Basic class for errors.
 * @tparam Enum     Enum type that contains error codes.
 */
template <typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
class ErrorImpl : public ErrorBase {
public:
    ErrorImpl(SourceLocation source_location, Enum value, ErrorBasePtr next_error)
            : m_source_location(source_location)
            , m_value(value)
            , m_next_error(std::move(next_error)) {
    }

    ErrorImpl(SourceLocation source_location, Enum value, std::string_view message, ErrorBasePtr next_error)
            : m_source_location(source_location)
            , m_value(value)
            , m_message(message)
            , m_next_error(std::move(next_error)) {
    }

    [[nodiscard]] const Enum &value() const {
        return m_value;
    }

    /**
     * @return String representation of error and its parents
     */
    [[nodiscard]] std::string str() override {
        fmt::basic_memory_buffer<char> buffer;

        fmt::format_to(
                std::back_inserter(buffer), "Error at {}:{}", m_source_location.func_name, m_source_location.line);

        if (std::string error_str = ErrorCodeToString<Enum>()(m_value); !error_str.empty()) {
            fmt::format_to(std::back_inserter(buffer), ": {}", error_str);
        }

        if (!m_message.empty()) {
            fmt::format_to(std::back_inserter(buffer), ": {}", m_message);
        }

        if (m_next_error) {
            fmt::format_to(std::back_inserter(buffer), "\nCaused by: {}", m_next_error->str());
        }
        return fmt::to_string(buffer);
    }

    /**
     * @return The next error in the stack if some
     */
    [[nodiscard]] const ErrorBasePtr &next() const {
        return m_next_error;
    }

protected:
    SourceLocation m_source_location;
    std::string m_message;
    Enum m_value;
    ErrorBasePtr m_next_error;
};

/*
 * Basic error container
 */
template <typename T>
using Error = std::shared_ptr<ErrorImpl<T>>;

/**
 * Basic result container
 * @tparam R        Result value type
 * @tparam Enum     Error type enum
 */
template <typename R, typename Enum>
class Result {
public:
    template <typename T, typename = std::enable_if_t<std::is_constructible_v<std::variant<R, Error<Enum>>, T>>>
    Result(T &&value) // NOLINT(*-explicit-constructor, *-explicit-conversions)
            : m_value(std::forward<T>(value)) {
        if (auto err = std::get_if<Error<Enum>>(&m_value); err && *err == nullptr) {
            Result::invalid_error(__func__);
        }
    }

    template <typename = std::enable_if_t<std::is_default_constructible_v<R>>>
    Result()
            : m_value(R{}) {
    }

    [[nodiscard]] bool has_value() const {
        return std::holds_alternative<R>(m_value);
    }
    [[nodiscard]] bool has_error() const {
        return !has_value();
    }
    [[nodiscard]] explicit operator bool() const {
        return has_value();
    }
    [[nodiscard]] const R &value() const noexcept {
        return std::get<R>(m_value);
    }
    [[nodiscard]] R &value() noexcept {
        return std::get<R>(m_value);
    }
    [[nodiscard]] const R &operator*() const noexcept {
        return std::get<R>(m_value);
    }
    [[nodiscard]] R &operator*() noexcept {
        return std::get<R>(m_value);
    }
    [[nodiscard]] const R *operator->() const noexcept {
        return &std::get<R>(m_value);
    }
    [[nodiscard]] R *operator->() noexcept {
        return &std::get<R>(m_value);
    }
    [[nodiscard]] const Error<Enum> &error() const noexcept {
        return std::get<Error<Enum>>(m_value);
    }

private:
    std::variant<R, Error<Enum>> m_value;

    void invalid_error(std::string_view sv) {
        static ag::Logger log_{sv}; // NOLINT(*-identifier-naming)
        errlog(log_, "Result should have either value or error");
        abort();
    }
};

template <>
struct ErrorCodeToString<std::errc> {
    std::string operator()(std::errc code) {
        return std::system_category().message((int) code);
    }
};

using SystemError = Error<std::errc>;

template <typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
Error<Enum> make_error_func(SourceLocation source_location, Enum value, ErrorBasePtr next_error = nullptr) {
    return std::make_shared<ErrorImpl<Enum>>(source_location, value, next_error);
}

template <typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
Error<Enum> make_error_func(
        SourceLocation source_location, Enum value, std::string_view message, ErrorBasePtr next_error = nullptr) {
    return std::make_shared<ErrorImpl<Enum>>(source_location, value, message, next_error);
}

constexpr std::string_view pretty_func(std::string_view pretty_function, std::string_view func) {
    size_t end = pretty_function.find('(');
    size_t start = pretty_function.rfind(func, end);
    if (start == std::string_view::npos || start == 0) {
        return func;
    }
    start = pretty_function.rfind(' ', start);
    if (start == std::string_view::npos) {
        return pretty_function.substr(0, end);
    }
    return pretty_function.substr(start + 1, end - start - 1);
}

#ifndef _WIN32
#define ERROR_PRETTY_FUNC ::ag::pretty_func(__PRETTY_FUNCTION__, __func__)
#else
#define ERROR_PRETTY_FUNC ::ag::pretty_func(__FUNCSIG__, __func__)
#endif

// TODO: Move to std::source_location when c++20 will be enabled
#define make_error(...) ::ag::make_error_func(::ag::SourceLocation{ERROR_PRETTY_FUNC, __LINE__}, __VA_ARGS__)

/**
 * Makes std::errc from errno
 */
std::errc errc_from_errno(int err);

/**
 * Makes std::errc from socket error. Same as errc_from_errno on non-Windows platforms
 */
std::errc errc_from_socket_error(int socket_err);

} // namespace ag
