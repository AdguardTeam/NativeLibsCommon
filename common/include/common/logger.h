#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <optional>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include "common/format.h"

namespace ag {

enum LogLevel {
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE
};

/**
 * Logger callback. Make sure that it could somehow handle parallel calls from several threads.
 */
using LoggerCallback = std::function<void(LogLevel level, std::string_view formatted_message)>;

class Logger {
public:
    /**
     * Create logger with specified name
     * @param name Logger name
     * @param log_level_override Log level which will be used in is_enabled instead of global log level
     */
    explicit Logger(std::string_view name, std::optional<LogLevel> log_level_override = std::nullopt)
            : m_name(name)
            , m_log_level_override(log_level_override)
    {}

    /**
     * Log message
     * @param level Log level
     * @param fmt Format string. Use FMT_STRING to enable type checks
     * @param args Format arguments
     *
     * Note that optnone is here because of clang optimizer bug which leads to making bad format args.
     * https://github.com/AdguardTeam/DnsLibs/issues/182
     * TODO: Recheck when either fmt library or clang will be updated.
     * Original fix is for Android NDK, LLVM 14
     * As of LLVM 15 (Xcode 14.3 and newer), bug is not fixed.
     * Note that _MSC_VER is here because of MSVC bug of non-working fmt static checks in lambdas.
     * Original fix is for MSVC 19.38 (VS 17.8.2)
     */
#if _MSC_VER >= 1938
    template <typename... Ts>
    inline void log(LogLevel level, fmt::string_view fmt, Ts&&... args) const {
        vlog(level, fmt, fmt::make_format_args(args...));
    }
#else
    template <typename... Ts>
    [[clang::optnone]]
    inline void log(LogLevel level, ag::StrictFormatString<Ts...> fmt, Ts&&... args) const {
        vlog(level, fmt::string_view(fmt), fmt::make_format_args(args...));
    }
#endif

    /**
     * @return True if log level  is enabled on current logger
     * @param level Log level
     * @note This method is intentionally non-static in case if logger will have separate log levels
     */
    bool is_enabled(LogLevel level) const;

    /**
     * Default logger callback implementation
     */
    static const LoggerCallback LOG_TO_STDERR;

    /**
     * Set common logger log level
     * @param level Log level
     */
    static void set_log_level(LogLevel level);

    /**
     * @return Current logger log level
     */
    static LogLevel get_log_level();

    /**
     * Set common logger callback
     * @param callback Logger callback
     */
    static void set_callback(LoggerCallback callback);

    /**
     * Functor for logging to file
     * LogToFile doesn't take file ownership. You need to close manually
     */
    class LogToFile {
    public:
        /**
         * Create functor with file name to open
         * @param file File
         */
        explicit LogToFile(FILE *file) : m_file(file) {}

        void operator()(LogLevel level, std::string_view message);

    private:
        FILE *m_file;
    };

private:
    void vlog(LogLevel level, fmt::string_view format, fmt::format_args args) const;

    void log_impl(LogLevel level, std::string_view message) const;

    std::string m_name;
    std::optional<LogLevel> m_log_level_override;
};

#define errlog(l, fmt_, ...) (l).log(::ag::LOG_LEVEL_ERROR, ("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__)
#define warnlog(l, fmt_, ...) (l).log(::ag::LOG_LEVEL_WARN, ("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__)
#define infolog(l, fmt_, ...) (l).log(::ag::LOG_LEVEL_INFO, ("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__)
#define dbglog(l, fmt_, ...) do { if ((l).is_enabled(::ag::LOG_LEVEL_DEBUG)) (l).log(::ag::LOG_LEVEL_DEBUG, ("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__); } while(0)
#define tracelog(l, fmt_, ...) do { if ((l).is_enabled(::ag::LOG_LEVEL_TRACE)) (l).log(::ag::LOG_LEVEL_TRACE, ("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__); } while(0)

} // namespace ag

