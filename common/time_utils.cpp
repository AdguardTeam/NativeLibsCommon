#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "common/time_utils.h"
#include "common/utils.h"

#if defined(ANDROID) && !defined(__LP64__)
#include <time64.h>
#endif
#include <fmt/args.h>

namespace ag {

tm gmtime_from_system_time(SystemTime timepoint) {
    int64_t t = to_secs(timepoint.time_since_epoch()).count();
    tm tm_info{};

#if defined(_WIN32)
    _gmtime64_s(&tm_info, &t);
#elif defined(ANDROID) && !defined(__LP64__)
    gmtime64_r(&t, &tm_info);
#elif defined(__LP64__)
    time_t ttime = t;
    gmtime_r(&ttime, &tm_info);
#else
#error "Unsupported platform"
#endif
    return tm_info;
}

SystemTime system_time_from_gmtime(const tm &tm_info) {
    return SystemTime{std::chrono::seconds{
#if defined(_WIN32)
            _mkgmtime64(const_cast<tm *>(&tm_info))
#elif defined(ANDROID) && !defined(__LP64__)
            timegm64(const_cast<tm *>(&tm_info))
#elif defined(__LP64__)
            timegm(const_cast<tm *>(&tm_info))
#else
#error "Unsupported platform"
#endif
    }};
}

tm localtime_from_system_time(SystemTime timepoint) {
    int64_t t = to_secs(timepoint.time_since_epoch()).count();
    tm tm_info{};

#if defined(_WIN32)
    if (0 == _localtime64_s(&tm_info, &t)) {
        // Because _localtime64_s makes tm_info unusable on failure
        return tm_info;
    }
    return {};
#elif defined(ANDROID) && !defined(__LP64__)
    localtime64_r(&t, &tm_info);
#elif defined(__LP64__)
    time_t tt = t;
    localtime_r(&tt, &tm_info);
#else
#error "Unsupported platform"
#endif
    return tm_info;
}

std::chrono::microseconds to_micros(std::chrono::nanoseconds duration) {
    return std::chrono::duration_cast<std::chrono::microseconds>(duration);
}

std::chrono::milliseconds to_millis(std::chrono::nanoseconds duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration);
}

std::chrono::seconds to_secs(std::chrono::nanoseconds duration) {
    return std::chrono::duration_cast<std::chrono::seconds>(duration);
}

std::pair<size_t, tm> parse_time(const std::string &s, const char *format) {
    tm tm_info{};
    std::istringstream input(s);
    input.imbue(std::locale::classic());
    input >> std::get_time(&tm_info, format);
    if (input.fail()) {
        return std::make_pair(std::string::npos, (tm){});
    }
#ifdef _MSC_VER
    // MS STL has buggy std::get_time - it successfully parses 4-digit year as %y, but places 0 into tm_year
    if (tm_info.tm_year == 0) {
        return std::make_pair(std::string::npos, (tm){});
    }
#endif // _MSC_VER
    bool whole_string_parsed = (input.tellg() == -1);
    size_t ret_pos = whole_string_parsed ? s.size() : (size_t) input.tellg();
    return std::make_pair(ret_pos, tm_info);
}

size_t validate_gmt_tz(std::string_view s) {
    constexpr size_t ALPHA_TZ_LEN = 3;
    constexpr size_t DIGIT_TZ_LEN = 5;

    std::string_view str = ag::utils::trim(s);
    auto start_offset = size_t(str.data() - s.data());
    bool may_be_alpha_code = ag::utils::starts_with(str, "GMT") || ag::utils::starts_with(str, "UTC");
    // github.com sends Set-Cookie header with timezone "-0000"
    bool may_be_digital_code = !may_be_alpha_code
            && (ag::utils::starts_with(str, "-") || ag::utils::starts_with(str, "+"))
            && ag::utils::starts_with(str.substr(1), "0000");
    if (!may_be_alpha_code && !may_be_digital_code) {
        return std::string_view::npos;
    }

    char c = may_be_alpha_code ? str[ALPHA_TZ_LEN] : str[DIGIT_TZ_LEN];
    if (isdigit((uint8_t) c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        return std::string_view::npos;
    }

    return start_offset + (may_be_alpha_code ? ALPHA_TZ_LEN : DIGIT_TZ_LEN);
}

/**
 * This method replaces %f in format string by microseconds from time since strftime()/std::put_time
 * doesn't support microseconds.
 *
 * This method also replaces %z and %Z by +0000 and GMT if format_gmtime was called.
 * That's needed because `struct tm` doesn't contain timezone.
 * @param us Microseconds
 * @param gmt Is GMT (%z %Z) should be injected
 */
static std::string inject_microseconds_and_gmt(int64_t us, bool gmt, std::string_view format) {
    std::string format_string, us_string;
    format_string.reserve(format.size() + 10);

    enum { DEFAULT, AFTER_PERCENT } state = DEFAULT;
    for (auto &ch : format) {
        switch (state) {
        state_default:
        case DEFAULT: {
            if (ch == '%') {
                state = AFTER_PERCENT;
            } else {
                format_string.push_back(ch);
            }
            break;
        }
        case AFTER_PERCENT: {
            switch (ch) {
            case 'f': {
                if (us_string.empty()) {
                    us_string = AG_FMT("{:06}", us);
                }
                format_string += us_string;
                state = DEFAULT;
                break;
            }
            case '%':
                format_string += "%%";
                state = DEFAULT;
                break;
            case 'z':
            case 'Z':
                if (gmt) {
                    format_string += (ch == 'z') ? "+0000" : "GMT";
                    state = DEFAULT;
                    break;
                }
                [[fallthrough]];
            default:
                format_string.push_back('%');
                state = DEFAULT;
                goto state_default;
            }
        }
        }
    }
    if (state == AFTER_PERCENT) {
        format_string.push_back('%');
    }
    return format_string;
}

static std::string format_time(const tm &tm_info, int64_t us, bool gmt, const char *format) {
    std::stringstream ss;
    ss << std::put_time(&tm_info, inject_microseconds_and_gmt(us, gmt, format).c_str());
    return ss.str();
}

std::string format_gmtime(const tm &tm_info, const char *format) {
    return format_time(tm_info, 0, true, format);
}

std::string format_gmtime(SystemTime time, const char *format) {
    tm tm_info = gmtime_from_system_time(time);
    int64_t us = to_micros(time.time_since_epoch() - to_secs(time.time_since_epoch())).count();
    return format_time(tm_info, us, true, format);
}

std::string format_gmtime(SystemTime::duration time_since_epoch, const char *format) {
    return format_gmtime(SystemTime{time_since_epoch}, format);
}

std::string format_localtime(const tm &tm_info, const char *format) {
    return format_time(tm_info, 0, false, format);
}

std::string format_localtime(SystemTime time, const char *format) {
    tm tm_info = localtime_from_system_time(time);
    int64_t us = to_micros(time.time_since_epoch() - to_secs(time.time_since_epoch())).count();
    return format_time(tm_info, us, false, format);
}

std::string format_localtime(SystemTime::duration time_since_epoch, const char *format) {
    return format_localtime(SystemTime{time_since_epoch}, format);
}

timeval timeval_from_timepoint(SystemTime timepoint) {
    int64_t micros = to_micros(timepoint.time_since_epoch()).count();
    return {.tv_sec = decltype(timeval::tv_sec)(micros / 1000000),
            .tv_usec = decltype(timeval::tv_usec)(micros % 1000000)};
}

timeval duration_to_timeval(Micros usecs) {
    static constexpr intmax_t denom = decltype(usecs)::period::den;
    return {.tv_sec = static_cast<decltype(timeval::tv_sec)>(usecs.count() / denom),
            .tv_usec = static_cast<decltype(timeval::tv_usec)>(usecs.count() % denom)};
}

long get_timezone() {
#if defined(_WIN32)
    _tzset();
    return _timezone;
#else
    tzset();
    return timezone;
#endif
}
} // namespace ag
