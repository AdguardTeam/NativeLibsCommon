#pragma once

#include <ctime>
#include <chrono>
#include <string>
#include <event2/util.h>

namespace ag {

using SystemTime = std::chrono::system_clock::time_point;

static constexpr const char *DEFAULT_GMTIME_FORMAT = "%Y-%m-%d %H:%M:%S GMT";
static constexpr const char *DEFAULT_LOCALTIME_FORMAT = "%Y-%m-%d %H:%M:%S %z";

/**
 * Split timepoint into parts in GMT timezone
 */
tm gmtime_from_timepoint(SystemTime t);

/**
 * Join time parts in GMT timezone into timepoint
 */
SystemTime timepoint_from_gmtime(const tm &tm);

/**
 * Split UNIX timestamp into parts in local timezone
 */
tm localtime_from_timepoint(SystemTime t);

/**
 * @return Duration in microseconds
 */
std::chrono::microseconds to_micros(std::chrono::nanoseconds duration);

/**
 * @return Duration in milliseconds
 */
std::chrono::milliseconds to_millis(std::chrono::nanoseconds duration);

/**
 * @return Duration in seconds
 */
std::chrono::seconds to_secs(std::chrono::nanoseconds duration);

/**
 * Parse time string
 * @param s Input string
 * @param format Format string in `strptime` format.
 *               Doesn't support %z and %Z, please use validate_gmt_tz() if you want to validate GMT timezone.
 * @param tm Time structure for parsed result
 * @return Pair of values:
 *         first: Index of the first character in string that has not been required to satisfy the specified
 *         conversions in format, or `npos` if parsing failed
 *         second: Valid tm structure if first is not `npos`
 */
std::pair<size_t, tm> parse_time(const std::string &s, const char *format);

/**
 * Check if timezone is one of the following "GMT", "UTC", "+0000" or "-0000"
 * @param s Input string
 * @return Index of the first character in string after parsed timezone, or
 *         `npos` if not valid
 */
size_t validate_gmt_tz(std::string_view s);

/**
 * Format GMT time
 * @param time System time
 * @param format Format string in strftime() format, extended by "%f" for microseconds.
 */
std::string format_gmtime(SystemTime time, const char *format = DEFAULT_GMTIME_FORMAT);

/**
 * Format GMT time
 * @param tm_info System time in `tm` structure
 * @param format Format string in strftime() format, extended by "%f" for microseconds.
 */
std::string format_gmtime(const tm &tm_info, const char *format = DEFAULT_GMTIME_FORMAT);

/**
 * Format local time
 * @param time System time
 * @param format Format string in strftime() format, extended by "%f" for microseconds.
 */
std::string format_localtime(SystemTime time, const char *format = DEFAULT_LOCALTIME_FORMAT);

/**
 * Format local time
 * @param time System time
 * @param format Format string in strftime() format, extended by "%f" for microseconds.
 */
std::string format_localtime(const tm &time, const char *format = DEFAULT_LOCALTIME_FORMAT);

/**
 * @return Timeval from timepoint
 * @note Timeval is not y2038-safe on Windows by now.
 */
timeval timeval_from_timepoint(SystemTime timepoint);

/**
 *
 * @return timezone platform dependent
 */
long get_timezone();

} // namespace ag
