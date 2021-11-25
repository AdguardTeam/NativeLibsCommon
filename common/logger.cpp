#include "common/logger.h"
#include "common/utils.h"
#include "common/time_utils.h"

#include <iostream>
#include <fmt/ostream.h>

namespace ag {

static constexpr std::string_view ENUM_NAMES[] = {
        "ERROR",
        "WARN",
        "INFO",
        "DEBUG",
        "TRACE"
};
static constexpr size_t ENUM_NAMES_NUMBER = std::size(ENUM_NAMES);

static void log_to_stderr(LogLevel level, std::string_view message);
const LoggerCallback Logger::LOG_TO_STDERR = log_to_stderr;

static std::atomic<LogLevel> g_log_level{LOG_LEVEL_INFO};
static std::shared_ptr<LoggerCallback> g_log_callback = std::make_shared<LoggerCallback>(Logger::LOG_TO_STDERR);

static constexpr const char *TIME_FORMAT = "%d.%m.%Y %H:%M:%S.%f";

static void log_to_stderr(LogLevel level, std::string_view message) {
    std::string_view level_str = (level >= 0 && level < ENUM_NAMES_NUMBER) ? ENUM_NAMES[level] : "UNKNOWN";
    SystemTime now = std::chrono::system_clock::now();
    std::string ts = format_localtime(now, TIME_FORMAT);
    std::clog << AG_FMT("{} {:5} [{}] {}\n", ts, level_str, std::this_thread::get_id(), message);
}

void Logger::set_log_level(LogLevel level) {
    g_log_level = level;
}

void Logger::set_callback(LoggerCallback callback) {
    std::atomic_store(&g_log_callback, std::make_shared<LoggerCallback>(
            callback ? std::move(callback) : LOG_TO_STDERR));
}

void Logger::vlog(LogLevel level, fmt::string_view format, fmt::format_args args) const {
    std::string message = AG_FMT("{} ", m_name) + fmt::vformat(format, args);
    if (is_enabled(level)) {
        auto callback = std::atomic_load(&g_log_callback);
        (*callback)(level, message);
    }
}

bool Logger::is_enabled(LogLevel level) const {
    return level <= g_log_level;
}

} // namespace ag
