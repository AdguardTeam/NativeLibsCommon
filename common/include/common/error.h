#pragma once

#include <type_traits>
#include <system_error>
#include <memory>
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

class ErrorBase {
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
            : m_source_location(source_location), m_value(value), m_next_error(std::move(next_error))
    {
    }

    ErrorImpl(SourceLocation source_location, Enum value, std::string_view message, ErrorBasePtr next_error)
            : m_source_location(source_location), m_value(value), m_message(message), m_next_error(std::move(next_error))
    {
    }

    Enum value() { return m_value; }

    /**
     * @return String representation of error and its parents
     */
    std::string str() override {
        std::string msg = fmt::format("Error at {}:{}: {}: {}", m_source_location.func_name, m_source_location.line,
                    ErrorCodeToString<Enum>()(m_value), m_message);
        if (m_next_error) {
            msg += "\nCaused by: ";
            msg += m_next_error->str();
        }
        return msg;
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
template<typename T>
using Error = std::shared_ptr<ErrorImpl<T>>;

/**
 * Basic result container
 * @tparam R        Result value type
 * @tparam Enum     Error type enum
 */
template<typename R, typename Enum>
class Result {
public:
    template <typename T, typename = std::enable_if_t<std::is_constructible_v<std::variant<R, Error<Enum>>, T>>>
    explicit Result(T &&value)
            : m_value(std::forward<T>(value))
    {
        if (auto err = std::get_if<Error<Enum>>(&m_value); err && *err == nullptr) {
            Result::invalid_error(__func__);
        }
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
    [[nodiscard]] const R &operator *() const noexcept {
        return std::get<R>(m_value);
    }
    [[nodiscard]] R &operator *() noexcept {
        return std::get<R>(m_value);
    }
    [[nodiscard]] const Error<Enum> &error() const noexcept {
        return std::get<Error<Enum>>(m_value);
    }
private:
    std::variant<R, Error<Enum>> m_value;

    void invalid_error(std::string_view sv) {
        static ag::Logger log_{sv};
        errlog(log_, "Result should have either value or error");
        abort();
    }
};

template<>
struct ErrorCodeToString<std::errc> {
    std::string operator()(std::errc code) {
        return std::system_category().message((int)code);
    }
};

using SystemError = Error<std::errc>;

template <typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
Error<Enum> make_error_func(SourceLocation source_location, Enum value, ErrorBasePtr next_error = nullptr) {
    return std::make_shared<ErrorImpl<Enum>>(source_location, value, next_error);
}

template <typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
Error<Enum> make_error_func(SourceLocation source_location, Enum value, std::string_view message, ErrorBasePtr next_error = nullptr) {
    return std::make_shared<ErrorImpl<Enum>>(source_location, value, message, next_error);
}

constexpr std::string_view pretty_func(std::string_view pretty_function, std::string_view func) {
    size_t pos = pretty_function.find('(');
    pos = pretty_function.rfind(func, pos);
    if (pos == std::string_view::npos || pos == 0) {
        return func;
    }
    size_t size = func.size();
    for (size_t i = pos - 1; i >= 0; --i) {
        if (pretty_function[i] == ' ') {
            return {pretty_function.data() + i + 1, size};
        } else {
            size++;
        }
    }
    return func;
}

#ifndef _WIN32
#define ERROR_PRETTY_FUNC ::ag::pretty_func(__PRETTY_FUNCTION__, __func__)
#else
#define ERROR_PRETTY_FUNC ::ag::pretty_func(__FUNCSIG__, __func__)
#endif

// TODO: Move to std::source_location when c++20 will be enabled
#define make_error(...) make_error_func(::ag::SourceLocation{ERROR_PRETTY_FUNC, __LINE__}, __VA_ARGS__)

/**
 * Makes std::errc from errno
 */
std::errc errc_from_errno(int err);

/**
 * Makes std::errc from socket error. Same as errc_from_errno on non-Windows platforms
 */
std::errc errc_from_socket_error(int socket_err);

} // namespace ag
