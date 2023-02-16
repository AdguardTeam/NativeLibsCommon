#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

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
     * @param name
     */
    explicit Logger(std::string_view name) : m_name(name) {}

    /**
     * Log message
     * @param level Log level
     * @param fmt Format string. Use FMT_STRING to enable type checks
     * @param args Format arguments
     */
    template <typename... Ts>
    inline void log(LogLevel level, fmt::format_string<Ts...> fmt, Ts&&... args) const {
        vlog(level, fmt::string_view(fmt), fmt::make_format_args(args...));
    }

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
    inline void vlog(LogLevel level, fmt::string_view format, fmt::format_args args) const {
        if (is_enabled(level)) {
            fmt::basic_memory_buffer<char> buffer;
            constexpr std::string_view SPACE = " ";

            buffer.append(m_name);
            buffer.append(SPACE);
            fmt::detail::vformat_to(buffer, format, args);

            log_impl(level, std::string_view{buffer.data(), buffer.size()});
        }
    }

    void log_impl(LogLevel level, std::string_view message) const;

    std::string m_name;
};

#define errlog(l, fmt_, ...) (l).log(::ag::LOG_LEVEL_ERROR, FMT_STRING("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__)
#define warnlog(l, fmt_, ...) (l).log(::ag::LOG_LEVEL_WARN, FMT_STRING("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__)
#define infolog(l, fmt_, ...) (l).log(::ag::LOG_LEVEL_INFO, FMT_STRING("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__)
#define dbglog(l, fmt_, ...) do { if ((l).is_enabled(::ag::LOG_LEVEL_DEBUG)) (l).log(::ag::LOG_LEVEL_DEBUG, FMT_STRING("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__); } while(0)
#define tracelog(l, fmt_, ...) do { if ((l).is_enabled(::ag::LOG_LEVEL_TRACE)) (l).log(::ag::LOG_LEVEL_TRACE, FMT_STRING("{}: " fmt_), ::fmt::string_view{__func__}, ##__VA_ARGS__); } while(0)

} // namespace ag

