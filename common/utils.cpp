#include <algorithm>
#include <array>
#include <codecvt>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <locale>
#include <numeric>
#include <random>

#include "common/socket_address.h"
#include "common/utils.h"

namespace ag {

std::string utils::generate_uuid() {
    static std::mt19937 rng = std::mt19937(std::random_device{}());

    union {
        uint32_t int_parts[4];
        uint16_t short_parts[8];
    } uuid_parts{};

    for (size_t i = 0; i < sizeof(uuid_parts)/sizeof(uint32_t); i++) {
        uuid_parts.int_parts[i] = (uint32_t)rng();
    }

    // set version to 4
    uuid_parts.short_parts[3] &= 0x0fff;
    uuid_parts.short_parts[3] |= 0x4000;

    // set variant according to section 4.1.1 of RFC 4122
    uuid_parts.short_parts[4] &= 0x3fff;
    uuid_parts.short_parts[4] |= 0x8000;

    return AG_FMT("{:04x}{:04x}-{:04x}-{:04x}-{:04x}-{:04x}{:04x}{:04x}",
            uuid_parts.short_parts[0], uuid_parts.short_parts[1],
            uuid_parts.short_parts[2], uuid_parts.short_parts[3], uuid_parts.short_parts[4],
            uuid_parts.short_parts[5], uuid_parts.short_parts[6], uuid_parts.short_parts[7]);
}

std::vector<std::string_view> utils::split_by(std::string_view str, std::string_view delim, bool include_empty, bool need_trim) {
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
            std::string_view s = str.substr(seek, length);
            if (need_trim) {
                s = utils::trim(s);
            }
            if (include_empty || !s.empty()) {
                out.push_back(s);
            }
        }
        seek = end + delim.length();
    }
    out.shrink_to_fit();

    return out;
}

std::vector<std::string_view> utils::split_by(std::string_view str, int delim, bool include_empty, bool need_trim) {
    auto ch = (char) delim;
    return split_by_any_of(str, {&ch, 1}, include_empty, need_trim);
}

std::vector<std::string_view> utils::split_by_any_of(std::string_view str, std::string_view delim, bool include_empty, bool need_trim) {
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
            std::string_view s = str.substr(seek, length);
            if (need_trim) {
                s = utils::trim(s);
            }
            if (include_empty || !s.empty()) {
                out.push_back(s);
            }
        }
        seek = end + 1;
    }
    out.shrink_to_fit();

    return out;
}

static std::array<std::string_view, 2> split2(std::string_view str, std::string_view delim, bool reverse, bool need_trim) {
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

    if (need_trim) {
        first = utils::trim(first);
        second = utils::trim(second);
    }

    return {first, second};
}

std::array<std::string_view, 2> utils::split2_by(std::string_view str, int delim, bool need_trim) {
    auto ch = (char) delim;
    return split2(str, {&ch, 1}, false, need_trim);
}

std::array<std::string_view, 2> utils::rsplit2_by(std::string_view str, int delim, bool need_trim) {
    auto ch = (char) delim;
    return split2(str, {&ch, 1}, true, need_trim);
}

std::array<std::string_view, 2> utils::split2_by_any_of(std::string_view str, std::string_view delim, bool need_trim) {
    return split2(str, delim, false, need_trim);
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

static int parse_hex_char(char c) {
    static constexpr int ASCII_NUMBER_OFFSET = 0x30;
    static constexpr int ASCII_LOWERCASE_LETTER_OFFSET = 0x57;
    static constexpr int ASCII_CAPITALCASE_LETTER_OFFSET = 0x37;
    if (c >= '0' && c <= '9') {
        return c - ASCII_NUMBER_OFFSET;
    }
    if (c >= 'a' && c <= 'f') {
        return c - ASCII_LOWERCASE_LETTER_OFFSET;
    }
    if (c >= 'A' && c <= 'F') {
        return c - ASCII_CAPITALCASE_LETTER_OFFSET;
    }
    return -1;
}

Uint8Vector utils::decode_hex(std::string_view hex) {
    static constexpr size_t NIBBLE_BITS = 4;
    static constexpr size_t UNSIGNED_MASK = 1;
    if (hex.size() & UNSIGNED_MASK) {
        return {};
    }
    Uint8Vector result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = parse_hex_char(hex[i]);
        int lo = parse_hex_char(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return {};
        }
        result.push_back((uint8_t) (hi << NIBBLE_BITS | lo));
    }
    return result;
}

std::string_view utils::safe_string_view(const char *cstr) {
    return (cstr != nullptr) ? std::string_view{cstr} : std::string_view{};
}

std::string utils::escape_argument_for_shell(std::string_view arg) {
    std::string out;
    out.reserve(arg.size() + 10);
    out += '\'';
    for (char ch : arg) {
        if (ch != '\'') {
            out += ch;
        } else {
            out += "'\\''";
        }
    }
    out += '\'';
    return out;
}

std::vector<std::string_view> word_wrap(std::string_view text, size_t width) {
    if (width == 0 || text.size() <= width) {
        return {text};
    }

    std::vector<std::string_view> result;
    result.reserve((text.size() / width) + 1);

    size_t cur_pos = 0;
    while (cur_pos < text.size()) {
        size_t cur_width = (text.size() - cur_pos < width) ? text.size() - cur_pos : width;
        auto segment = text.substr(cur_pos, cur_width);

        // If we are not at the end, and the next char is not a space,
        // attempt to break the line on the last space in current segment.
        if (cur_pos + cur_width < text.size() && text[cur_pos + cur_width] != ' ') {
            auto last_space_pos = segment.find_last_of(' ');
            if (last_space_pos != std::string_view::npos && last_space_pos != 0) {
                cur_width = last_space_pos; // Break the line at the last space.
                segment = text.substr(cur_pos, cur_width);
            }
        }

        result.push_back(segment);
        cur_pos += cur_width;

        // Skip any spaces after the segment to avoid starting the next line with a space.
        while (cur_pos < text.size() && text[cur_pos] == ' ') {
            ++cur_pos;
        }
    }

    return result;
}

} // namespace ag
