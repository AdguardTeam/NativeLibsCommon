#include <algorithm>
#include <array>
#include <codecvt>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <locale>
#include <numeric>

#include "common/socket_address.h"
#include "common/utils.h"

namespace ag {

std::vector<std::string_view> utils::split_by(std::string_view str, std::string_view delim, bool include_empty) {
    if (str.empty()) {
        return include_empty ? std::vector{str} : std::vector<std::string_view>{};
    }

    size_t num = 1;
    size_t seek = 0;
    while (true) {
        size_t pos = str.find(delim, seek);
        if (pos != std::string_view::npos) {
            ++num;
            seek = pos + delim.length();
        } else {
            break;
        }
    }

    seek = 0;
    std::vector<std::string_view> out;
    out.reserve(num);
    for (size_t i = 0; i < num; ++i) {
        size_t start = seek;
        size_t end = str.find(delim, seek);
        if (end == std::string_view::npos) {
            end = str.length();
        }
        size_t length = end - start;
        if (length != 0) {
            std::string_view s = utils::trim(str.substr(seek, length));
            if (include_empty || !s.empty()) {
                out.push_back(s);
            }
        }
        seek = end + delim.length();
    }
    out.shrink_to_fit();

    return out;
}

std::vector<std::string_view> utils::split_by(std::string_view str, int delim, bool include_empty) {
    return split_by_any_of(str, {(char *) &delim, 1}, include_empty);
}

std::vector<std::string_view> utils::split_by_any_of(std::string_view str, std::string_view delim, bool include_empty) {
    if (str.empty()) {
        return include_empty ? std::vector{str} : std::vector<std::string_view>{};
    }

    size_t num = 1 + std::count_if(str.begin(), str.end(), [&delim](int c) {
        return delim.find(c) != delim.npos;
    });
    size_t seek = 0;
    std::vector<std::string_view> out;
    out.reserve(num);
    for (size_t i = 0; i < num; ++i) {
        size_t start = seek;
        size_t end = str.find_first_of(delim, seek);
        if (end == std::string_view::npos) {
            end = str.length();
        }
        size_t length = end - start;
        if (length != 0) {
            std::string_view s = utils::trim(str.substr(seek, length));
            if (include_empty || !s.empty()) {
                out.push_back(s);
            }
        }
        seek = end + 1;
    }
    out.shrink_to_fit();

    return out;
}

static std::array<std::string_view, 2> split2(std::string_view str, std::string_view delim, bool reverse) {
    std::string_view first;
    std::string_view second;

    size_t seek = !reverse ? str.find_first_of(delim) : str.find_last_of(delim);
    if (seek != std::string_view::npos) {
        first = {str.data(), seek};
        second = {str.data() + seek + 1, str.length() - seek - 1};
    } else {
        first = str;
        second = {};
    }

    return {first, second};
}

std::array<std::string_view, 2> utils::split2_by(std::string_view str, int delim) {
    return split2(str, {(char *) &delim, 1}, false);
}

std::array<std::string_view, 2> utils::rsplit2_by(std::string_view str, int delim) {
    return split2(str, {(char *) &delim, 1}, true);
}

std::array<std::string_view, 2> utils::split2_by_any_of(std::string_view str, std::string_view delim) {
    return split2(str, delim, false);
}

bool utils::is_valid_ip4(std::string_view str) {
    SocketAddress addr(str, 0);
    return addr.valid() && addr.c_sockaddr()->sa_family == AF_INET;
}

bool utils::is_valid_ip6(std::string_view str) {
    SocketAddress addr(str, 0);
    return addr.valid() && addr.c_sockaddr()->sa_family == AF_INET6;
}

std::wstring utils::to_wstring(std::string_view sv) {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(sv.data(), sv.data() + sv.size());
}

std::string utils::from_wstring(std::wstring_view wsv) {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(wsv.data(), wsv.data() + wsv.size());
}

int utils::for_each_line(std::string_view str, LineAction action, void *arg) {
    using SizeType = std::string_view::size_type;
    SizeType start = 0;
    while (start != str.size()) {
        SizeType end = str.find_first_of("\r\n", start);

        if (end == std::string_view::npos) {
            str.remove_prefix(start);
            str = utils::trim(str);
            action(start, str, arg);
            return 0;
        }

        SizeType len = end - start;
        std::string_view line = utils::trim({&str[start], len});

        if (!action(start, line, arg)) {
            return 0;
        }

        start += len + 1;
    }
    return 0;
}

std::optional<std::string_view> utils::read_line(std::string_view str, size_t pos) {
    using SizeType = std::string_view::size_type;

    if (pos >= str.size()) {
        return std::nullopt;
    }

    SizeType start = pos;
    SizeType end = str.find_first_of("\r\n", start);

    if (end == std::string_view::npos) {
        end = str.size();
    }

    return utils::trim({&str[start], end - start});
}

#ifdef __linux__
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
uint32_t utils::gettid(void) {
    return syscall(SYS_gettid);
}
#endif //__linux__

#ifdef __MACH__
#include <pthread.h>
uint32_t utils::gettid(void) {
    uint64_t tid;
    if (0 != pthread_threadid_np(NULL, &tid))
        return 0;
    return (uint32_t) tid;
}
#endif //__MACH__

#ifdef _WIN32
#include <process.h>
#include <winbase.h>
#include <windows.h>
uint32_t utils::gettid(void) {
    return GetCurrentThreadId();
}
#endif // _WIN32

std::string utils::encode_to_hex(Uint8View data) {
    static constexpr char TABLE[] = "0123456789abcdef";
    static constexpr size_t NIBBLE_BITS = 4;
    static constexpr uint8_t LEAST_SIGNIFICANT_NIBBLE_MASK = 0x0f;

    std::string out;
    out.reserve(data.length() * 2);

    for (uint8_t x : data) {
        // NOLINTNEXTLINE(*-pro-bounds-constant-array-index)
        out.push_back(TABLE[x >> NIBBLE_BITS]);
        out.push_back(TABLE[x & LEAST_SIGNIFICANT_NIBBLE_MASK]); // NOLINT(*-pro-bounds-constant-array-index)
    }

    return out;
}

std::string_view utils::safe_string_view(const char *cstr) {
    return (cstr != nullptr) ? std::string_view{cstr} : std::string_view{};
}

} // namespace ag
